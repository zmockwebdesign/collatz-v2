/**
 * Collatz Hash - C++ implementation for maximum speed.
 * Pure math hashing using the Collatz conjecture. No external crypto libs.
 *
 * Requires: GMP (GNU Multiple Precision) - brew install gmp
 * Build: g++ -std=c++17 -O3 -o collatz_hash collatz_hash.cpp -lgmp -lgmpxx
 */
#include <chrono>
#include <iostream>
#include <string>
#include <gmpxx.h>

constexpr long MULTIPLIER = 100'000'000'000'000L;  // 100 trillion

mpz_class text_to_number(const std::string& text) {
    mpz_class result = 0;
    for (unsigned char c : text) {
        result = (result << 7) + (c & 0x7F);
    }
    result *= MULTIPLIER;
    if (result % 2 == 0) {
        result = (result << 1) | 1;
    }
    return result;
}

void collatz_sequence(mpz_class n, std::vector<mpz_class>& out) {
    out.clear();
    out.push_back(n);
    while (n != 1) {
        if (mpz_odd_p(n.get_mpz_t())) {
            n = 3 * n + 1;
        } else {
            n = n >> 1;
        }
        out.push_back(n);
    }
}

mpz_class reverse_bits(mpz_class n, int width) {
    mpz_class result = 0;
    for (int i = 0; i < width; ++i) {
        result = (result << 1) | (n & 1);
        n >>= 1;
    }
    return result;
}

std::string collatz_hash(const std::string& text, int bit_width = 512) {
    mpz_class encoded = text_to_number(text);

    std::vector<mpz_class> sequence;
    collatz_sequence(encoded, sequence);

    // Concatenate all sequence values into one giant number
    mpz_class concat_bits = 0;
    for (const auto& val : sequence) {
        size_t val_bits = mpz_sizeinbase(val.get_mpz_t(), 2);
        if (val_bits == 0) val_bits = 1;
        concat_bits = (concat_bits << val_bits) | val;
    }

    // Split into rows of bit_width
    mpz_class mask = (mpz_class(1) << bit_width) - 1;
    std::vector<mpz_class> rows;
    mpz_class remaining = concat_bits;

    while (remaining > 0) {
        rows.push_back(remaining & mask);
        remaining >>= bit_width;
    }
    if (rows.empty()) rows.push_back(0);

    // Fold: even rows XOR, odd rows ADD with reversed bits
    mpz_class result = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i % 2 == 0) {
            result = result ^ rows[i];
        } else {
            mpz_class rev = reverse_bits(rows[i], bit_width);
            result = (result + rev) & mask;
        }
    }

    // Weighted sum mix
    mpz_class weighted_sum = 0;
    for (size_t i = 0; i < sequence.size(); ++i) {
        weighted_sum += sequence[i] * (i + 1);
    }
    result = (result ^ (weighted_sum & mask)) & mask;

    // To hex
    int target_len = bit_width / 4;
    std::string hex_hash = result.get_str(16);
    if ((int)hex_hash.size() < target_len) {
        hex_hash = std::string(target_len - hex_hash.size(), '0') + hex_hash;
    } else if ((int)hex_hash.size() > target_len) {
        hex_hash = hex_hash.substr(hex_hash.size() - target_len);
    }
    return hex_hash;
}

std::string collatz_hash_256(const std::string& text) {
    return collatz_hash(text, 256);
}

std::string collatz_hash_512(const std::string& text) {
    return collatz_hash(text, 512);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: collatz_hash <text> [benchmark_iterations]\n";
        std::cout << "  With 1 arg: hash the text and print result\n";
        std::cout << "  With 2 args: benchmark N iterations\n\n";
        std::cout << "Examples:\n";
        std::cout << "  ./collatz_hash 'hello'\n";
        std::cout << "  ./collatz_hash 'password' 10000\n";
        return 1;
    }

    std::string text = argv[1];

    if (argc >= 3) {
        int iters = std::stoi(argv[2]);
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iters; ++i) {
            collatz_hash_256(text);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "C++ collatz_hash_256: " << (ms / iters) << " ms/op ("
                  << (iters / (ms / 1000.0)) << " ops/sec) over " << iters << " iterations\n";
    } else {
        std::string h256 = collatz_hash_256(text);
        std::string h512 = collatz_hash_512(text);
        std::cout << "Input: \"" << text << "\"\n";
        std::cout << "Hash 256: " << h256 << "\n";
        std::cout << "Hash 512: " << h512.substr(0, 64) << "...\n";
    }
    return 0;
}
