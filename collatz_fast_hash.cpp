/**
 * CollatzHash V4 - Assembly-tuned for Apple Silicon (ARM64).
 *
 * Optimizations over the C++ baseline:
 *   1. ARM64 inline asm for collatz_rapid_mix: hand-scheduled instruction
 *      pipeline using ADD+LSL (3n), RBIT+CLZ (ctz), MUL+UMULH (128-bit)
 *   2. __attribute__((always_inline)) on all hot functions -- the compiler
 *      was NOT inlining absorb(), causing function call overhead per hash
 *   3. Parallel squeeze permutation: all 3 state mixes use old values,
 *      enabling full instruction-level parallelism on M3's 8-wide pipeline
 *   4. Single Collatz iteration variant (V4b) for maximum throughput
 *
 * Build: g++ -std=c++17 -O3 -march=native -o collatz_fast_hash collatz_fast_hash.cpp
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#define FORCE_INLINE __attribute__((always_inline)) inline

static constexpr uint64_t SECRET[8] = {
    0x2d358dccaa6c78a5ULL, 0x8bb84b93962eacc9ULL,
    0x4b33a62ed433d4a3ULL, 0x4d5a2da51de1aa47ULL,
    0xa0761d6478bd642fULL, 0xe7037ed1a0b428dbULL,
    0x90ed1765281c388cULL, 0xaaaaaaaaaaaaaaaaULL
};

// ============================================================
// Core primitive - architecture-specific implementations
// ============================================================

#if defined(__aarch64__)

/**
 * ARM64 inline asm: Collatz step (2 iterations) + 128-bit multiply-XOR.
 * 16 instructions, hand-scheduled for Apple M-series pipeline.
 */
static FORCE_INLINE uint64_t collatz_rapid_mix(uint64_t a, uint64_t b) {
    uint64_t result, t1, t2;
    __asm__ __volatile__ (
        "orr  %[a], %[a], #1          \n\t"
        "add  %[t1], %[a], %[a], lsl #1\n\t"  // t1 = 3*a
        "add  %[t1], %[t1], #1         \n\t"  // t1 = 3*a + 1
        "rbit %[t2], %[t1]             \n\t"
        "clz  %[t2], %[t2]             \n\t"  // t2 = ctz(t1)
        "lsr  %[a],  %[t1], %[t2]      \n\t"  // a = t1 >> ctz (always odd)
        "add  %[t1], %[a], %[a], lsl #1\n\t"  // second Collatz iteration
        "add  %[t1], %[t1], #1         \n\t"
        "rbit %[t2], %[t1]             \n\t"
        "clz  %[t2], %[t2]             \n\t"
        "lsr  %[a],  %[t1], %[t2]      \n\t"
        "mul   %[t1], %[a], %[b]       \n\t"  // lo = a * b
        "umulh %[t2], %[a], %[b]       \n\t"  // hi = a * b
        "eor  %[a],  %[a],  %[t1]      \n\t"  // a ^= lo
        "eor  %[b],  %[b],  %[t2]      \n\t"  // b ^= hi
        "eor  %[res], %[a], %[b]       \n\t"  // result = a ^ b
        : [res] "=&r" (result), [a] "+&r" (a), [b] "+&r" (b),
          [t1] "=&r" (t1), [t2] "=&r" (t2)
        :
        :
    );
    return result;
}

/**
 * ARM64 inline asm: single Collatz iteration variant.
 * 11 instructions -- ~30% fewer cycles on the critical path.
 */
static FORCE_INLINE uint64_t collatz_rapid_mix_fast(uint64_t a, uint64_t b) {
    uint64_t result, t1, t2;
    __asm__ __volatile__ (
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
        "eor  %[res], %[a], %[b]       \n\t"
        : [res] "=&r" (result), [a] "+&r" (a), [b] "+&r" (b),
          [t1] "=&r" (t1), [t2] "=&r" (t2)
        :
        :
    );
    return result;
}

#else
// C++ fallback for x86_64 and other architectures

static FORCE_INLINE void collatz_mum(uint64_t* a, uint64_t* b) {
    __uint128_t r = (__uint128_t)(*a) * (__uint128_t)(*b);
    *a ^= (uint64_t)r;
    *b ^= (uint64_t)(r >> 64);
}

static FORCE_INLINE uint64_t collatz_step_2iter(uint64_t n) {
    n |= 1ULL;
    uint64_t t = 3 * n + 1;
    t >>= __builtin_ctzll(t);
    uint64_t t2 = 3 * t + 1;
    t2 >>= __builtin_ctzll(t2);
    return t2;
}

static FORCE_INLINE uint64_t collatz_step_1iter(uint64_t n) {
    n |= 1ULL;
    uint64_t t = 3 * n + 1;
    t >>= __builtin_ctzll(t);
    return t;
}

static FORCE_INLINE uint64_t collatz_rapid_mix(uint64_t a, uint64_t b) {
    a = collatz_step_2iter(a);
    collatz_mum(&a, &b);
    return a ^ b;
}

static FORCE_INLINE uint64_t collatz_rapid_mix_fast(uint64_t a, uint64_t b) {
    a = collatz_step_1iter(a);
    collatz_mum(&a, &b);
    return a ^ b;
}

#endif // __aarch64__

// ============================================================
// Read helpers
// ============================================================

static FORCE_INLINE uint64_t read64(const uint8_t* p) {
    uint64_t v; std::memcpy(&v, p, 8); return v;
}

static FORCE_INLINE uint64_t read32(const uint8_t* p) {
    uint32_t v; std::memcpy(&v, p, 4); return v;
}

// ============================================================
// Templated core: MixFn selects 2-iter or 1-iter variant
// ============================================================

template<uint64_t (*MixFn)(uint64_t, uint64_t)>
static FORCE_INLINE void absorb(const uint8_t* p, size_t len,
                                 uint64_t& s0, uint64_t& s1, uint64_t& s2) {
    s0 = SECRET[0] ^ len;
    s1 = SECRET[1];
    s2 = SECRET[2];
    s0 = MixFn(s0, SECRET[3]);

    size_t i = len;

    if (i <= 16) {
        uint64_t a = 0, b = 0;
        if (i >= 4) {
            if (i >= 8) { a = read64(p); b = read64(p + i - 8); }
            else { a = read32(p); b = read32(p + i - 4); }
        } else if (i > 0) {
            a = ((uint64_t)p[0] << 45) | p[i - 1];
            b = p[i >> 1];
        }
        s0 ^= a; s1 ^= b; s2 ^= i;
    } else if (i <= 48) {
        s0 = MixFn(read64(p) ^ SECRET[0], read64(p + 8) ^ s0);
        if (i > 32) s1 = MixFn(read64(p + 16) ^ SECRET[1], read64(p + 24) ^ s1);
        if (i > 40) s2 = MixFn(read64(p + 32) ^ SECRET[2], read64(p + 40) ^ s2);
        s0 ^= read64(p + i - 16);
        s1 ^= read64(p + i - 8);
    } else {
        do {
            s0 = MixFn(read64(p) ^ SECRET[0], read64(p + 8) ^ s0);
            s1 = MixFn(read64(p + 16) ^ SECRET[1], read64(p + 24) ^ s1);
            s2 = MixFn(read64(p + 32) ^ SECRET[2], read64(p + 40) ^ s2);
            p += 48; i -= 48;
        } while (i > 48);

        if (i > 16) {
            s0 = MixFn(read64(p) ^ SECRET[3], read64(p + 8) ^ s0);
            if (i > 32) s1 = MixFn(read64(p + 16) ^ SECRET[4], read64(p + 24) ^ s1);
        }
        s0 ^= read64(p + i - 16) ^ i;
        s1 ^= read64(p + i - 8);
    }
}

template<uint64_t (*MixFn)(uint64_t, uint64_t)>
static FORCE_INLINE void finalize(uint64_t& s0, uint64_t& s1, uint64_t& s2, size_t len) {
    s0 ^= len;
    s2 ^= SECRET[7];
    s0 = MixFn(s0, s1 ^ SECRET[4]);
    s1 = MixFn(s1, s2 ^ SECRET[5]);
    s2 = MixFn(s2, s0 ^ SECRET[6]);
}

// ============================================================
// Sponge squeeze - parallel permutation for ILP
// ============================================================

struct CollatzHashResult {
    static constexpr int MAX_WORDS = 16;
    uint64_t h[MAX_WORDS];
    int num_words;

    std::string hex() const {
        std::ostringstream ss;
        for (int i = 0; i < num_words; ++i)
            ss << std::setfill('0') << std::setw(16) << std::hex << h[i];
        return ss.str();
    }
};

/**
 * Parallel squeeze: all 3 mixes use OLD state values, enabling the CPU
 * to execute all 3 mix pipelines simultaneously on Apple M3's 8-wide backend.
 */
template<uint64_t (*MixFn)(uint64_t, uint64_t)>
static FORCE_INLINE CollatzHashResult squeeze(uint64_t s0, uint64_t s1, uint64_t s2,
                                               int output_bits) {
    CollatzHashResult result;
    result.num_words = output_bits / 64;
    if (result.num_words > CollatzHashResult::MAX_WORDS)
        result.num_words = CollatzHashResult::MAX_WORDS;

    for (int i = 0; i < result.num_words; ++i) {
        result.h[i] = s0 ^ s1 ^ s2;

        // All 3 mixes read only OLD state -- fully parallel on wide pipelines
        uint64_t ns0 = MixFn(s0, s1 ^ (uint64_t)(i + 1));
        uint64_t ns1 = MixFn(s1, s2 ^ SECRET[i & 7]);
        uint64_t ns2 = MixFn(s2, s0 ^ SECRET[(i + 3) & 7]);
        s0 = ns0; s1 = ns1; s2 = ns2;
    }

    return result;
}

// ============================================================
// Public API
// ============================================================

// V4a: 2 Collatz iterations per mix (stronger nonlinearity)
CollatzHashResult collatz_hash_v4(const void* data, size_t len, int output_bits = 1024) {
    const uint8_t* input = static_cast<const uint8_t*>(data);
    uint64_t s0, s1, s2;
    absorb<collatz_rapid_mix>(input, len, s0, s1, s2);
    finalize<collatz_rapid_mix>(s0, s1, s2, len);
    return squeeze<collatz_rapid_mix>(s0, s1, s2, output_bits);
}

// V4b: 1 Collatz iteration per mix (maximum speed)
CollatzHashResult collatz_hash_v4b(const void* data, size_t len, int output_bits = 1024) {
    const uint8_t* input = static_cast<const uint8_t*>(data);
    uint64_t s0, s1, s2;
    absorb<collatz_rapid_mix_fast>(input, len, s0, s1, s2);
    finalize<collatz_rapid_mix_fast>(s0, s1, s2, len);
    return squeeze<collatz_rapid_mix_fast>(s0, s1, s2, output_bits);
}

CollatzHashResult collatz_hash_v4_str(const std::string& text, int output_bits = 1024) {
    return collatz_hash_v4(text.data(), text.size(), output_bits);
}

CollatzHashResult collatz_hash_v4b_str(const std::string& text, int output_bits = 1024) {
    return collatz_hash_v4b(text.data(), text.size(), output_bits);
}

uint64_t collatz_hash_v4_64(const void* data, size_t len) {
    const uint8_t* input = static_cast<const uint8_t*>(data);
    uint64_t s0, s1, s2;
    absorb<collatz_rapid_mix>(input, len, s0, s1, s2);
    finalize<collatz_rapid_mix>(s0, s1, s2, len);
    return s0 ^ s1 ^ s2;
}

uint64_t collatz_hash_v4b_64(const void* data, size_t len) {
    const uint8_t* input = static_cast<const uint8_t*>(data);
    uint64_t s0, s1, s2;
    absorb<collatz_rapid_mix_fast>(input, len, s0, s1, s2);
    finalize<collatz_rapid_mix_fast>(s0, s1, s2, len);
    return s0 ^ s1 ^ s2;
}

// ============================================================
// Tests
// ============================================================

void test_determinism(const char* label, CollatzHashResult (*fn)(const void*, size_t, int)) {
    auto h1 = fn("sample input 12", 12, 1024);
    auto h2 = fn("sample input 12", 12, 1024);
    std::cout << "  Determinism (" << label << "): "
              << (h1.hex() == h2.hex() ? "PASS" : "FAIL") << "\n";
}

double test_avalanche(const std::string& text, int output_bits,
                      CollatzHashResult (*fn)(const void*, size_t, int), int trials = 500) {
    auto base = fn(text.data(), text.size(), output_bits);
    double total_flip_pct = 0;
    std::vector<uint8_t> buf(text.begin(), text.end());

    for (int t = 0; t < trials; ++t) {
        int byte_idx = t % buf.size();
        int bit_idx = (t / buf.size()) % 8;
        auto modified = buf;
        modified[byte_idx] ^= (1 << bit_idx);
        auto mod_hash = fn(modified.data(), modified.size(), output_bits);

        int flipped = 0;
        int nw = output_bits / 64;
        for (int i = 0; i < nw; ++i)
            flipped += __builtin_popcountll(base.h[i] ^ mod_hash.h[i]);
        total_flip_pct += (double)flipped / (double)output_bits * 100.0;
    }
    return total_flip_pct / trials;
}

void test_distribution(int output_bits,
                       CollatzHashResult (*fn)(const void*, size_t, int),
                       int num_inputs = 50000) {
    int total_bits = output_bits;
    std::vector<int> bit_counts(total_bits, 0);
    int num_words = output_bits / 64;

    for (int i = 0; i < num_inputs; ++i) {
        std::string inp = "dist_test_" + std::to_string(i);
        auto h = fn(inp.data(), inp.size(), output_bits);
        for (int w = 0; w < num_words; ++w)
            for (int b = 0; b < 64; ++b)
                if ((h.h[w] >> b) & 1) bit_counts[w * 64 + b]++;
    }

    int min_c = num_inputs, max_c = 0;
    for (int i = 0; i < total_bits; ++i) {
        if (bit_counts[i] < min_c) min_c = bit_counts[i];
        if (bit_counts[i] > max_c) max_c = bit_counts[i];
    }
    double min_pct = (double)min_c / num_inputs * 100.0;
    double max_pct = (double)max_c / num_inputs * 100.0;
    bool pass = min_pct >= 48.0 && max_pct <= 52.0;
    std::cout << "  Distribution (" << output_bits << "-bit): min="
              << std::fixed << std::setprecision(1) << min_pct
              << "% max=" << max_pct << "% "
              << (pass ? "PASS" : "FAIL") << "\n";
}

void test_collisions(CollatzHashResult (*fn)(const void*, size_t, int),
                     const char* label, int num_inputs = 1000000) {
    std::unordered_set<std::string> seen;
    int collisions = 0;
    for (int i = 0; i < num_inputs; ++i) {
        std::string inp = "collision_" + std::to_string(i);
        auto h = fn(inp.data(), inp.size(), 1024);
        std::string hex = h.hex();
        if (seen.count(hex)) collisions++;
        else seen.insert(hex);
    }
    std::cout << "  Collisions (" << label << " 1024-bit): " << collisions
              << " / " << num_inputs << " " << (collisions == 0 ? "PASS" : "FAIL") << "\n";
}

// ============================================================
// Benchmark harness
// ============================================================

struct BenchResult {
    double ops_sec;
    double ns_op;
};

template<typename F>
BenchResult run_bench(F func, const void* data, size_t len, int iters) {
    volatile uint64_t sink = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; ++i)
        sink ^= func(data, len);
    auto end = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(end - start).count();
    return {(double)iters / sec, sec / iters * 1e9};
}

// ============================================================
// Main
// ============================================================

int main(int argc, char** argv) {
    // --test: full security validation for both variants
    if (argc >= 2 && std::string(argv[1]) == "--test") {
        std::cout << "=== CollatzHash V4 ASM-Tuned - Security Tests ===\n\n";

        std::cout << "[V4a] 2-iteration Collatz:\n";
        test_determinism("V4a", collatz_hash_v4);
        for (const auto& t : {"hello world", "sample input 12", "a", "The quick brown fox"}) {
            double avg = test_avalanche(t, 1024, collatz_hash_v4, 500);
            std::cout << "  Avalanche (\"" << t << "\"): "
                      << std::fixed << std::setprecision(1) << avg << "%\n";
        }
        test_distribution(1024, collatz_hash_v4, 50000);
        test_collisions(collatz_hash_v4, "V4a", 1000000);

        std::cout << "\n[V4b] 1-iteration Collatz (speed variant):\n";
        test_determinism("V4b", collatz_hash_v4b);
        for (const auto& t : {"hello world", "sample input 12", "a", "The quick brown fox"}) {
            double avg = test_avalanche(t, 1024, collatz_hash_v4b, 500);
            std::cout << "  Avalanche (\"" << t << "\"): "
                      << std::fixed << std::setprecision(1) << avg << "%\n";
        }
        test_distribution(1024, collatz_hash_v4b, 50000);
        test_collisions(collatz_hash_v4b, "V4b", 1000000);
        return 0;
    }

    // Hash a string
    if (argc >= 2 && std::string(argv[1]) != "--bench") {
        auto h = collatz_hash_v4_str(argv[1], 1024);
        std::cout << h.hex() << "\n";
        return 0;
    }

    // --bench: full benchmark comparison
    std::cout << "============================================================\n";
    std::cout << "  CollatzHash V4 - ARM64 Assembly-Tuned Benchmark\n";
    std::cout << "  Apple M3 Max | 1024-bit output (256 hex chars)\n";
    std::cout << "============================================================\n\n";

    int iters = 10000000;

    auto bench_1024 = [](const void* d, size_t l) -> uint64_t {
        auto h = collatz_hash_v4(d, l, 1024);
        return h.h[0];
    };
    auto bench_1024b = [](const void* d, size_t l) -> uint64_t {
        auto h = collatz_hash_v4b(d, l, 1024);
        return h.h[0];
    };
    auto bench_64a = [](const void* d, size_t l) -> uint64_t {
        return collatz_hash_v4_64(d, l);
    };
    auto bench_64b = [](const void* d, size_t l) -> uint64_t {
        return collatz_hash_v4b_64(d, l);
    };

    struct TestInput {
        const char* label;
        const char* data;
        size_t len;
    };

    TestInput inputs[] = {
        {"short (5B)  ", "hello", 5},
        {"medium (12B)", "sample input 12", 12},
        {"long (43B)  ", "The quick brown fox jumps over the lazy dog", 43},
    };

    std::cout << "  " << std::left << std::setw(16) << "Input"
              << std::setw(26) << "Variant"
              << std::right << std::setw(14) << "ops/sec"
              << std::setw(10) << "ns/op" << "\n";
    std::cout << "  " << std::string(66, '-') << "\n";

    for (auto& inp : inputs) {
        auto r1 = run_bench(bench_1024, inp.data, inp.len, iters);
        auto r2 = run_bench(bench_1024b, inp.data, inp.len, iters);
        auto r3 = run_bench(bench_64a, inp.data, inp.len, iters);
        auto r4 = run_bench(bench_64b, inp.data, inp.len, iters);

        std::cout << std::fixed;
        std::cout << "  " << std::left << std::setw(16) << inp.label
                  << std::setw(26) << "V4a 1024-bit (2-iter asm)"
                  << std::right << std::setw(14) << std::setprecision(0) << r1.ops_sec
                  << std::setw(10) << std::setprecision(1) << r1.ns_op << "\n";
        std::cout << "  " << std::left << std::setw(16) << ""
                  << std::setw(26) << "V4b 1024-bit (1-iter asm)"
                  << std::right << std::setw(14) << std::setprecision(0) << r2.ops_sec
                  << std::setw(10) << std::setprecision(1) << r2.ns_op << "\n";
        std::cout << "  " << std::left << std::setw(16) << ""
                  << std::setw(26) << "V4a  64-bit (2-iter asm)"
                  << std::right << std::setw(14) << std::setprecision(0) << r3.ops_sec
                  << std::setw(10) << std::setprecision(1) << r3.ns_op << "\n";
        std::cout << "  " << std::left << std::setw(16) << ""
                  << std::setw(26) << "V4b  64-bit (1-iter asm)"
                  << std::right << std::setw(14) << std::setprecision(0) << r4.ops_sec
                  << std::setw(10) << std::setprecision(1) << r4.ns_op << "\n";
        std::cout << "  " << std::string(66, '-') << "\n";
    }

    std::cout << "\n  Previous C++ baseline was ~10.4M ops/sec (1024-bit)\n";
    std::cout << "  and ~125M ops/sec (64-bit)\n";

    std::cout << "\nSample hashes (V4a 1024-bit):\n";
    for (const auto& text : {"hello", "sample input 12", "The quick brown fox jumps over the lazy dog"}) {
        auto h = collatz_hash_v4_str(text, 1024);
        std::string hex = h.hex();
        std::cout << "  \"" << text << "\"\n  -> "
                  << hex.substr(0, 64) << "\n     "
                  << hex.substr(64, 64) << "\n     "
                  << hex.substr(128, 64) << "\n     "
                  << hex.substr(192, 64) << "\n";
    }

    return 0;
}
