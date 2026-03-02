/**
 * CRF (Collatz Residue Fingerprint) collision test in C++.
 * Parallelized across all CPU cores.
 */
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

constexpr int64_t N0_START = 50'000'001;
constexpr int64_t N0_END = 249'999'999;
constexpr int64_t MAX_N = 1000000000000LL;

// Trailing zeros in binary (2-adic valuation). n must be even, non-zero.
inline int v2(uint64_t n) {
#ifdef __GNUC__
    return __builtin_ctzll(n);
#else
    if (n == 0) return -1;
    int k = 0;
    while ((n & 1) == 0) { n >>= 1; k++; }
    return k;
#endif
}

std::vector<int> get_crf_sequence(int64_t n0) {
    std::vector<int> seq;
    int64_t n = n0;

    while (true) {
        int64_t t = 3 * n + 1;
        int k = v2(static_cast<uint64_t>(t));
        seq.push_back(k);

        if (n == 1 || n > MAX_N) break;
        n = t >> k;
    }
    return seq;
}

// Build key: "len:N,k0,k1,..." - length prefix prevents cross-length collisions
std::string sequence_to_key(const std::vector<int>& seq) {
    std::string key = "len:" + std::to_string(seq.size()) + ",";
    key.reserve(key.size() + seq.size() * 4);
    for (size_t i = 0; i < seq.size(); ++i) {
        if (i > 0) key += ',';
        key += std::to_string(seq[i]);
    }
    return key;
}

void run_test_parallel(std::ofstream& out, int num_threads) {
    std::unordered_set<std::string> seen;
    std::mutex mtx;
    std::vector<std::pair<int64_t, std::string>> collisions;
    std::atomic<int64_t> orbits_processed{0};
    std::atomic<int64_t> orbits_skipped{0};
    std::atomic<size_t> min_collision_len{std::numeric_limits<size_t>::max()};

    auto worker = [&](int tid) {
        for (int64_t n0 = N0_START + tid * 2; n0 <= N0_END; n0 += num_threads * 2) {
            auto seq = get_crf_sequence(n0);
            int remove_pos = static_cast<int>(seq.size()) - 2;  // second-to-last only
            if (seq.size() < 3 || remove_pos < 0) {  // min len 3 so trimmed has 2+ elements
                orbits_skipped++;
                continue;
            }

            std::vector<int> seq_trimmed;
            seq_trimmed.reserve(seq.size() - 1);
            for (size_t i = 0; i < seq.size(); ++i) {
                if (static_cast<int>(i) != remove_pos) {
                    seq_trimmed.push_back(seq[i]);
                }
            }

            // Skip low-entropy sequences (all same value) - they cause most collisions
            bool has_variation = false;
            for (size_t i = 1; i < seq_trimmed.size(); ++i) {
                if (seq_trimmed[i] != seq_trimmed[0]) { has_variation = true; break; }
            }
            if (!has_variation) {
                orbits_skipped++;
                continue;
            }

            std::string key = sequence_to_key(seq_trimmed);
            {
                std::lock_guard<std::mutex> lock(mtx);
                auto it = seen.find(key);
                if (it != seen.end()) {
                    size_t len = seq_trimmed.size();
                    size_t cur = min_collision_len.load();
                    while (len < cur && !min_collision_len.compare_exchange_weak(cur, len)) {}
                    collisions.push_back({n0, key});
                } else {
                    seen.insert(std::move(key));
                }
            }
            orbits_processed++;
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& th : threads) th.join();

    int64_t total_proc = orbits_processed.load();
    int64_t total_skip = orbits_skipped.load();
    int collision_count = static_cast<int>(collisions.size());

    out << "--- Best config: 1 pairing (2nd-to-last) + length in key + skip low-entropy ---\n";
    out << "Threads: " << num_threads << "\n";
    out << "Orbits processed: " << total_proc << "\n";
    out << "Orbits skipped: " << total_skip << "\n";
    out << "Unique output sequences: " << seen.size() << "\n";
    out << "Collisions found: " << collision_count << "\n";
    if (collision_count > 0) {
        size_t mcl = min_collision_len.load();
        if (mcl != std::numeric_limits<size_t>::max()) {
            out << "Minimum sequence length among collisions: " << mcl << "\n";
        }
        out << "Sample collisions:\n";
        for (size_t i = 0; i < collisions.size() && i < 5; ++i) {
            out << "  n0=" << collisions[i].first;
            std::string& s = collisions[i].second;
            size_t len = std::min(s.size(), size_t(50));
            out << " seq: " << s.substr(0, len);
            if (s.size() > 50) out << "...";
            out << "\n";
        }
    }
    out << "\n";
}

int main() {
    int num_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (num_threads <= 0) num_threads = 14;

    std::ofstream out("crf_collision_test_results.txt");
    out << "Range: n0 in [" << N0_START << ", " << N0_END << "] (odd only)\n";
    out << "Max steps: none (run until orbit reaches 1)\n";
    out << "Threads: " << num_threads << "\n\n";

    run_test_parallel(out, num_threads);

    out.close();
    return 0;
}
