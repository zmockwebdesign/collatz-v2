/**
 * Filesystem hashing benchmark: CollatzHash V4b vs SHA-256
 * Walks a directory tree, reads every file, hashes with both algorithms,
 * and records per-file timing for a real-world performance comparison.
 *
 * Build: g++ -std=c++17 -O3 -march=native -o hash_filesystem hash_filesystem.cpp
 * Usage: ./hash_filesystem /path/to/directory
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#define FORCE_INLINE __attribute__((always_inline)) inline

// ============================================================
// CollatzHash V4b (1-iter asm, 1024-bit) -- inlined
// ============================================================
namespace collatz {

static constexpr uint64_t SECRET[8] = {
    0x2d358dccaa6c78a5ULL, 0x8bb84b93962eacc9ULL,
    0x4b33a62ed433d4a3ULL, 0x4d5a2da51de1aa47ULL,
    0xa0761d6478bd642fULL, 0xe7037ed1a0b428dbULL,
    0x90ed1765281c388cULL, 0xaaaaaaaaaaaaaaaaULL
};

#if defined(__aarch64__)
static FORCE_INLINE uint64_t mix(uint64_t a, uint64_t b) {
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
          [t1] "=&r" (t1), [t2] "=&r" (t2) : : );
    return result;
}
#else
static FORCE_INLINE void mum(uint64_t* a, uint64_t* b) {
    __uint128_t r = (__uint128_t)(*a) * (__uint128_t)(*b);
    *a ^= (uint64_t)r; *b ^= (uint64_t)(r >> 64);
}
static FORCE_INLINE uint64_t mix(uint64_t a, uint64_t b) {
    a |= 1ULL;
    uint64_t t = 3*a+1; t >>= __builtin_ctzll(t);
    a = t; mum(&a, &b); return a ^ b;
}
#endif

static FORCE_INLINE uint64_t read64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
static FORCE_INLINE uint64_t read32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

static FORCE_INLINE void absorb(const uint8_t* p, size_t len,
                                 uint64_t& s0, uint64_t& s1, uint64_t& s2) {
    s0 = SECRET[0] ^ len; s1 = SECRET[1]; s2 = SECRET[2];
    s0 = mix(s0, SECRET[3]);
    size_t i = len;
    if (i <= 16) {
        uint64_t a = 0, b = 0;
        if (i >= 4) {
            if (i >= 8) { a = read64(p); b = read64(p+i-8); }
            else { a = read32(p); b = read32(p+i-4); }
        } else if (i > 0) { a = ((uint64_t)p[0]<<45)|p[i-1]; b = p[i>>1]; }
        s0 ^= a; s1 ^= b; s2 ^= i;
    } else if (i <= 48) {
        s0 = mix(read64(p)^SECRET[0], read64(p+8)^s0);
        if (i > 32) s1 = mix(read64(p+16)^SECRET[1], read64(p+24)^s1);
        if (i > 40) s2 = mix(read64(p+32)^SECRET[2], read64(p+40)^s2);
        s0 ^= read64(p+i-16); s1 ^= read64(p+i-8);
    } else {
        do {
            s0 = mix(read64(p)^SECRET[0], read64(p+8)^s0);
            s1 = mix(read64(p+16)^SECRET[1], read64(p+24)^s1);
            s2 = mix(read64(p+32)^SECRET[2], read64(p+40)^s2);
            p += 48; i -= 48;
        } while (i > 48);
        if (i > 16) {
            s0 = mix(read64(p)^SECRET[3], read64(p+8)^s0);
            if (i > 32) s1 = mix(read64(p+16)^SECRET[4], read64(p+24)^s1);
        }
        s0 ^= read64(p+i-16)^i; s1 ^= read64(p+i-8);
    }
}

static FORCE_INLINE void finalize(uint64_t& s0, uint64_t& s1, uint64_t& s2, size_t len) {
    s0 ^= len; s2 ^= SECRET[7];
    s0 = mix(s0, s1^SECRET[4]);
    s1 = mix(s1, s2^SECRET[5]);
    s2 = mix(s2, s0^SECRET[6]);
}

std::string hash_1024(const uint8_t* data, size_t len) {
    uint64_t s0, s1, s2;
    absorb(data, len, s0, s1, s2);
    finalize(s0, s1, s2, len);

    char buf[257];
    for (int i = 0; i < 16; ++i) {
        uint64_t word = s0 ^ s1 ^ s2;
        snprintf(buf + i*16, 17, "%016llx", (unsigned long long)word);
        uint64_t ns0 = mix(s0, s1 ^ (uint64_t)(i+1));
        uint64_t ns1 = mix(s1, s2 ^ SECRET[i&7]);
        uint64_t ns2 = mix(s2, s0 ^ SECRET[(i+3)&7]);
        s0 = ns0; s1 = ns1; s2 = ns2;
    }
    buf[256] = 0;
    return std::string(buf, 256);
}

} // namespace collatz

// ============================================================
// SHA-256 (standalone C++)
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

static FORCE_INLINE uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32-n)); }

std::string sha256(const uint8_t* msg, size_t len) {
    size_t padded_len = ((len + 9 + 63) / 64) * 64;
    std::vector<uint8_t> padded(padded_len, 0);
    std::memcpy(padded.data(), msg, len);
    padded[len] = 0x80;
    uint64_t bit_len = len * 8;
    for (int i = 0; i < 8; ++i)
        padded[padded_len - 1 - i] = (bit_len >> (i * 8)) & 0xFF;

    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    for (size_t block = 0; block < padded_len; block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (padded[block+i*4]<<24)|(padded[block+i*4+1]<<16)|
                   (padded[block+i*4+2]<<8)|padded[block+i*4+3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            uint32_t s1 = rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i] = w[i-16]+s0+w[i-7]+s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
            uint32_t ch=(e&f)^(~e&g);
            uint32_t t1=hh+S1+ch+K[i]+w[i];
            uint32_t S0=rotr(a,2)^rotr(a,13)^rotr(a,22);
            uint32_t maj=(a&b)^(a&c)^(b&c);
            uint32_t t2=S0+maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }

    char buf[65];
    for (int i = 0; i < 8; ++i)
        snprintf(buf+i*8, 9, "%08x", h[i]);
    buf[64] = 0;
    return std::string(buf, 64);
}

} // namespace sha

// ============================================================
// Filesystem walker + benchmark
// ============================================================

struct FileResult {
    std::string path;
    size_t size;
    std::string hash;
    double hash_us;  // microseconds for hash computation only
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <directory> [max_file_mb]\n";
        return 1;
    }

    std::string root = argv[1];
    size_t max_file_bytes = 500ULL * 1024 * 1024;  // 500MB default
    if (argc >= 3) max_file_bytes = std::stoull(argv[2]) * 1024 * 1024;

    std::vector<FileResult> collatz_results;
    std::vector<FileResult> sha_results;

    size_t total_bytes = 0;
    size_t files_hashed = 0;
    size_t files_skipped = 0;
    size_t files_error = 0;

    double total_collatz_us = 0;
    double total_sha_us = 0;

    std::cout << "============================================================\n";
    std::cout << "  Filesystem Hash Benchmark\n";
    std::cout << "  CollatzHash V4b (1024-bit) vs SHA-256 (256-bit)\n";
    std::cout << "  Root: " << root << "\n";
    std::cout << "  Max file size: " << max_file_bytes / (1024*1024) << " MB\n";
    std::cout << "============================================================\n\n";
    std::cout << "Scanning..." << std::flush;

    auto wall_start = std::chrono::high_resolution_clock::now();

    auto it = fs::recursive_directory_iterator(
        root, fs::directory_options::skip_permission_denied);
    auto end = fs::recursive_directory_iterator();

    while (it != end) {
        std::error_code ec;
        try {
            auto& entry = *it;
            bool is_reg = false;
            try { is_reg = entry.is_regular_file(); } catch (...) { }
            bool is_sym = false;
            try { is_sym = entry.is_symlink(); } catch (...) { }

            if (!is_reg || is_sym) { it.increment(ec); if (ec) it = end; continue; }

            uintmax_t fsize = 0;
            try { fsize = entry.file_size(); } catch (...) { files_error++; it.increment(ec); if (ec) it = end; continue; }
            if (fsize == 0 || fsize > max_file_bytes) {
                if (fsize > max_file_bytes) files_skipped++;
                it.increment(ec); if (ec) it = end;
                continue;
            }

            std::string fpath = entry.path().string();

            std::ifstream file(fpath, std::ios::binary);
            if (!file.is_open()) { files_error++; it.increment(ec); if (ec) it = end; continue; }

            std::vector<uint8_t> buf(fsize);
            file.read(reinterpret_cast<char*>(buf.data()), fsize);
            if (!file) { files_error++; it.increment(ec); if (ec) it = end; continue; }
            file.close();

            const uint8_t* data = buf.data();
            size_t len = buf.size();

            auto t1 = std::chrono::high_resolution_clock::now();
            std::string chash = collatz::hash_1024(data, len);
            auto t2 = std::chrono::high_resolution_clock::now();
            double collatz_us = std::chrono::duration<double, std::micro>(t2 - t1).count();

            auto t3 = std::chrono::high_resolution_clock::now();
            std::string shash = sha::sha256(data, len);
            auto t4 = std::chrono::high_resolution_clock::now();
            double sha_us = std::chrono::duration<double, std::micro>(t4 - t3).count();

            collatz_results.push_back({fpath, len, chash, collatz_us});
            sha_results.push_back({fpath, len, shash, sha_us});

            total_bytes += len;
            total_collatz_us += collatz_us;
            total_sha_us += sha_us;
            files_hashed++;

            if (files_hashed % 200 == 0)
                std::cout << "\r  " << files_hashed << " files hashed ("
                          << total_bytes / (1024*1024) << " MB)..." << std::flush;

            it.increment(ec);
            if (ec) it = end;
        } catch (...) {
            files_error++;
            std::error_code ec2;
            it.increment(ec2);
            if (ec2) it = end;
        }
    }

    auto wall_end = std::chrono::high_resolution_clock::now();
    double wall_sec = std::chrono::duration<double>(wall_end - wall_start).count();

    std::cout << "\r                                                     \r";

    // Write CollatzHash results
    {
        std::ofstream out("collatz_v4b_filesystem_hashes.txt");
        out << "# CollatzHash V4b (1024-bit) - Filesystem Hashes\n";
        out << "# Directory: " << root << "\n";
        out << "# Files: " << files_hashed << " | Total: "
            << total_bytes / (1024*1024) << " MB\n";
        out << "# Total hash time: " << std::fixed << std::setprecision(3)
            << total_collatz_us / 1000.0 << " ms\n";
        out << "# Throughput: " << std::setprecision(1)
            << (total_bytes / (1024.0*1024.0)) / (total_collatz_us / 1e6) << " MB/s\n\n";
        for (auto& r : collatz_results)
            out << r.hash << "  " << r.path << "\n";
        out.close();
    }

    // Write SHA-256 results
    {
        std::ofstream out("sha256_filesystem_hashes.txt");
        out << "# SHA-256 (256-bit) - Filesystem Hashes\n";
        out << "# Directory: " << root << "\n";
        out << "# Files: " << files_hashed << " | Total: "
            << total_bytes / (1024*1024) << " MB\n";
        out << "# Total hash time: " << std::fixed << std::setprecision(3)
            << total_sha_us / 1000.0 << " ms\n";
        out << "# Throughput: " << std::setprecision(1)
            << (total_bytes / (1024.0*1024.0)) / (total_sha_us / 1e6) << " MB/s\n\n";
        for (auto& r : sha_results)
            out << r.hash << "  " << r.path << "\n";
        out.close();
    }

    // Print summary
    double collatz_sec = total_collatz_us / 1e6;
    double sha_sec = total_sha_us / 1e6;
    double collatz_mbps = (total_bytes / (1024.0*1024.0)) / collatz_sec;
    double sha_mbps = (total_bytes / (1024.0*1024.0)) / sha_sec;
    double total_mb = total_bytes / (1024.0*1024.0);

    std::cout << "============================================================\n";
    std::cout << "  RESULTS\n";
    std::cout << "============================================================\n";
    std::cout << std::fixed;
    std::cout << "  Files hashed:  " << files_hashed << "\n";
    std::cout << "  Files skipped: " << files_skipped << " (too large)\n";
    std::cout << "  Files error:   " << files_error << " (unreadable)\n";
    std::cout << "  Total data:    " << std::setprecision(1) << total_mb << " MB\n";
    std::cout << "  Wall time:     " << std::setprecision(2) << wall_sec << " s\n\n";

    std::cout << "  " << std::left << std::setw(26) << "Algorithm"
              << std::right << std::setw(12) << "Hash Time"
              << std::setw(14) << "Throughput"
              << std::setw(12) << "Output" << "\n";
    std::cout << "  " << std::string(64, '-') << "\n";
    std::cout << "  " << std::left << std::setw(26) << "CollatzHash V4b (asm)"
              << std::right << std::setw(10) << std::setprecision(3) << collatz_sec << " s"
              << std::setw(12) << std::setprecision(1) << collatz_mbps << " MB/s"
              << std::setw(12) << "1024-bit" << "\n";
    std::cout << "  " << std::left << std::setw(26) << "SHA-256 (standalone C++)"
              << std::right << std::setw(10) << std::setprecision(3) << sha_sec << " s"
              << std::setw(12) << std::setprecision(1) << sha_mbps << " MB/s"
              << std::setw(12) << "256-bit" << "\n";

    std::cout << "\n  Speedup: CollatzHash is " << std::setprecision(1)
              << sha_sec / collatz_sec << "x faster than SHA-256\n";
    std::cout << "  (while producing 4x more output bits: 1024 vs 256)\n";

    std::cout << "\n  Output files:\n";
    std::cout << "    collatz_v4b_filesystem_hashes.txt\n";
    std::cout << "    sha256_filesystem_hashes.txt\n";
    std::cout << "============================================================\n";

    return 0;
}
