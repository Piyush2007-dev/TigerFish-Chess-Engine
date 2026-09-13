#include "engine/types.cpp"
#include "engine/bitboards.cpp"
#include "engine/zobrist.cpp"
#include "engine/position.cpp"
#include "engine/movegen.cpp"
#include "engine/tt.cpp"
#include "engine/eval.cpp"
#include "engine/search.cpp"

#include <iostream>
#include <chrono>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>

using namespace tigerfish;

struct WorkerTask {
    Position* root_pos = nullptr;
    MoveList* root_moves = nullptr;
    int depth = 0;
    int alpha = 0;
    int beta = 0;
    std::atomic<int> next_move_idx{1};
    std::atomic<int> active_workers{0};
    std::atomic<int> best_score{-SCORE_INFINITY};
    std::atomic<uint16_t> best_move_data{0};
    std::mutex res_mutex;
    bool stop = false;
    bool new_task = false;
    std::condition_variable cv_start;
    std::condition_variable cv_done;
    std::mutex task_mutex;
};

SearchResult test_parallel_search(Position& pos, int target_depth, int num_threads, TranspositionTable& tt) {
    SearchResult result;
    SearchContext main_ctx;
    main_ctx.reset();
    main_ctx.start_time = std::chrono::steady_clock::now();

    tt.current_age++;

    Move best_move = MOVE_NONE;
    int best_score = 0;
    uint64_t total_nodes = 0;

    if (num_threads <= 1) {
        SearchLimits lim;
        lim.depth = target_depth;
        lim.thread_count = 1;
        return search_position(pos, lim, tt);
    }

    WorkerTask task;
    task.root_pos = &pos;
    std::vector<std::thread> workers;
    std::vector<SearchContext> worker_contexts(num_threads - 1);

    for (int t = 0; t < num_threads - 1; ++t) {
        workers.emplace_back([&, t]() {
            SearchContext& wctx = worker_contexts[t];
            Position wpos = pos;
            StateInfo wstate = *pos.state;
            wpos.state = &wstate;

            while (true) {
                std::unique_lock<std::mutex> lock(task.task_mutex);
                task.cv_start.wait(lock, [&]() { return task.new_task || task.stop; });
                if (task.stop) break;

                int depth = task.depth;
                MoveList& rmoves = *task.root_moves;
                lock.unlock();

                while (true) {
                    int idx = task.next_move_idx.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= rmoves.size() || task.stop) break;

                    Move m = rmoves[idx];
                    StateInfo m_state;
                    make_move(wpos, m, m_state);

                    int cur_alpha = task.alpha;
                    int score = -alpha_beta(wpos, -cur_alpha - 1, -cur_alpha, depth - 1, 1, wctx, tt);

                    if (score > cur_alpha) {
                        score = -alpha_beta(wpos, -SCORE_INFINITY, -cur_alpha, depth - 1, 1, wctx, tt);
                        if (score > cur_alpha) {
                            std::lock_guard<std::mutex> rlock(task.res_mutex);
                            if (score > task.best_score.load(std::memory_order_relaxed)) {
                                task.best_score.store(score, std::memory_order_relaxed);
                                task.best_move_data.store(m.raw_data, std::memory_order_relaxed);
                                task.alpha = score;
                            }
                        }
                    }
                    undo_move(wpos, m);
                }

                if (task.active_workers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    task.cv_done.notify_one();
                }
            }
        });
    }

    // Main thread iterative deepening loop
    for (int depth = 1; depth <= target_depth; ++depth) {
        if (depth < 4) {
            int score = alpha_beta(pos, -SCORE_INFINITY, SCORE_INFINITY, depth, 0, main_ctx, tt);
            best_score = score;
            if (main_ctx.pv_length[0] > 0) best_move = main_ctx.pv_table[0][0];
            continue;
        }

        MoveList root_moves;
        generate_legal_moves(pos, root_moves);
        if (root_moves.empty()) break;

        // Sort root moves so best move is first
        int move_scores[256];
        for (int i = 0; i < root_moves.size(); ++i) {
            if (root_moves[i] == best_move) move_scores[i] = 2000000;
            else move_scores[i] = score_move(pos, root_moves[i], MOVE_NONE, 0, main_ctx);
        }
        for (int i = 0; i < root_moves.size(); ++i) {
            pick_next_best_move(root_moves, move_scores, i);
        }

        // 1. Search PV move first on main thread
        StateInfo state0;
        make_move(pos, root_moves[0], state0);
        int alpha = -alpha_beta(pos, -SCORE_INFINITY, SCORE_INFINITY, depth - 1, 1, main_ctx, tt);
        undo_move(pos, root_moves[0]);

        best_score = alpha;
        best_move = root_moves[0];

        // 2. Launch helper workers for remaining root moves
        {
            std::unique_lock<std::mutex> lock(task.task_mutex);
            task.depth = depth;
            task.alpha = alpha;
            task.beta = SCORE_INFINITY;
            task.root_moves = &root_moves;
            task.next_move_idx.store(1, std::memory_order_relaxed);
            task.best_score.store(best_score, std::memory_order_relaxed);
            task.best_move_data.store(best_move.raw_data, std::memory_order_relaxed);
            task.active_workers.store(num_threads - 1, std::memory_order_relaxed);
            task.new_task = true;
            task.cv_start.notify_all();
        }

        // Main thread also helps process remaining root moves!
        while (true) {
            int idx = task.next_move_idx.fetch_add(1, std::memory_order_relaxed);
            if (idx >= root_moves.size()) break;

            Move m = root_moves[idx];
            StateInfo m_state;
            make_move(pos, m, m_state);

            int cur_alpha = task.alpha;
            int score = -alpha_beta(pos, -cur_alpha - 1, -cur_alpha, depth - 1, 1, main_ctx, tt);

            if (score > cur_alpha) {
                score = -alpha_beta(pos, -SCORE_INFINITY, -cur_alpha, depth - 1, 1, main_ctx, tt);
                if (score > cur_alpha) {
                    std::lock_guard<std::mutex> rlock(task.res_mutex);
                    if (score > task.best_score.load(std::memory_order_relaxed)) {
                        task.best_score.store(score, std::memory_order_relaxed);
                        task.best_move_data.store(m.raw_data, std::memory_order_relaxed);
                        task.alpha = score;
                    }
                }
            }
            undo_move(pos, m);
        }

        // Wait for all workers to finish this depth
        {
            std::unique_lock<std::mutex> lock(task.task_mutex);
            task.cv_done.wait(lock, [&]() { return task.active_workers.load(std::memory_order_acquire) == 0; });
            task.new_task = false;
        }

        best_score = task.best_score.load(std::memory_order_relaxed);
        best_move = Move(task.best_move_data.load(std::memory_order_relaxed));
    }

    // Stop workers
    {
        std::unique_lock<std::mutex> lock(task.task_mutex);
        task.stop = true;
        task.cv_start.notify_all();
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    total_nodes = main_ctx.nodes;
    for (const auto& wctx : worker_contexts) total_nodes += wctx.nodes;

    auto t1 = std::chrono::steady_clock::now();
    result.nodes = total_nodes;
    result.best_move = best_move;
    result.score = best_score;
    result.depth = target_depth;
    result.time_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - main_ctx.start_time).count());
    result.nps = (result.time_ms > 0) ? (result.nodes * 1000 / result.time_ms) : result.nodes;
    return result;
}

int main() {
    init_bitboards();
    init_zobrist();
    init_pst_tables();
    init_king_shields();
    init_lmr_table();

    Position pos;
    StateInfo state;
    set_fen(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", state);

    TranspositionTable tt;
    init_tt(tt, 64);

    for (int threads : {1, 2, 4}) {
        clear_tt(tt);
        auto t0 = std::chrono::high_resolution_clock::now();
        SearchResult res = test_parallel_search(pos, 15, threads, tt);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "Threads " << threads << ": Depth " << res.depth
                  << " move " << res.best_move.to_uci()
                  << " score " << res.score
                  << " nodes " << res.nodes
                  << " time " << ms << " ms (" << res.nps << " NPS)\n";
    }

    return 0;
}
