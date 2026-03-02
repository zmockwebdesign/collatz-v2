#pragma once
/**
 * CollatzPerm-512: cryptographic permutation based on the Collatz mapping.
 *
 * State:  512 bits = 8 x uint64_t
 * Rounds: configurable (a=12 for init/final, b=8 for processing)
 *
 * Round function:
 *   1. Nonlinear layer  -- 4 parallel Collatz-step + 128-bit multiply pairs
 *   2. Linear diffusion -- cross-pair XOR + word rotation + bit rotation
 *   3. Round constant   -- break round symmetry
 *
 * The Collatz step provides mathematical nonlinearity (3n+1 with trailing-zero
 * removal). The 128-bit multiply provides massive diffusion (~128 bits mixed
 * per multiply). The linear layer ensures full diffusion across all 8 words
 * within ~3 rounds.
 */

#include <cstdint>
#include <cstring>

#define CP_FORCE_INLINE __attribute__((always_inline)) inline

// Round constants: first 12 digits of pi, e, sqrt(2), sqrt(3) combined
// with round index to break symmetry between rounds and between words.
static constexpr uint64_t COLLATZ_RC[12] = {
    0x243f6a8885a308d3ULL,  // pi
    0xb7e151628aed2a6aULL,  // e
    0x6a09e667f3bcc908ULL,  // sqrt(2)
    0xbb67ae8584caa73bULL,  // sqrt(3)
    0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL,
    0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL,
    0x5be0cd19137e2179ULL,
    0x428a2f98d728ae22ULL,
    0x7137449123ef65cdULL,
};

// Domain separation constants
static constexpr uint64_t DS_HASH   = 0x0000000000000001ULL;
static constexpr uint64_t DS_AEAD   = 0x0000000000000002ULL;
static constexpr uint64_t DS_MAC    = 0x0000000000000004ULL;
static constexpr uint64_t DS_AD_END = 0x0000000000000080ULL;
static constexpr uint64_t DS_FINAL  = 0x00000000000000FFULL;

// Sponge parameters
static constexpr int RATE_WORDS = 4;   // 256 bits
static constexpr int CAP_WORDS  = 4;   // 256 bits
static constexpr int RATE_BYTES = RATE_WORDS * 8;  // 32 bytes
static constexpr int ROUNDS_A   = 12;  // init / finalization
static constexpr int ROUNDS_B   = 8;   // per-block processing

static CP_FORCE_INLINE uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

// ============================================================
// Collatz-multiply pair: the nonlinear core
// ============================================================

#if defined(__aarch64__)

/**
 * ARM64 asm: pre-mix rotation (ensures every input bit matters, including bit 0),
 * then Collatz step on 'a', then 128-bit multiply a*b with XOR feedback.
 */
static CP_FORCE_INLINE void collatz_mul_pair(uint64_t& a, uint64_t& b) {
    uint64_t t1, t2;
    __asm__ __volatile__ (
        "eor  %[a], %[a], %[a], ror #41\n\t"  // pre-mix: rotl23, asymmetric
        "orr  %[a], %[a], #1          \n\t"
        "add  %[t1], %[a], %[a], lsl #1\n\t"
        "add  %[t1], %[t1], #1         \n\t"
        "rbit %[t2], %[t1]             \n\t"
        "clz  %[t2], %[t2]             \n\t"
        "lsr  %[a],  %[t1], %[t2]      \n\t"
        "mul   %[t1], %[a], %[b]       \n\t"
        "umulh %[t2], %[a], %[b]       \n\t"
        "eor  %[a],  %[a],  %[t1]      \n\t"
        "eor  %[b],  %[b],  %[t2]      \n\t"
        : [a] "+&r" (a), [b] "+&r" (b),
          [t1] "=&r" (t1), [t2] "=&r" (t2)
        :
        :
    );
}

#else

static CP_FORCE_INLINE void collatz_mul_pair(uint64_t& a, uint64_t& b) {
    a ^= rotl64(a, 23);  // pre-mix: asymmetric rotation, bit 0 -> bit 23
    a |= 1ULL;
    uint64_t t = 3 * a + 1;
    t >>= __builtin_ctzll(t);
    a = t;
    __uint128_t r = (__uint128_t)a * (__uint128_t)b;
    a ^= (uint64_t)r;
    b ^= (uint64_t)(r >> 64);
}

#endif

// ============================================================
// Single round of CollatzPerm-512
// ============================================================

static CP_FORCE_INLINE void collatz_perm_round(uint64_t s[8], int round) {
    // 1. Nonlinear layer: 4 parallel Collatz-multiply pairs
    collatz_mul_pair(s[0], s[1]);
    collatz_mul_pair(s[2], s[3]);
    collatz_mul_pair(s[4], s[5]);
    collatz_mul_pair(s[6], s[7]);

    // 2. Linear diffusion: cross-pair mixing
    s[0] ^= s[2];  s[4] ^= s[6];
    s[1] ^= s[3];  s[5] ^= s[7];

    // Word rotation: shift even-indexed words
    uint64_t tmp = s[0];
    s[0] = s[2]; s[2] = s[4]; s[4] = s[6]; s[6] = tmp;

    // Bit rotation mixing on odd-indexed words
    s[1] ^= rotl64(s[5], 19);
    s[3] ^= rotl64(s[7], 31);
    s[5] ^= rotl64(s[1], 7);
    s[7] ^= rotl64(s[3], 53);

    // 3. Round constant injection
    s[0] ^= COLLATZ_RC[round];
}

// ============================================================
// Full permutation: apply R rounds
// ============================================================

static CP_FORCE_INLINE void collatz_perm(uint64_t s[8], int rounds) {
    for (int r = 0; r < rounds; ++r)
        collatz_perm_round(s, r % 12);
}

// ============================================================
// Utility: load/store bytes into state words (little-endian)
// ============================================================

static CP_FORCE_INLINE void state_xor_bytes(uint64_t s[8], const uint8_t* data, size_t len) {
    size_t full_words = len / 8;
    for (size_t i = 0; i < full_words && i < RATE_WORDS; ++i) {
        uint64_t w;
        std::memcpy(&w, data + i * 8, 8);
        s[i] ^= w;
    }
    if (len % 8 != 0 && full_words < RATE_WORDS) {
        uint64_t w = 0;
        std::memcpy(&w, data + full_words * 8, len % 8);
        s[full_words] ^= w;
    }
}

static CP_FORCE_INLINE void state_extract_bytes(const uint64_t s[8], uint8_t* out, size_t len) {
    size_t full_words = len / 8;
    for (size_t i = 0; i < full_words && i < RATE_WORDS; ++i)
        std::memcpy(out + i * 8, &s[i], 8);
    if (len % 8 != 0 && full_words < RATE_WORDS)
        std::memcpy(out + full_words * 8, &s[full_words], len % 8);
}

static CP_FORCE_INLINE void state_xor_extract_bytes(uint64_t s[8],
                                                      const uint8_t* in,
                                                      uint8_t* out,
                                                      size_t len) {
    size_t full_words = len / 8;
    for (size_t i = 0; i < full_words && i < RATE_WORDS; ++i) {
        uint64_t w;
        std::memcpy(&w, in + i * 8, 8);
        uint64_t ct = s[i] ^ w;
        std::memcpy(out + i * 8, &ct, 8);
        s[i] = ct;
    }
    if (len % 8 != 0 && full_words < RATE_WORDS) {
        uint64_t w = 0;
        std::memcpy(&w, in + full_words * 8, len % 8);
        uint64_t ct = s[full_words] ^ w;
        uint64_t mask = (1ULL << ((len % 8) * 8)) - 1;
        std::memcpy(out + full_words * 8, &ct, len % 8);
        s[full_words] = (s[full_words] & ~mask) | (ct & mask);
    }
}

static CP_FORCE_INLINE void state_decrypt_bytes(uint64_t s[8],
                                                  const uint8_t* ct_in,
                                                  uint8_t* pt_out,
                                                  size_t len) {
    size_t full_words = len / 8;
    for (size_t i = 0; i < full_words && i < RATE_WORDS; ++i) {
        uint64_t c;
        std::memcpy(&c, ct_in + i * 8, 8);
        uint64_t p = s[i] ^ c;
        std::memcpy(pt_out + i * 8, &p, 8);
        s[i] = c;
    }
    if (len % 8 != 0 && full_words < RATE_WORDS) {
        uint64_t c = 0;
        std::memcpy(&c, ct_in + full_words * 8, len % 8);
        uint64_t p = s[full_words] ^ c;
        std::memcpy(pt_out + full_words * 8, &p, len % 8);
        uint64_t mask = (1ULL << ((len % 8) * 8)) - 1;
        s[full_words] = (s[full_words] & ~mask) | (c & mask);
    }
}
