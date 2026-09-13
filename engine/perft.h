#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — ADVANCED PARALLEL & TT PERFT (newengine/perft.h)
//
// Key Optimizations from Archive & Modern Engine Architecture:
// 1. PEXT / BMI2 native hardware slider attacks (_pext_u64).
// 2. Leaf-node bypass: if (depth == 1) return move_list.size().
// 3. Dynamic Work-Array with atomic work-stealing (splits 2-3 plies deep).
//    - Automatically load-balances fast P-cores and slower E-cores with 0 idle time.
// 4. Partitioned 65,536-bucket Transposition Table with Depth >= 3 threshold.
//    - Avoids TT overhead at depths 1-2; scales ultra-deep perft (depth 8-10).
// 5. Parallel split-perft: computes exact per-root-move node counts.
// 6. Built-in standardized benchmark suite (Startpos, Kiwipete, Positions 3, 4, 5, 6).
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "position.cpp"
#include "movegen.cpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <chrono>
#include <string>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tigerfish {

// ── Perft Transposition Table ────────────────────────────────────────────────
struct PerftTTEntry {
    uint64_t key   = 0;
    uint64_t occ   = 0;
    uint64_t nodes = 0;
    int8_t   depth = 0;
};

class PerftTT {
public:
    static constexpr size_t NUM_LOCKS = 65536;

    std::vector<PerftTTEntry> table;
    size_t size_mask = 0;
    std::unique_ptr<std::mutex[]> locks;

    PerftTT(size_t mb = 256) {
        size_t num_entries = (mb * 1024 * 1024) / sizeof(PerftTTEntry);
        size_t size = 1;
        while ((size << 1) <= num_entries) size <<= 1;
        table.resize(size);
        size_mask = size - 1;
        locks = std::make_unique<std::mutex[]>(NUM_LOCKS);
    }

    void clear() noexcept {
        for (size_t i = 0; i < NUM_LOCKS; ++i) {
            std::lock_guard<std::mutex> lock(locks[i]);
            for (size_t idx = i; idx < table.size(); idx += NUM_LOCKS) {
                table[idx] = PerftTTEntry{};
            }
        }
    }

    inline bool probe(uint64_t key, uint64_t occ, int depth, uint64_t& out_nodes) noexcept {
        size_t idx = key & size_mask;
        size_t lock_idx = key & (NUM_LOCKS - 1);

        std::lock_guard<std::mutex> lock(locks[lock_idx]);
        const PerftTTEntry& entry = table[idx];
        if (entry.key == key && entry.occ == occ && entry.depth == depth) {
            out_nodes = entry.nodes;
            return true;
        }
        return false;
    }

    inline void store(uint64_t key, uint64_t occ, int depth, uint64_t nodes) noexcept {
        size_t idx = key & size_mask;
        size_t lock_idx = key & (NUM_LOCKS - 1);

        std::lock_guard<std::mutex> lock(locks[lock_idx]);
        if (table[idx].depth <= depth) {
            table[idx].key   = key;
            table[idx].occ   = occ;
            table[idx].depth = static_cast<int8_t>(depth);
            table[idx].nodes = nodes;
        }
    }
};

// ── Single-Threaded Raw Perft (with Depth-1 Leaf Optimization) ───────────────
inline uint64_t perft_raw(Position& pos, int depth) noexcept {
    if (depth <= 0) return 1ULL;
    if (depth == 1) return count_legal_moves(pos);

    MoveList move_list;
    generate_legal_moves(pos, move_list);

    // Inlined depth 2: eliminates millions of recursive function call frames & move generation
    if (depth == 2) {
        uint64_t nodes = 0;
        StateInfo state;
        for (int i = 0; i < move_list.size(); ++i) {
            make_move(pos, move_list[i], state);
            nodes += count_legal_moves(pos);
            undo_move(pos, move_list[i]);
        }
        return nodes;
    }

    uint64_t nodes = 0;
    StateInfo state;
    for (int i = 0; i < move_list.size(); ++i) {
        make_move(pos, move_list[i], state);
        nodes += perft_raw(pos, depth - 1);
        undo_move(pos, move_list[i]);
    }
    return nodes;
}

// ── Single-Threaded TT-Accelerated Perft (Threshold Depth >= 3) ──────────────
inline uint64_t perft_with_tt(Position& pos, int depth, PerftTT& tt) noexcept {
    if (depth <= 0) return 1ULL;
    if (depth == 1) return count_legal_moves(pos);

    MoveList move_list;
    generate_legal_moves(pos, move_list);

    // Archive Rule: Avoid TT probing at depth <= 2 (eval/movegen is faster than TT hashing)
    if (depth <= 2) {
        uint64_t nodes = 0;
        StateInfo state;
        for (int i = 0; i < move_list.size(); ++i) {
            make_move(pos, move_list[i], state);
            nodes += count_legal_moves(pos);
            undo_move(pos, move_list[i]);
        }
        return nodes;
    }

    // Depth >= 3: Probe TT
    uint64_t key = pos.state->zobrist_key;
    uint64_t occ = pos.colors[WHITE] ^ (pos.colors[BLACK] << 1);
    uint64_t cached_nodes = 0;
    if (tt.probe(key, occ, depth, cached_nodes)) {
        return cached_nodes;
    }

    uint64_t nodes = 0;
    StateInfo state;
    for (int i = 0; i < move_list.size(); ++i) {
        make_move(pos, move_list[i], state);
        nodes += perft_with_tt(pos, depth - 1, tt);
        undo_move(pos, move_list[i]);
    }

    tt.store(key, occ, depth, nodes);
    return nodes;
}

// ── Work-Array Task for Parallel Work Stealing ────────────────────────────────
struct PerftTask {
    int root_move_idx;
    Position pos;
    StateInfo state;
    int depth;
};

inline void collect_tasks(Position& pos, int current_ply, int target_ply, int remaining_depth,
                          int root_move_idx, std::vector<PerftTask>& tasks) {
    if (current_ply == target_ply || remaining_depth <= 1) {
        PerftTask t;
        t.root_move_idx = root_move_idx;
        t.pos = pos;
        t.state = *pos.state;
        t.state.previous = nullptr;
        t.depth = remaining_depth;
        tasks.push_back(t);
        return;
    }

    MoveList ml;
    generate_legal_moves(pos, ml);
    for (int i = 0; i < ml.size(); ++i) {
        StateInfo st;
        make_move(pos, ml[i], st);
        int next_root_idx = (current_ply == 0) ? i : root_move_idx;
        collect_tasks(pos, current_ply + 1, target_ply, remaining_depth - 1, next_root_idx, tasks);
        undo_move(pos, ml[i]);
    }
}

// ── Comprehensive Perft Result ───────────────────────────────────────────────
struct PerftResult {
    uint64_t total_nodes = 0;
    double elapsed_ms = 0.0;
    double nps = 0.0;
    std::vector<std::pair<std::string, uint64_t>> split_results;
};

// ── Master Parallel Perft Runner ─────────────────────────────────────────────
inline PerftResult run_perft(Position& root_pos, int depth, int threads = 0,
                             bool use_tt = false, bool split = false, size_t tt_size_mb = 256,
                             bool stream = false, int split_depth = 0) {
    PerftResult result;

    if (threads <= 0) {
        unsigned int hw = std::thread::hardware_concurrency();
        threads = (hw > 0) ? static_cast<int>(hw) : 1;
    }

    MoveList root_moves;
    generate_legal_moves(root_pos, root_moves);
    int num_root_moves = root_moves.size();

    if (depth <= 0) {
        result.total_nodes = 1;
        return result;
    }
    if (depth == 1) {
        result.total_nodes = num_root_moves;
        if (split) {
            for (int i = 0; i < num_root_moves; ++i) {
                result.split_results.push_back({root_moves[i].to_uci(), 1});
            }
        }
        return result;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    std::atomic<uint64_t> root_counts[256];
    for (int i = 0; i < 256; ++i) root_counts[i].store(0, std::memory_order_relaxed);

    std::unique_ptr<PerftTT> tt;
    if (use_tt) {
        tt = std::make_unique<PerftTT>(tt_size_mb);
    }

    // Determine target split ply:
    // If user specified split_depth > 0, honor it (bounded by depth - 1).
    // Otherwise, default to:
    // depth <= 2: split 1 ply
    // depth 3-5: split 2 plies
    // depth >= 6: split 3 plies (~8,900 tasks for startpos)
    int target_split_ply = 1;
    if (split_depth > 0) {
        target_split_ply = std::min(split_depth, std::max(1, depth - 1));
    } else if (depth >= 6) {
        target_split_ply = 3;
    } else if (depth >= 3) {
        target_split_ply = 2;
    }

    std::vector<PerftTask> tasks;
    collect_tasks(root_pos, 0, target_split_ply, depth, 0, tasks);

    std::atomic<size_t> next_task{0};
    std::atomic<size_t> completed_tasks{0};
    std::atomic<uint64_t> total_nodes{0};
    size_t num_tasks = tasks.size();

    std::atomic<bool> done{false};
    std::thread monitor_thread;

    if (stream) {
        monitor_thread = std::thread([&]() {
            auto prev_time = std::chrono::high_resolution_clock::now();
            uint64_t prev_nodes = 0;

            while (!done.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (done.load(std::memory_order_relaxed)) break;

                auto cur_time = std::chrono::high_resolution_clock::now();
                double dt = std::chrono::duration<double>(cur_time - prev_time).count();
                double total_elapsed = std::chrono::duration<double>(cur_time - t0).count();

                size_t done_tasks = completed_tasks.load(std::memory_order_relaxed);
                if (done_tasks > num_tasks) done_tasks = num_tasks;

                uint64_t cur_nodes = total_nodes.load(std::memory_order_relaxed);
                uint64_t delta_nodes = (cur_nodes >= prev_nodes) ? (cur_nodes - prev_nodes) : 0;
                double instant_nps = (dt > 0) ? (delta_nodes / dt) : 0.0;
                double overall_nps = (total_elapsed > 0) ? (cur_nodes / total_elapsed) : 0.0;

                double pct = (num_tasks > 0) ? (100.0 * done_tasks / num_tasks) : 0.0;
                double eta_sec = (done_tasks > 0 && done_tasks < num_tasks)
                    ? (total_elapsed / done_tasks * (num_tasks - done_tasks))
                    : 0.0;

                std::cout << "[Stream " << std::setw(5) << std::fixed << std::setprecision(1) << pct << "%] "
                          << "Tasks: " << done_tasks << "/" << num_tasks << " | "
                          << "Nodes: " << cur_nodes << " | "
                          << "Cur: " << std::setprecision(1) << (instant_nps / 1'000'000.0) << "M NPS | "
                          << "Avg: " << std::setprecision(1) << (overall_nps / 1'000'000.0) << "M NPS | "
                          << "Elapsed: " << std::setprecision(1) << total_elapsed << "s | "
                          << "ETA: " << std::setprecision(1) << eta_sec << "s\n" << std::flush;

                prev_time = cur_time;
                prev_nodes = cur_nodes;
            }
        });
    }

    std::vector<std::thread> workers;
    workers.reserve(threads);

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t]() {
#ifdef _WIN32
            DWORD_PTR proc_mask = 0, sys_mask = 0;
            if (GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask) && proc_mask != 0) {
                DWORD_PTR target = (static_cast<DWORD_PTR>(1) << (t % 64));
                if (target & proc_mask) {
                    SetThreadAffinityMask(GetCurrentThread(), target);
                }
            }
#endif
            std::vector<uint64_t> local_root_counts(num_root_moves, 0);

            constexpr size_t MAX_CHUNK = 16;
            while (true) {
                size_t curr = next_task.load(std::memory_order_relaxed);
                if (curr >= num_tasks) break;
                size_t rem = num_tasks - curr;
                size_t chunk = std::clamp(rem / (threads * 4), (size_t)1, MAX_CHUNK);
                size_t idx = next_task.fetch_add(chunk, std::memory_order_relaxed);
                if (idx >= num_tasks) break;
                size_t end = std::min(idx + chunk, num_tasks);

                uint64_t chunk_nodes = 0;
                for (size_t i = idx; i < end; ++i) {
                    Position p = tasks[i].pos;
                    StateInfo st = tasks[i].state;
                    p.state = &st;

                    uint64_t n = 0;
                    if (use_tt && tt) {
                        n = perft_with_tt(p, tasks[i].depth, *tt);
                    } else {
                        n = perft_raw(p, tasks[i].depth);
                    }

                    local_root_counts[tasks[i].root_move_idx] += n;
                    chunk_nodes += n;
                }

                total_nodes.fetch_add(chunk_nodes, std::memory_order_relaxed);
                completed_tasks.fetch_add(end - idx, std::memory_order_relaxed);
            }

            for (int i = 0; i < num_root_moves; ++i) {
                root_counts[i].fetch_add(local_root_counts[i], std::memory_order_relaxed);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    done.store(true, std::memory_order_relaxed);
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.total_nodes = total_nodes.load();
    result.nps = (result.elapsed_ms > 0) ? (result.total_nodes * 1000.0 / result.elapsed_ms) : 0.0;

    if (split) {
        for (int i = 0; i < num_root_moves; ++i) {
            result.split_results.push_back({root_moves[i].to_uci(), root_counts[i].load()});
        }
    }

    return result;
}

// ── Automated Perft Benchmark Suite ──────────────────────────────────────────
inline void run_perft_benchmark_suite() {
    struct TestCase {
        std::string name;
        std::string fen;
        int depth;
        uint64_t expected;
    };

    std::vector<TestCase> cases = {
        {"Startpos Depth 5", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609ULL},
        {"Startpos Depth 6", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6, 119060324ULL},
        {"Kiwipete Depth 4", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603ULL},
        {"Kiwipete Depth 5", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5, 193690690ULL},
        {"Position 3 Depth 4", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238ULL},
        {"Position 4 Depth 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333ULL},
        {"Position 5 Depth 4", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487ULL},
        {"Position 6 Depth 4", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594ULL},
    };

    std::cout << "\n==============================================================================\n";
    std::cout << "                 TIGERFISH 2.0 MULTI-THREADED PERFT SUITE                     \n";
    std::cout << "==============================================================================\n";

    for (const auto& tc : cases) {
        Position pos;
        StateInfo st;
        if (!set_fen(pos, tc.fen, st)) continue;

        std::cout << "\nPosition: " << tc.name << " (Expected: " << tc.expected << ")\n";
        std::cout << "------------------------------------------------------------------------------\n";
        std::cout << "  Threads   Nodes Searched       Time (ms)         Throughput       Status    \n";
        std::cout << "------------------------------------------------------------------------------\n";

        for (int threads : {1, 16, 24}) {
            PerftResult res = run_perft(pos, tc.depth, threads, false, false);
            bool pass = (res.total_nodes == tc.expected);

            std::cout << "    " << std::setw(2) << threads << "    "
                      << std::setw(16) << res.total_nodes << "   "
                      << std::setw(11) << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms   "
                      << std::setw(9) << std::setprecision(2) << (res.nps / 1'000'000.0) << " M NPS   "
                      << (pass ? "  [PASS]" : "  [FAIL]") << "\n";
        }
    }
    std::cout << "\n==============================================================================\n";
}

} // namespace tigerfish
