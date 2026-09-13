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

using namespace tigerfish;

// Prototype of Root-Parallel search
SearchResult search_root_parallel(Position& pos, int target_depth, int num_threads, TranspositionTable& tt) {
    SearchResult result;
    SearchContext main_ctx;
    main_ctx.reset();
    main_ctx.start_time = std::chrono::steady_clock::now();

    tt.current_age++;

    Move best_move = MOVE_NONE;
    int best_score = 0;
    uint64_t total_nodes = 0;

    // Iterative deepening up to target_depth
    for (int depth = 1; depth <= target_depth; ++depth) {
        if (depth < 4 || num_threads <= 1) {
            // Low depth: run single-threaded
            int score = alpha_beta(pos, -SCORE_INFINITY, SCORE_INFINITY, depth, 0, main_ctx, tt);
            best_score = score;
            if (main_ctx.pv_length[0] > 0) best_move = main_ctx.pv_table[0][0];
            total_nodes += main_ctx.nodes;
            main_ctx.nodes = 0;
            continue;
        }

        // Depth >= 4: Root Move Parallelism
        MoveList root_moves;
        generate_legal_moves(pos, root_moves);
        if (root_moves.empty()) break;

        // Sort root moves so best_move is first
        int move_scores[256];
        for (int i = 0; i < root_moves.size(); ++i) {
            if (root_moves[i] == best_move) move_scores[i] = 2000000;
            else move_scores[i] = score_move(pos, root_moves[i], MOVE_NONE, 0, main_ctx);
        }
        for (int i = 0; i < root_moves.size(); ++i) {
            pick_next_best_move(root_moves, move_scores, i);
        }

        // 1. Search first move (PV move) to establish alpha
        StateInfo state0;
        make_move(pos, root_moves[0], state0);
        int alpha = -alpha_beta(pos, -SCORE_INFINITY, SCORE_INFINITY, depth - 1, 1, main_ctx, tt);
        undo_move(pos, root_moves[0]);

        best_score = alpha;
        best_move = root_moves[0];

        // 2. Search remaining moves in parallel across num_threads
        std::atomic<int> next_idx{1};
        std::mutex root_mutex;
        std::atomic<bool> stop_flag{false};

        std::vector<std::thread> workers;
        std::vector<SearchContext> worker_contexts(num_threads);

        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back([&, t]() {
                SearchContext& wctx = worker_contexts[t];
                wctx.reset();
                wctx.start_time = main_ctx.start_time;

                Position wpos = pos;
                StateInfo wstate = *pos.state;
                wpos.state = &wstate;

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    int idx = next_idx.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= root_moves.size()) break;

                    Move m = root_moves[idx];
                    StateInfo m_state;
                    make_move(wpos, m, m_state);

                    // Zero-window search
                    int cur_alpha = alpha;
                    int score = -alpha_beta(wpos, -cur_alpha - 1, -cur_alpha, depth - 1, 1, wctx, tt);

                    if (score > cur_alpha) {
                        // Re-search with full window
                        score = -alpha_beta(wpos, -SCORE_INFINITY, -cur_alpha, depth - 1, 1, wctx, tt);
                        if (score > cur_alpha) {
                            std::lock_guard<std::mutex> lock(root_mutex);
                            if (score > best_score) {
                                best_score = score;
                                best_move = m;
                                alpha = score;
                            }
                        }
                    }
                    undo_move(wpos, m);
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        for (const auto& wctx : worker_contexts) {
            total_nodes += wctx.nodes;
        }
        total_nodes += main_ctx.nodes;
        main_ctx.nodes = 0;
    }

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
        SearchResult res = search_root_parallel(pos, 15, threads, tt);
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
