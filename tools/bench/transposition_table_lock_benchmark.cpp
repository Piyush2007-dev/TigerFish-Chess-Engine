#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <immintrin.h>
#include <mutex>
#include <memory>

struct PerftTTEntry {
    uint64_t key = 0;
    uint64_t occ = 0;
    uint64_t nodes = 0;
    int8_t depth = 0;
};

// 1. Current std::mutex array
struct TT_StdMutex {
    std::vector<PerftTTEntry> table;
    size_t size_mask;
    std::unique_ptr<std::mutex[]> locks;
    TT_StdMutex(size_t size) : table(size), size_mask(size - 1), locks(std::make_unique<std::mutex[]>(65536)) {}

    void store(uint64_t key, uint64_t occ, int depth, uint64_t nodes) {
        size_t idx = key & size_mask;
        size_t lock_idx = key & 65535;
        std::lock_guard<std::mutex> lock(locks[lock_idx]);
        table[idx] = {key, occ, nodes, (int8_t)depth};
    }
    bool probe(uint64_t key, uint64_t occ, int depth, uint64_t& out_nodes) {
        size_t idx = key & size_mask;
        size_t lock_idx = key & 65535;
        std::lock_guard<std::mutex> lock(locks[lock_idx]);
        if (table[idx].key == key && table[idx].occ == occ && table[idx].depth == depth) {
            out_nodes = table[idx].nodes;
            return true;
        }
        return false;
    }
};

// 2. Padded Spinlock with _mm_pause()
struct alignas(64) PaddedLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    inline void lock() noexcept {
        while (flag.test_and_set(std::memory_order_acquire)) {
            _mm_pause();
        }
    }
    inline void unlock() noexcept {
        flag.clear(std::memory_order_release);
    }
};

struct TT_PaddedSpin {
    std::vector<PerftTTEntry> table;
    size_t size_mask;
    static constexpr size_t NUM_LOCKS = 8192;
    std::unique_ptr<PaddedLock[]> locks;
    TT_PaddedSpin(size_t size) : table(size), size_mask(size - 1), locks(std::make_unique<PaddedLock[]>(NUM_LOCKS)) {}

    void store(uint64_t key, uint64_t occ, int depth, uint64_t nodes) {
        size_t idx = key & size_mask;
        size_t lock_idx = key & (NUM_LOCKS - 1);
        locks[lock_idx].lock();
        table[idx] = {key, occ, nodes, (int8_t)depth};
        locks[lock_idx].unlock();
    }
    bool probe(uint64_t key, uint64_t occ, int depth, uint64_t& out_nodes) {
        size_t idx = key & size_mask;
        size_t lock_idx = key & (NUM_LOCKS - 1);
        locks[lock_idx].lock();
        bool hit = (table[idx].key == key && table[idx].occ == occ && table[idx].depth == depth);
        if (hit) out_nodes = table[idx].nodes;
        locks[lock_idx].unlock();
        return hit;
    }
};

int main() {
    const size_t size = 1 << 22; // 4M entries
    const int num_threads = 24;
    const int ops_per_thread = 5'000'000;

    std::cout << "Benchmarking 24 threads under heavy concurrent TT access...\n";

    // Benchmark std::mutex
    {
        TT_StdMutex tt(size);
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                uint64_t seed = 12345 + t * 987654321ULL;
                for (int i = 0; i < ops_per_thread; ++i) {
                    seed ^= seed >> 12;
                    seed ^= seed << 25;
                    seed ^= seed >> 27;
                    seed *= 0x2545F4914F6CDD1DULL;
                    if (i % 2 == 0) {
                        tt.store(seed, seed ^ 1, 4, seed);
                    } else {
                        uint64_t n;
                        tt.probe(seed, seed ^ 1, 4, n);
                    }
                }
            });
        }
        for (auto& th : threads) th.join();
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "std::mutex [65536]: " << ms << " ms (" << (num_threads * ops_per_thread * 1000.0 / ms / 1e6) << " M ops/s)\n";
    }

    // Benchmark PaddedLock
    {
        TT_PaddedSpin tt(size);
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                uint64_t seed = 12345 + t * 987654321ULL;
                for (int i = 0; i < ops_per_thread; ++i) {
                    seed ^= seed >> 12;
                    seed ^= seed << 25;
                    seed ^= seed >> 27;
                    seed *= 0x2545F4914F6CDD1DULL;
                    if (i % 2 == 0) {
                        tt.store(seed, seed ^ 1, 4, seed);
                    } else {
                        uint64_t n;
                        tt.probe(seed, seed ^ 1, 4, n);
                    }
                }
            });
        }
        for (auto& th : threads) th.join();
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "PaddedSpin [8192]:  " << ms << " ms (" << (num_threads * ops_per_thread * 1000.0 / ms / 1e6) << " M ops/s)\n";
    }

    return 0;
}
