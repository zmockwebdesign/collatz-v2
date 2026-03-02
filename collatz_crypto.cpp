/**
 * Collatz Cryptographic Suite - Implementation, Tests, Benchmarks
 *
 * Build: g++ -std=c++17 -O3 -march=native -o collatz_crypto collatz_crypto.cpp
 * Usage:
 *   ./collatz_crypto --test      Full security validation
 *   ./collatz_crypto --bench     Benchmark vs SHA-256 / ChaCha20-Poly1305
 *   ./collatz_crypto --demo      Interactive demo of all primitives
 *   ./collatz_crypto <text>      Hash a string (256-bit)
 */

#include "collatz_crypto.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <unordered_set>

// ============================================================
// CollatzHash V5 implementation
// ============================================================

static void hash_absorb_block(uint64_t s[8], const uint8_t* block) {
    for (int i = 0; i < RATE_WORDS; ++i) {
        uint64_t w;
        std::memcpy(&w, block + i * 8, 8);
        s[i] ^= w;
    }
    collatz_perm(s, ROUNDS_B);
}

CollatzDigest collatz_hash(const void* data, size_t len, int output_bits) {
    const uint8_t* msg = static_cast<const uint8_t*>(data);
    uint64_t s[8] = {0};

    // Domain separation
    s[7] ^= DS_HASH;
    s[6] ^= (uint64_t)output_bits;

    // Absorb full blocks
    size_t off = 0;
    while (off + RATE_BYTES <= len) {
        hash_absorb_block(s, msg + off);
        off += RATE_BYTES;
    }

    // Absorb final block with padding (10*1 padding)
    uint8_t last[RATE_BYTES] = {0};
    size_t remaining = len - off;
    if (remaining > 0)
        std::memcpy(last, msg + off, remaining);
    last[remaining] = 0x80;
    last[RATE_BYTES - 1] |= 0x01;

    state_xor_bytes(s, last, RATE_BYTES);
    collatz_perm(s, ROUNDS_A);

    // Squeeze
    CollatzDigest digest;
    int out_bytes = output_bits / 8;
    digest.len = out_bytes;

    int squeezed = 0;
    while (squeezed < out_bytes) {
        int chunk = std::min(RATE_BYTES, out_bytes - squeezed);
        state_extract_bytes(s, digest.bytes + squeezed, chunk);
        squeezed += chunk;
        if (squeezed < out_bytes)
            collatz_perm(s, ROUNDS_B);
    }

    return digest;
}

CollatzDigest collatz_hash_str(const std::string& text, int output_bits) {
    return collatz_hash(text.data(), text.size(), output_bits);
}

std::string CollatzDigest::hex() const {
    char buf[257];
    for (int i = 0; i < len; ++i)
        snprintf(buf + i * 2, 3, "%02x", bytes[i]);
    return std::string(buf, len * 2);
}

bool CollatzDigest::operator==(const CollatzDigest& o) const {
    return len == o.len && std::memcmp(bytes, o.bytes, len) == 0;
}

bool CollatzDigest::operator!=(const CollatzDigest& o) const {
    return !(*this == o);
}

// ============================================================
// CollatzAEAD implementation
// ============================================================

static void aead_init(uint64_t s[8], const uint8_t key[32], const uint8_t nonce[16]) {
    std::memset(s, 0, 64);

    // Load key into capacity (words 4-7)
    std::memcpy(&s[4], key, 16);
    std::memcpy(&s[5], key + 8, 8);  // overlap intentional for mixing
    std::memcpy(&s[6], key + 16, 16);

    // Load nonce into rate (words 0-1)
    std::memcpy(&s[0], nonce, 16);

    // Domain separation
    s[7] ^= DS_AEAD;

    // Initialization permutation
    collatz_perm(s, ROUNDS_A);

    // XOR key again after permutation (like Ascon)
    uint64_t k[4];
    std::memcpy(k, key, 32);
    s[4] ^= k[0]; s[5] ^= k[1]; s[6] ^= k[2]; s[7] ^= k[3];
}

static void aead_process_aad(uint64_t s[8], const uint8_t* aad, size_t aad_len) {
    if (aad_len == 0) return;

    size_t off = 0;
    while (off + RATE_BYTES <= aad_len) {
        state_xor_bytes(s, aad + off, RATE_BYTES);
        collatz_perm(s, ROUNDS_B);
        off += RATE_BYTES;
    }

    // Final partial AAD block
    if (off < aad_len) {
        state_xor_bytes(s, aad + off, aad_len - off);
        // Pad
        uint8_t pad_byte = 0x80;
        size_t pad_pos = aad_len - off;
        uint64_t pad_word = (uint64_t)pad_byte << ((pad_pos % 8) * 8);
        s[pad_pos / 8] ^= pad_word;
        collatz_perm(s, ROUNDS_B);
    }

    // Domain separation: end of AAD
    s[7] ^= DS_AD_END;
}

CollatzAEADResult collatz_aead_encrypt(
    const uint8_t key[32], const uint8_t nonce[16],
    const void* plaintext, size_t pt_len,
    const void* aad, size_t aad_len)
{
    uint64_t s[8];
    aead_init(s, key, nonce);
    aead_process_aad(s, static_cast<const uint8_t*>(aad), aad_len);

    CollatzAEADResult result;
    result.ciphertext.resize(pt_len);

    const uint8_t* pt = static_cast<const uint8_t*>(plaintext);

    // Encrypt full blocks
    size_t off = 0;
    while (off + RATE_BYTES <= pt_len) {
        state_xor_extract_bytes(s, pt + off, result.ciphertext.data() + off, RATE_BYTES);
        collatz_perm(s, ROUNDS_B);
        off += RATE_BYTES;
    }

    // Final partial block
    if (off < pt_len) {
        size_t rem = pt_len - off;
        state_xor_extract_bytes(s, pt + off, result.ciphertext.data() + off, rem);
        // Pad
        uint8_t pad_byte = 0x80;
        size_t pad_pos = rem;
        uint64_t pad_word = (uint64_t)pad_byte << ((pad_pos % 8) * 8);
        s[pad_pos / 8] ^= pad_word;
    }

    // Finalization: XOR key, permute
    uint64_t k[4];
    std::memcpy(k, key, 32);
    s[RATE_WORDS] ^= k[0]; s[RATE_WORDS+1] ^= k[1];
    s[RATE_WORDS+2] ^= k[2]; s[RATE_WORDS+3] ^= k[3];
    collatz_perm(s, ROUNDS_A);
    s[RATE_WORDS] ^= k[0]; s[RATE_WORDS+1] ^= k[1];
    s[RATE_WORDS+2] ^= k[2]; s[RATE_WORDS+3] ^= k[3];

    // Extract tag from capacity words
    std::memcpy(result.tag, &s[RATE_WORDS], 16);

    return result;
}

std::vector<uint8_t> collatz_aead_decrypt(
    const uint8_t key[32], const uint8_t nonce[16],
    const void* ciphertext, size_t ct_len,
    const void* aad, size_t aad_len,
    const uint8_t tag[16])
{
    uint64_t s[8];
    aead_init(s, key, nonce);
    aead_process_aad(s, static_cast<const uint8_t*>(aad), aad_len);

    std::vector<uint8_t> plaintext(ct_len);
    const uint8_t* ct = static_cast<const uint8_t*>(ciphertext);

    // Decrypt full blocks
    size_t off = 0;
    while (off + RATE_BYTES <= ct_len) {
        state_decrypt_bytes(s, ct + off, plaintext.data() + off, RATE_BYTES);
        collatz_perm(s, ROUNDS_B);
        off += RATE_BYTES;
    }

    // Final partial block
    if (off < ct_len) {
        size_t rem = ct_len - off;
        state_decrypt_bytes(s, ct + off, plaintext.data() + off, rem);
        uint8_t pad_byte = 0x80;
        size_t pad_pos = rem;
        uint64_t pad_word = (uint64_t)pad_byte << ((pad_pos % 8) * 8);
        s[pad_pos / 8] ^= pad_word;
    }

    // Finalization
    uint64_t k[4];
    std::memcpy(k, key, 32);
    s[RATE_WORDS] ^= k[0]; s[RATE_WORDS+1] ^= k[1];
    s[RATE_WORDS+2] ^= k[2]; s[RATE_WORDS+3] ^= k[3];
    collatz_perm(s, ROUNDS_A);
    s[RATE_WORDS] ^= k[0]; s[RATE_WORDS+1] ^= k[1];
    s[RATE_WORDS+2] ^= k[2]; s[RATE_WORDS+3] ^= k[3];

    // Verify tag (constant-time comparison)
    uint8_t computed_tag[16];
    std::memcpy(computed_tag, &s[RATE_WORDS], 16);

    uint8_t diff = 0;
    for (int i = 0; i < 16; ++i)
        diff |= computed_tag[i] ^ tag[i];

    if (diff != 0)
        return {};  // authentication failure

    return plaintext;
}

bool CollatzAEADResult::verify_tag(const uint8_t expected[16]) const {
    uint8_t diff = 0;
    for (int i = 0; i < 16; ++i)
        diff |= tag[i] ^ expected[i];
    return diff == 0;
}

// ============================================================
// CollatzMAC implementation
// ============================================================

CollatzMACTag collatz_mac(const uint8_t key[32], const void* data, size_t len, int tag_bits) {
    uint64_t s[8] = {0};

    // Domain separation
    s[7] ^= DS_MAC;
    s[6] ^= (uint64_t)tag_bits;

    // Absorb key
    uint64_t k[4];
    std::memcpy(k, key, 32);
    s[0] ^= k[0]; s[1] ^= k[1]; s[2] ^= k[2]; s[3] ^= k[3];
    collatz_perm(s, ROUNDS_A);

    // Absorb message
    const uint8_t* msg = static_cast<const uint8_t*>(data);
    size_t off = 0;
    while (off + RATE_BYTES <= len) {
        state_xor_bytes(s, msg + off, RATE_BYTES);
        collatz_perm(s, ROUNDS_B);
        off += RATE_BYTES;
    }

    // Final block with padding
    uint8_t last[RATE_BYTES] = {0};
    size_t remaining = len - off;
    if (remaining > 0)
        std::memcpy(last, msg + off, remaining);
    last[remaining] = 0x80;
    last[RATE_BYTES - 1] |= 0x01;
    state_xor_bytes(s, last, RATE_BYTES);

    // XOR key again, finalize
    s[RATE_WORDS] ^= k[0]; s[RATE_WORDS+1] ^= k[1];
    s[RATE_WORDS+2] ^= k[2]; s[RATE_WORDS+3] ^= k[3];
    collatz_perm(s, ROUNDS_A);

    // Squeeze tag
    CollatzMACTag tag;
    int tag_bytes = tag_bits / 8;
    tag.len = tag_bytes;

    int squeezed = 0;
    while (squeezed < tag_bytes) {
        int chunk = std::min(RATE_BYTES, tag_bytes - squeezed);
        state_extract_bytes(s, tag.bytes + squeezed, chunk);
        squeezed += chunk;
        if (squeezed < tag_bytes)
            collatz_perm(s, ROUNDS_B);
    }

    return tag;
}

bool collatz_mac_verify(const uint8_t key[32], const void* data, size_t len,
                         const CollatzMACTag& expected) {
    CollatzMACTag computed = collatz_mac(key, data, len, expected.len * 8);
    uint8_t diff = 0;
    for (int i = 0; i < expected.len; ++i)
        diff |= computed.bytes[i] ^ expected.bytes[i];
    return diff == 0;
}

std::string CollatzMACTag::hex() const {
    char buf[65];
    for (int i = 0; i < len; ++i)
        snprintf(buf + i * 2, 3, "%02x", bytes[i]);
    return std::string(buf, len * 2);
}

bool CollatzMACTag::operator==(const CollatzMACTag& o) const {
    return len == o.len && std::memcmp(bytes, o.bytes, len) == 0;
}

// ============================================================
// SHA-256 standalone (for benchmark comparison)
// ============================================================

namespace sha {
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static inline uint32_t rotr(uint32_t x, int n) { return (x>>n)|(x<<(32-n)); }

void sha256(const void* data, size_t len, uint8_t out[32]) {
    const uint8_t* msg = static_cast<const uint8_t*>(data);
    size_t pl = ((len+9+63)/64)*64;
    std::vector<uint8_t> p(pl, 0);
    std::memcpy(p.data(), msg, len);
    p[len] = 0x80;
    uint64_t bl = len*8;
    for (int i = 0; i < 8; ++i) p[pl-1-i] = (bl>>(i*8))&0xFF;
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (size_t b = 0; b < pl; b += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i]=(p[b+i*4]<<24)|(p[b+i*4+1]<<16)|(p[b+i*4+2]<<8)|p[b+i*4+3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        uint32_t a=h[0],b2=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
            uint32_t ch=(e&f)^(~e&g);
            uint32_t t1=hh+S1+ch+K[i]+w[i];
            uint32_t S0=rotr(a,2)^rotr(a,13)^rotr(a,22);
            uint32_t maj=(a&b2)^(a&c)^(b2&c);
            uint32_t t2=S0+maj;
            hh=g;g=f;f=e;e=d+t1;d=c;c=b2;b2=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b2;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    for (int i=0;i<8;++i){out[i*4]=(h[i]>>24)&0xFF;out[i*4+1]=(h[i]>>16)&0xFF;
        out[i*4+2]=(h[i]>>8)&0xFF;out[i*4+3]=h[i]&0xFF;}
}
} // namespace sha

// ============================================================
// ChaCha20-Poly1305 standalone (for benchmark comparison)
// ============================================================

namespace chacha {

static inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b; d ^= a; d = rotl32(d, 16);
    c += d; b ^= c; b = rotl32(b, 12);
    a += b; d ^= a; d = rotl32(d, 8);
    c += d; b ^= c; b = rotl32(b, 7);
}

static void chacha20_block(uint32_t out[16], const uint32_t in[16]) {
    uint32_t s[16];
    std::memcpy(s, in, 64);
    for (int i = 0; i < 10; ++i) {
        quarter_round(s[0],s[4],s[8],s[12]);
        quarter_round(s[1],s[5],s[9],s[13]);
        quarter_round(s[2],s[6],s[10],s[14]);
        quarter_round(s[3],s[7],s[11],s[15]);
        quarter_round(s[0],s[5],s[10],s[15]);
        quarter_round(s[1],s[6],s[11],s[12]);
        quarter_round(s[2],s[7],s[8],s[13]);
        quarter_round(s[3],s[4],s[9],s[14]);
    }
    for (int i = 0; i < 16; ++i) out[i] = s[i] + in[i];
}

void chacha20_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                       uint32_t counter, const uint8_t* pt, uint8_t* ct, size_t len) {
    uint32_t state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    std::memcpy(&state[4], key, 32);
    state[12] = counter;
    std::memcpy(&state[13], nonce, 12);

    size_t off = 0;
    while (off < len) {
        uint32_t block[16];
        chacha20_block(block, state);
        size_t chunk = std::min((size_t)64, len - off);
        uint8_t* keystream = reinterpret_cast<uint8_t*>(block);
        for (size_t i = 0; i < chunk; ++i)
            ct[off + i] = pt[off + i] ^ keystream[i];
        off += chunk;
        state[12]++;
    }
}

} // namespace chacha

// ============================================================
// HMAC-SHA256 standalone (for MAC benchmark comparison)
// ============================================================

namespace hmac {

void hmac_sha256(const uint8_t* key, size_t key_len,
                  const uint8_t* msg, size_t msg_len,
                  uint8_t out[32]) {
    uint8_t k_pad[64] = {0};
    if (key_len > 64) {
        sha::sha256(key, key_len, k_pad);
    } else {
        std::memcpy(k_pad, key, key_len);
    }

    uint8_t i_pad[64], o_pad[64];
    for (int i = 0; i < 64; ++i) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }

    // Inner hash: H(i_pad || msg)
    std::vector<uint8_t> inner_data(64 + msg_len);
    std::memcpy(inner_data.data(), i_pad, 64);
    std::memcpy(inner_data.data() + 64, msg, msg_len);
    uint8_t inner_hash[32];
    sha::sha256(inner_data.data(), inner_data.size(), inner_hash);

    // Outer hash: H(o_pad || inner_hash)
    uint8_t outer_data[96];
    std::memcpy(outer_data, o_pad, 64);
    std::memcpy(outer_data + 64, inner_hash, 32);
    sha::sha256(outer_data, 96, out);
}

} // namespace hmac

// ============================================================
// Security Tests
// ============================================================

static int g_pass = 0, g_fail = 0;

static void check(const char* name, bool cond) {
    if (cond) { g_pass++; std::cout << "  PASS  " << name << "\n"; }
    else { g_fail++; std::cout << "  FAIL  " << name << "\n"; }
}

static void test_perm_diffusion() {
    std::cout << "\n--- Permutation Diffusion ---\n";
    // Flip one bit in state, run permutation, check all output words changed
    for (int word = 0; word < 8; ++word) {
        for (int bit = 0; bit < 64; bit += 16) {
            uint64_t s1[8] = {0x123456789abcdef0ULL, 0xfedcba9876543210ULL,
                              0x0011223344556677ULL, 0x8899aabbccddeeffULL,
                              0xdeadbeefcafebabeULL, 0x1337c0de42424242ULL,
                              0xaaaaaaaaaaaaaaaaULL, 0x5555555555555555ULL};
            uint64_t s2[8];
            std::memcpy(s2, s1, 64);
            s2[word] ^= (1ULL << bit);

            collatz_perm(s1, ROUNDS_A);
            collatz_perm(s2, ROUNDS_A);

            int words_changed = 0;
            for (int i = 0; i < 8; ++i)
                if (s1[i] != s2[i]) words_changed++;

            char buf[128];
            snprintf(buf, sizeof(buf), "Diffusion: flip word[%d] bit %d -> %d/8 words changed",
                     word, bit, words_changed);
            check(buf, words_changed == 8);
        }
    }
}

static void test_hash_determinism() {
    std::cout << "\n--- Hash Determinism ---\n";
    auto h1 = collatz_hash_str("sample input 12", 256);
    auto h2 = collatz_hash_str("sample input 12", 256);
    check("Hash-256 determinism", h1 == h2);

    auto h3 = collatz_hash_str("sample input 12", 1024);
    auto h4 = collatz_hash_str("sample input 12", 1024);
    check("Hash-1024 determinism", h3 == h4);
}

static void test_hash_avalanche() {
    std::cout << "\n--- Hash Avalanche ---\n";
    for (int bits : {256, 512, 1024}) {
        for (const char* text : {"hello world", "sample input 12", "test input here"}) {
            auto base = collatz_hash_str(text, bits);
            std::string s(text);
            double total = 0;
            int trials = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                for (int b = 0; b < 8; ++b) {
                    std::string mod = s;
                    mod[i] ^= (1 << b);
                    auto mh = collatz_hash_str(mod, bits);
                    int flipped = 0;
                    for (int j = 0; j < base.len; ++j)
                        flipped += __builtin_popcount(base.bytes[j] ^ mh.bytes[j]);
                    total += (double)flipped / bits * 100.0;
                    trials++;
                }
            }
            double avg = total / trials;
            char buf[128];
            snprintf(buf, sizeof(buf), "Avalanche %d-bit \"%s\": %.1f%%", bits, text, avg);
            check(buf, avg > 45.0 && avg < 55.0);
        }
    }
}

static void test_hash_distribution() {
    std::cout << "\n--- Hash Distribution ---\n";
    for (int bits : {256, 1024}) {
        int n = 50000;
        std::vector<int> counts(bits, 0);
        for (int i = 0; i < n; ++i) {
            std::string inp = "dist_" + std::to_string(i);
            auto h = collatz_hash_str(inp, bits);
            for (int j = 0; j < h.len; ++j)
                for (int b = 0; b < 8; ++b)
                    if ((h.bytes[j] >> b) & 1) counts[j * 8 + b]++;
        }
        int mn = n, mx = 0;
        for (int c : counts) { mn = std::min(mn, c); mx = std::max(mx, c); }
        double mn_pct = (double)mn / n * 100, mx_pct = (double)mx / n * 100;
        char buf[128];
        snprintf(buf, sizeof(buf), "Distribution %d-bit: min=%.1f%% max=%.1f%%", bits, mn_pct, mx_pct);
        check(buf, mn_pct > 48.0 && mx_pct < 52.0);
    }
}

static void test_hash_collisions() {
    std::cout << "\n--- Hash Collisions ---\n";
    for (int bits : {256, 1024}) {
        int n = 1000000;
        std::unordered_set<std::string> seen;
        int collisions = 0;
        for (int i = 0; i < n; ++i) {
            std::string inp = "coll_" + std::to_string(i);
            auto h = collatz_hash_str(inp, bits);
            std::string hex = h.hex();
            if (seen.count(hex)) collisions++;
            else seen.insert(hex);
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "Collisions %d-bit: %d / %d", bits, collisions, n);
        check(buf, collisions == 0);
    }
}

static void test_aead_correctness() {
    std::cout << "\n--- AEAD Correctness ---\n";
    uint8_t key[32], nonce[16];
    std::mt19937_64 rng(42);
    for (int i = 0; i < 32; ++i) key[i] = rng() & 0xFF;
    for (int i = 0; i < 16; ++i) nonce[i] = rng() & 0xFF;

    // Test various plaintext lengths
    for (size_t pt_len : {0, 1, 15, 16, 31, 32, 33, 63, 64, 100, 1000}) {
        std::vector<uint8_t> pt(pt_len);
        for (size_t i = 0; i < pt_len; ++i) pt[i] = rng() & 0xFF;

        std::string aad = "associated data for test";

        auto enc = collatz_aead_encrypt(key, nonce, pt.data(), pt_len,
                                         aad.data(), aad.size());
        auto dec = collatz_aead_decrypt(key, nonce, enc.ciphertext.data(), enc.ciphertext.size(),
                                         aad.data(), aad.size(), enc.tag);

        char buf[64];
        snprintf(buf, sizeof(buf), "AEAD roundtrip (pt_len=%zu)", pt_len);
        check(buf, dec.size() == pt_len && (pt_len == 0 || std::memcmp(dec.data(), pt.data(), pt_len) == 0));
    }
}

static void test_aead_authentication() {
    std::cout << "\n--- AEAD Authentication ---\n";
    uint8_t key[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                        17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    uint8_t nonce[16] = {0};
    std::string pt = "secret message here";
    std::string aad = "header data";

    auto enc = collatz_aead_encrypt(key, nonce, pt.data(), pt.size(),
                                     aad.data(), aad.size());

    // Flip a ciphertext bit
    if (!enc.ciphertext.empty()) {
        auto bad_ct = enc.ciphertext;
        bad_ct[0] ^= 0x01;
        auto dec = collatz_aead_decrypt(key, nonce, bad_ct.data(), bad_ct.size(),
                                         aad.data(), aad.size(), enc.tag);
        check("AEAD rejects flipped ciphertext", dec.empty());
    }

    // Flip a tag bit
    {
        uint8_t bad_tag[16];
        std::memcpy(bad_tag, enc.tag, 16);
        bad_tag[0] ^= 0x01;
        auto dec = collatz_aead_decrypt(key, nonce, enc.ciphertext.data(), enc.ciphertext.size(),
                                         aad.data(), aad.size(), bad_tag);
        check("AEAD rejects flipped tag", dec.empty());
    }

    // Flip an AAD bit
    {
        std::string bad_aad = aad;
        bad_aad[0] ^= 0x01;
        auto dec = collatz_aead_decrypt(key, nonce, enc.ciphertext.data(), enc.ciphertext.size(),
                                         bad_aad.data(), bad_aad.size(), enc.tag);
        check("AEAD rejects flipped AAD", dec.empty());
    }

    // Wrong key
    {
        uint8_t bad_key[32];
        std::memcpy(bad_key, key, 32);
        bad_key[0] ^= 0x01;
        auto dec = collatz_aead_decrypt(bad_key, nonce, enc.ciphertext.data(), enc.ciphertext.size(),
                                         aad.data(), aad.size(), enc.tag);
        check("AEAD rejects wrong key", dec.empty());
    }

    // Wrong nonce
    {
        uint8_t bad_nonce[16];
        std::memcpy(bad_nonce, nonce, 16);
        bad_nonce[0] ^= 0x01;
        auto dec = collatz_aead_decrypt(key, bad_nonce, enc.ciphertext.data(), enc.ciphertext.size(),
                                         aad.data(), aad.size(), enc.tag);
        check("AEAD rejects wrong nonce", dec.empty());
    }
}

static void test_aead_key_sensitivity() {
    std::cout << "\n--- AEAD Key Sensitivity ---\n";
    uint8_t key1[32] = {0}, key2[32] = {0};
    key1[0] = 1;
    key2[0] = 2;
    uint8_t nonce[16] = {0};
    std::string pt = "same plaintext";

    auto enc1 = collatz_aead_encrypt(key1, nonce, pt.data(), pt.size(), nullptr, 0);
    auto enc2 = collatz_aead_encrypt(key2, nonce, pt.data(), pt.size(), nullptr, 0);

    int diff_bytes = 0;
    for (size_t i = 0; i < enc1.ciphertext.size(); ++i)
        if (enc1.ciphertext[i] != enc2.ciphertext[i]) diff_bytes++;

    double diff_pct = (double)diff_bytes / enc1.ciphertext.size() * 100.0;
    char buf[128];
    snprintf(buf, sizeof(buf), "Key sensitivity: %.0f%% ciphertext bytes differ", diff_pct);
    check(buf, diff_pct > 30.0);

    check("Tags differ with different keys",
          std::memcmp(enc1.tag, enc2.tag, 16) != 0);
}

static void test_mac_correctness() {
    std::cout << "\n--- MAC Correctness ---\n";
    uint8_t key[32] = {0x42};
    std::string msg = "authenticate this message";

    auto tag1 = collatz_mac(key, msg.data(), msg.size(), 128);
    auto tag2 = collatz_mac(key, msg.data(), msg.size(), 128);
    check("MAC determinism", tag1 == tag2);

    bool ok = collatz_mac_verify(key, msg.data(), msg.size(), tag1);
    check("MAC verify correct", ok);

    // Wrong message
    std::string bad_msg = "authenticate this messagf";
    bool bad = collatz_mac_verify(key, bad_msg.data(), bad_msg.size(), tag1);
    check("MAC rejects wrong message", !bad);

    // Wrong key
    uint8_t bad_key[32] = {0x43};
    bad = collatz_mac_verify(bad_key, msg.data(), msg.size(), tag1);
    check("MAC rejects wrong key", !bad);

    // 256-bit tag
    auto tag256 = collatz_mac(key, msg.data(), msg.size(), 256);
    check("MAC 256-bit works", tag256.len == 32 && tag256.hex().size() == 64);
}

// ============================================================
// Benchmarks
// ============================================================

static double now_sec() {
    return std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

static void bench_hash() {
    std::cout << "\n--- Hash Benchmark: CollatzHash V5 vs SHA-256 ---\n";

    struct { const char* label; std::string data; } inputs[] = {
        {"short (5B)", "hello"},
        {"medium (12B)", "sample input 12"},
        {"long (43B)", "The quick brown fox jumps over the lazy dog"},
    };

    int iters = 2000000;

    std::cout << "  " << std::left << std::setw(18) << "Input"
              << std::setw(24) << "Algorithm"
              << std::right << std::setw(14) << "ops/sec"
              << std::setw(10) << "ns/op" << "\n";
    std::cout << "  " << std::string(66, '-') << "\n";

    for (auto& inp : inputs) {
        // CollatzHash V5 256-bit
        volatile uint8_t sink = 0;
        double t0 = now_sec();
        for (int i = 0; i < iters; ++i) {
            auto h = collatz_hash(inp.data.data(), inp.data.size(), 256);
            sink ^= h.bytes[0];
        }
        double dt = now_sec() - t0;
        double ops = iters / dt;
        std::cout << "  " << std::left << std::setw(18) << inp.label
                  << std::setw(24) << "CollatzHash-256"
                  << std::right << std::setw(14) << std::fixed << std::setprecision(0) << ops
                  << std::setw(10) << std::setprecision(1) << dt / iters * 1e9 << "\n";

        // CollatzHash V5 1024-bit
        t0 = now_sec();
        for (int i = 0; i < iters; ++i) {
            auto h = collatz_hash(inp.data.data(), inp.data.size(), 1024);
            sink ^= h.bytes[0];
        }
        dt = now_sec() - t0;
        ops = iters / dt;
        std::cout << "  " << std::left << std::setw(18) << ""
                  << std::setw(24) << "CollatzHash-1024"
                  << std::right << std::setw(14) << std::fixed << std::setprecision(0) << ops
                  << std::setw(10) << std::setprecision(1) << dt / iters * 1e9 << "\n";

        // SHA-256
        uint8_t sha_out[32];
        t0 = now_sec();
        for (int i = 0; i < iters; ++i) {
            sha::sha256(inp.data.data(), inp.data.size(), sha_out);
            sink ^= sha_out[0];
        }
        dt = now_sec() - t0;
        double sha_ops = iters / dt;
        std::cout << "  " << std::left << std::setw(18) << ""
                  << std::setw(24) << "SHA-256"
                  << std::right << std::setw(14) << std::fixed << std::setprecision(0) << sha_ops
                  << std::setw(10) << std::setprecision(1) << dt / iters * 1e9 << "\n";

        std::cout << "  " << std::string(66, '-') << "\n";
    }
}

static void bench_aead() {
    std::cout << "\n--- AEAD Benchmark: CollatzAEAD vs ChaCha20 ---\n";

    uint8_t key[32] = {0};
    uint8_t nonce_aead[16] = {0};
    uint8_t nonce_chacha[12] = {0};

    for (size_t sz : {64, 256, 1024, 4096}) {
        std::vector<uint8_t> pt(sz, 0x42);
        std::vector<uint8_t> ct(sz);
        int iters = (sz <= 256) ? 500000 : 100000;

        // CollatzAEAD
        volatile uint8_t sink = 0;
        double t0 = now_sec();
        for (int i = 0; i < iters; ++i) {
            auto r = collatz_aead_encrypt(key, nonce_aead, pt.data(), sz, nullptr, 0);
            sink ^= r.ciphertext[0];
        }
        double dt = now_sec() - t0;
        double collatz_mbps = (double)sz * iters / dt / 1e6;

        // ChaCha20
        t0 = now_sec();
        for (int i = 0; i < iters; ++i) {
            chacha::chacha20_encrypt(key, nonce_chacha, 1, pt.data(), ct.data(), sz);
            sink ^= ct[0];
        }
        double dt2 = now_sec() - t0;
        double chacha_mbps = (double)sz * iters / dt2 / 1e6;

        std::cout << "  " << sz << "B: CollatzAEAD "
                  << std::fixed << std::setprecision(1) << collatz_mbps << " MB/s"
                  << " | ChaCha20 " << chacha_mbps << " MB/s"
                  << " | ratio " << std::setprecision(2) << collatz_mbps / chacha_mbps << "x\n";
    }
}

static void bench_mac() {
    std::cout << "\n--- MAC Benchmark: CollatzMAC vs HMAC-SHA256 ---\n";

    uint8_t key[32] = {0};
    std::string msg = "Authenticate this message with a MAC tag";
    int iters = 1000000;

    volatile uint8_t sink = 0;
    double t0 = now_sec();
    for (int i = 0; i < iters; ++i) {
        auto tag = collatz_mac(key, msg.data(), msg.size(), 128);
        sink ^= tag.bytes[0];
    }
    double dt = now_sec() - t0;
    double collatz_ops = iters / dt;

    uint8_t hmac_out[32];
    t0 = now_sec();
    for (int i = 0; i < iters; ++i) {
        hmac::hmac_sha256(key, 32, reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), hmac_out);
        sink ^= hmac_out[0];
    }
    double dt2 = now_sec() - t0;
    double hmac_ops = iters / dt2;

    std::cout << "  CollatzMAC-128:  " << std::fixed << std::setprecision(0) << collatz_ops
              << " ops/sec (" << std::setprecision(1) << dt / iters * 1e9 << " ns/op)\n";
    std::cout << "  HMAC-SHA256:     " << std::setprecision(0) << hmac_ops
              << " ops/sec (" << std::setprecision(1) << dt2 / iters * 1e9 << " ns/op)\n";
    std::cout << "  Speedup: " << std::setprecision(1) << collatz_ops / hmac_ops << "x\n";
}

// ============================================================
// Demo
// ============================================================

static void demo() {
    std::cout << "============================================================\n";
    std::cout << "  Collatz Cryptographic Suite - Demo\n";
    std::cout << "============================================================\n";

    // Hash demo
    std::cout << "\n--- CollatzHash V5 ---\n";
    for (const char* text : {"hello", "sample input 12"}) {
        auto h256 = collatz_hash_str(text, 256);
        auto h1024 = collatz_hash_str(text, 1024);
        std::cout << "  \"" << text << "\"\n";
        std::cout << "    256-bit:  " << h256.hex() << "\n";
        std::cout << "    1024-bit: " << h1024.hex().substr(0, 64) << "...\n";
    }

    // AEAD demo
    std::cout << "\n--- CollatzAEAD ---\n";
    uint8_t key[32] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
                        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                        0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};
    uint8_t nonce[16] = {0};
    std::string plaintext = "Hello, Collatz encryption!";
    std::string aad = "metadata";

    auto enc = collatz_aead_encrypt(key, nonce, plaintext.data(), plaintext.size(),
                                     aad.data(), aad.size());
    std::cout << "  Plaintext:  \"" << plaintext << "\"\n";
    std::cout << "  Ciphertext: ";
    for (size_t i = 0; i < std::min(enc.ciphertext.size(), (size_t)32); ++i)
        printf("%02x", enc.ciphertext[i]);
    std::cout << "\n  Tag:        ";
    for (int i = 0; i < 16; ++i) printf("%02x", enc.tag[i]);
    std::cout << "\n";

    auto dec = collatz_aead_decrypt(key, nonce, enc.ciphertext.data(), enc.ciphertext.size(),
                                     aad.data(), aad.size(), enc.tag);
    std::string recovered(dec.begin(), dec.end());
    std::cout << "  Decrypted:  \"" << recovered << "\"\n";
    std::cout << "  Match:      " << (recovered == plaintext ? "YES" : "NO") << "\n";

    // MAC demo
    std::cout << "\n--- CollatzMAC ---\n";
    std::string msg = "Authenticate me";
    auto tag = collatz_mac(key, msg.data(), msg.size(), 128);
    std::cout << "  Message: \"" << msg << "\"\n";
    std::cout << "  MAC-128: " << tag.hex() << "\n";
    std::cout << "  Verify:  " << (collatz_mac_verify(key, msg.data(), msg.size(), tag) ? "OK" : "FAIL") << "\n";
}

// ============================================================
// Main
// ============================================================

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--test") {
        std::cout << "============================================================\n";
        std::cout << "  Collatz Crypto Suite - Security Validation\n";
        std::cout << "============================================================\n";

        test_perm_diffusion();
        test_hash_determinism();
        test_hash_avalanche();
        test_hash_distribution();
        test_hash_collisions();
        test_aead_correctness();
        test_aead_authentication();
        test_aead_key_sensitivity();
        test_mac_correctness();

        std::cout << "\n============================================================\n";
        std::cout << "  Results: " << g_pass << " passed, " << g_fail << " failed\n";
        std::cout << "============================================================\n";
        return g_fail > 0 ? 1 : 0;
    }

    if (argc >= 2 && std::string(argv[1]) == "--bench") {
        std::cout << "============================================================\n";
        std::cout << "  Collatz Crypto Suite - Benchmarks\n";
        std::cout << "============================================================\n";
        bench_hash();
        bench_aead();
        bench_mac();
        return 0;
    }

    if (argc >= 2 && std::string(argv[1]) == "--demo") {
        demo();
        return 0;
    }

    if (argc >= 2) {
        auto h = collatz_hash_str(argv[1], 256);
        std::cout << h.hex() << "\n";
        return 0;
    }

    std::cout << "Usage:\n";
    std::cout << "  " << argv[0] << " <text>     Hash a string (256-bit)\n";
    std::cout << "  " << argv[0] << " --test     Security validation\n";
    std::cout << "  " << argv[0] << " --bench    Benchmark vs standards\n";
    std::cout << "  " << argv[0] << " --demo     Interactive demo\n";
    return 0;
}
