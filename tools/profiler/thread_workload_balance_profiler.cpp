#include "engine/perft.h"
#include <iostream>
#include <vector>
#include <chrono>

using namespace tigerfish;

int main() {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

    Position root;
    StateInfo st;
    set_fen(root, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", st);

    int depth = 8;
    int target_split_ply = 3;
    std::vector<PerftTask> tasks;
    collect_tasks(root, 0, target_split_ply, depth, 0, tasks);

    std::cout << "Total tasks: " << tasks.size() << "\n";

    PerftTT tt(512);

    std::atomic<size_t> next_task{0};
    int threads = 24;
    std::vector<int> task_counts(threads, 0);
    std::vector<double> thread_active_times(threads, 0.0);
    auto start_all = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t]() {
            auto t_start = std::chrono::high_resolution_clock::now();
            int count = 0;
            while (true) {
                size_t idx = next_task.fetch_add(1, std::memory_order_relaxed);
                if (idx >= tasks.size()) break;
                Position p = tasks[idx].pos;
                StateInfo st = tasks[idx].state;
                p.state = &st;
                perft_raw(p, tasks[idx].depth);
                count++;
            }
            auto t_end = std::chrono::high_resolution_clock::now();
            task_counts[t] = count;
            thread_active_times[t] = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        });
    }

    for (auto& w : workers) w.join();
    auto end_all = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end_all - start_all).count();

    std::cout << "Total time: " << total_ms << " ms\n";
    for (int t = 0; t < threads; ++t) {
        std::cout << "Thread " << t << ": " << task_counts[t] << " tasks, active " << thread_active_times[t] << " ms (" << (thread_active_times[t]*100.0/total_ms) << "%)\n";
    }
    return 0;
}
