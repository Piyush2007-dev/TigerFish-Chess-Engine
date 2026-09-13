// ══════════════════════════════════════════════════════════════
// benchv2.cpp — TigerFish Parallel Speed Benchmark (24 Threads)
//
// Strategy: Root-level parallelism with a work-stealing queue.
//
//   - All legal root moves are put into a shared atomic queue
//   - 24 worker threads continuously pull from the queue
//   - Each thread evaluates its move on a private Board copy
//   - No thread ever sits idle — as soon as one move finishes,
//     the thread immediately grabs the next available root move
//   - Results are collected with a mutex and best move selected
//
// No shared Board state → no race conditions → no locks in search
//
// Compile:
//   g++ -O2 -std=c++20 -pthread -o benchv2.exe benchv2.cpp
//
// Run:
//   benchv2.exe              (depth 5, 30 plies, 24 threads)
//   benchv2.exe 6            (depth 6)
//   benchv2.exe 6 20 16      (depth 6, 20 plies, 16 threads)
//   benchv2.exe 5 30 24 1    (depth 5, 30 plies fallback, 24 threads, run until win/draw)
// ══════════════════════════════════════════════════════════════

#include "chess.cpp"
#include <chrono>  
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <iomanip>
#include <algorithm>

using namespace std;
using namespace chrono;

// ── Evaluation + Minimax (same as bench.cpp, thread-local) ───
// Each thread instantiates its own ParallelEngine — no sharing.

struct ParallelEngine {
    uint64_t nodes = 0;

    static constexpr int PIECE_VALUE[12] = {
        100, 320, 330, 500, 900, 20000,
        100, 320, 330, 500, 900, 20000
    };
    static constexpr int PAWN_PST[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
         5, 10, 10,-20,-20, 10, 10,  5,
         5, -5,-10,  0,  0,-10, -5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5,  5, 10, 25, 25, 10,  5,  5,
        10, 10, 20, 30, 30, 20, 10, 10,
        50, 50, 50, 50, 50, 50, 50, 50,
         0,  0,  0,  0,  0,  0,  0,  0,
    };
    static constexpr int KNIGHT_PST[64] = {
       -50,-40,-30,-30,-30,-30,-40,-50,
       -40,-20,  0,  5,  5,  0,-20,-40,
       -30,  5, 10, 15, 15, 10,  5,-30,
       -30,  0, 15, 20, 20, 15,  0,-30,
       -30,  5, 15, 20, 20, 15,  5,-30,
       -30,  0, 10, 15, 15, 10,  0,-30,
       -40,-20,  0,  0,  0,  0,-20,-40,
       -50,-40,-30,-30,-30,-30,-40,-50,
    };
    static constexpr int BISHOP_PST[64] = {
       -20,-10,-10,-10,-10,-10,-10,-20,
       -10,  5,  0,  0,  0,  0,  5,-10,
       -10, 10, 10, 10, 10, 10, 10,-10,
       -10,  0, 10, 10, 10, 10,  0,-10,
       -10,  5,  5, 10, 10,  5,  5,-10,
       -10,  0,  5, 10, 10,  5,  0,-10,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -20,-10,-10,-10,-10,-10,-10,-20,
    };
    static constexpr int ROOK_PST[64] = {
         0,  0,  0,  5,  5,  0,  0,  0,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
         5, 10, 10, 10, 10, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0,
    };
    static constexpr int QUEEN_PST[64] = {
       -20,-10,-10, -5, -5,-10,-10,-20,
       -10,  0,  5,  0,  0,  0,  0,-10,
       -10,  5,  5,  5,  5,  5,  0,-10,
         0,  0,  5,  5,  5,  5,  0, -5,
        -5,  0,  5,  5,  5,  5,  0, -5,
       -10,  0,  5,  5,  5,  5,  0,-10,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -20,-10,-10, -5, -5,-10,-10,-20,
    };
    static constexpr int KING_PST[64] = {
        20, 30, 10,  0,  0, 10, 30, 20,
        20, 20,  0,  0,  0,  0, 20, 20,
       -10,-20,-20,-20,-20,-20,-20,-10,
       -20,-30,-30,-40,-40,-30,-30,-20,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
    };

    static int mirror(int sq) { return (7 - sq/8)*8 + sq%8; }

    int evaluate(Board& b) {
        int score = 0;
        for (int sq = 0; sq < 64; sq++) {
            Piece p = b.piece_on[sq];
            if (p == (Piece)0xF) continue;
            bool w = p < 6;
            int t = w ? sq : mirror(sq);
            int base = PIECE_VALUE[p], pst = 0;
            switch (p) {
                case Piece::P: case Piece::p: pst = PAWN_PST[t];   break;
                case Piece::N: case Piece::n: pst = KNIGHT_PST[t]; break;
                case Piece::B: case Piece::b: pst = BISHOP_PST[t]; break;
                case Piece::R: case Piece::r: pst = ROOK_PST[t];   break;
                case Piece::Q: case Piece::q: pst = QUEEN_PST[t];  break;
                case Piece::K: case Piece::k: pst = KING_PST[t];   break;
                default: break;
            }
            if (w) score += base + pst;
            else   score -= base + pst;
        }
        return score;
    }

    int minimax(Board& b, int depth, int alpha, int beta, bool maxing) {
        nodes++;
        if (depth == 0) return evaluate(b);

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(b, moves);

        if (moves.size() == 0)
            return b.is_in_check() ?
                (maxing ? (-20000 - depth) : (20000 + depth)) : 0;

        if (maxing) {
            int best = -100000;
            for (int i = 0; i < moves.size(); i++) {
                MoveList dummy;
                b.make_move(moves.move_list[i], dummy);
                best = max(best, minimax(b, depth-1, alpha, beta, false));
                b.unmake_move();
                alpha = max(alpha, best);
                if (beta <= alpha) break;
            }
            return best;
        } else {
            int best = 100000;
            for (int i = 0; i < moves.size(); i++) {
                MoveList dummy;
                b.make_move(moves.move_list[i], dummy);
                best = min(best, minimax(b, depth-1, alpha, beta, true));
                b.unmake_move();
                beta = min(beta, best);
                if (beta <= alpha) break;
            }
            return best;
        }
    }
};

// ── Work-stealing parallel root search ───────────────────────
//
// Shared across all threads (atomic → lock-free grab):
//   next_idx  : next root move to evaluate (threads race to claim)
//   total_nodes: accumulates node counts from all threads
//
// Each thread loop:
//   1. Atomically grab next_idx (fetch_add) → my_move_idx
//   2. If idx >= num_moves → all work done, exit
//   3. Copy the board, make the move, run minimax
//   4. Lock result mutex, update best move if better
//   5. Add own node count to shared total
//   6. Go back to step 1 immediately (never idle)

struct ParallelResult {
    int      score;
    Move     move;
    uint64_t nodes;
};

Move parallel_best_move(
    Board& board,
    int depth,
    int num_threads,
    uint64_t& out_nodes)
{
    // Generate root moves once on the main thread
    MoveList root_moves;
    MoveGenerator mg;
    mg.generate_moves(board, root_moves);
    int num_moves = root_moves.size();

    if (num_moves == 0) { out_nodes = 0; Move m; m.value = 0; return m; }

    bool maximizing = (board.side_to_move == WHITE);

    // Shared state
    atomic<int>      next_idx{0};           // work queue pointer
    atomic<uint64_t> total_nodes{0};        // sum of all threads' nodes
    mutex            result_mtx;
    int              best_score = maximizing ? -100001 : 100001;
    Move             best_move; best_move.value = 0;

    // Worker lambda — each thread runs this loop until queue empty
    auto worker = [&]() {
        ParallelEngine engine;

        while (true) {
            // Grab next root move (atomic, lock-free)
            int idx = next_idx.fetch_add(1, memory_order_relaxed);
            if (idx >= num_moves) break;  // queue exhausted → exit

            Move root_move = root_moves.move_list[idx];

            // Private board copy — NO sharing with other threads
            Board local = board;
            MoveList dummy;
            local.make_move(root_move, dummy);

            int score = engine.minimax(local, depth - 1,
                                       -100000, 100000,
                                       !maximizing);
            local.unmake_move();

            // Update global best (mutex only for result, not search)
            {
                lock_guard<mutex> lock(result_mtx);
                if (maximizing ? (score > best_score) : (score < best_score)) {
                    best_score = score;
                    best_move  = root_move;
                }
            }
        }

        // Accumulate this thread's node count
        total_nodes.fetch_add(engine.nodes, memory_order_relaxed);
    };

    // Launch threads
    vector<thread> threads;
    int actual_threads = min(num_threads, num_moves); // don't over-spawn
    threads.reserve(actual_threads);
    for (int i = 0; i < actual_threads; i++)
        threads.emplace_back(worker);

    // Wait for all threads to finish
    for (auto& t : threads) t.join();

    out_nodes = total_nodes.load();
    return best_move;
}

// ── Helpers ───────────────────────────────────────────────────
string fmt_nodes(uint64_t n) {
    if (n < 1000000)    return to_string(n / 1000) + "K";
    if (n < 1000000000) return to_string(n / 1000000) + "M";
    return to_string(n / 1000000000) + "B";
}
string fmt_ms(double ms) {
    if (ms < 1000) return to_string((int)ms) + "ms";
    return to_string(ms / 1000.0).substr(0, 5) + "s ";
}
void sep(char c = '-', int w = 72) {
    for (int i = 0; i < w; i++) cout << c;
    cout << "\n";
}

// ── Main ──────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    init_rays();
    init_magics();
    int depth      = (argc >= 2) ? atoi(argv[1]) : 5;
    int max_plies  = (argc >= 3) ? atoi(argv[2]) : 30;
    int num_threads= (argc >= 4) ? atoi(argv[3]) : 24;
    bool until_end = (argc >= 5) ? (atoi(argv[4]) != 0) : false;

    const string START_FEN =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Board board;
    board.set_fen(START_FEN);

    // Header
    cout << "\n";
    sep('=');
    cout << "  TigerFish Parallel Benchmark  |  "
         << num_threads << " threads  |  depth " << depth
         << "  |  plies: " << (until_end ? "until win/draw" : to_string(max_plies)) << "\n";
    sep('=');
    cout << "  Start: " << START_FEN << "\n";
    sep();
    cout << left
         << setw(5)  << "Ply"
         << setw(8)  << "Move"
         << setw(12) << "Nodes"
         << setw(10) << "Time"
         << setw(12) << "NPS"
         << setw(10) << "Speedup"
         << setw(8)  << "Score"
         << "\n";
    sep();

    uint64_t total_nodes = 0;
    double   total_ms    = 0;
    int      ply         = 0;
    vector<string> move_list;

    // Single-thread reference NPS (set after first move)
    double ref_nps = 0;

    while (until_end || (ply < max_plies)) {
        GameResult result = board.get_game_result();
        if (result != GAME_ONGOING) break;

        auto t0 = high_resolution_clock::now();

        uint64_t nodes = 0;
        Move best = parallel_best_move(board, depth, num_threads, nodes);

        auto t1 = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

        if (best.value == 0) break;

        string uci = move_to_uci(best);
        move_list.push_back(uci);
        total_nodes += nodes;
        total_ms    += ms;

        double nps     = (ms > 0) ? nodes / (ms / 1000.0) : 0;
        if (ply == 0) ref_nps = nps;  // baseline from move 1
        double speedup = (ref_nps > 0) ? nps / (ref_nps / num_threads) : 0;

        ParallelEngine pe;
        int score = pe.evaluate(board);
        string score_str = (score >= 0 ? "+" : "") + to_string(score);

        cout << left
             << setw(5)  << (ply + 1)
             << setw(8)  << uci
             << setw(12) << fmt_nodes(nodes)
             << setw(10) << fmt_ms(ms)
             << setw(12) << (fmt_nodes((uint64_t)nps) + "/s")
             << setw(10) << (to_string((int)speedup) + "x")
             << setw(8)  << score_str
             << "\n";

        // Apply move to main board
        {
            MoveList apply_moves;
            MoveGenerator apply_mg;
            apply_mg.generate_moves(board, apply_moves);
            for (int i = 0; i < apply_moves.size(); i++) {
                if (move_to_uci(apply_moves.move_list[i]) == uci) {
                    MoveList dummy;
                    board.make_move(apply_moves.move_list[i], dummy);
                    break;
                }
            }
        }
        ply++;
    }

    // Summary
    sep();
    GameResult final = board.get_game_result();
    string res_str   = "Game in progress";
    if      (final == GAME_CHECKMATE)              res_str = board.side_to_move == WHITE ? "Black wins" : "White wins";
    else if (final == GAME_STALEMATE)              res_str = "Draw (Stalemate)";
    else if (final == GAME_FIFTY_MOVE_DRAW)        res_str = "Draw (50-move)";
    else if (final == GAME_SEVENTY_FIVE_MOVE_DRAW) res_str = "Draw (75-move)";
    else if (final == GAME_INSUFFICIENT_MATERIAL)  res_str = "Draw (Material)";
    else if (ply >= max_plies && !until_end)       res_str = "Stopped (max plies)";

    double avg_ms  = ply > 0 ? total_ms / ply : 0;
    double tot_nps = total_ms > 0 ? total_nodes / (total_ms / 1000.0) : 0;
    double single_est = tot_nps / num_threads;  // estimated single-thread NPS

    cout << "\n";
    cout << "  Result       : " << res_str                   << "\n";
    cout << "  Plies        : " << ply                       << "\n";
    cout << "  Threads      : " << num_threads               << "\n";
    cout << "  Depth        : " << depth                     << "\n";
    cout << "  Total time   : " << fixed << setprecision(2) << total_ms/1000.0 << " s\n";
    cout << "  Total nodes  : " << fmt_nodes(total_nodes) << " (" << total_nodes << ")\n";
    cout << "  Avg time/ply : " << fixed << setprecision(1) << avg_ms << " ms\n";
    cout << "  Parallel NPS : " << fmt_nodes((uint64_t)tot_nps) << " nodes/sec\n";
    cout << "  ~1-thread NPS: " << fmt_nodes((uint64_t)single_est) << " nodes/sec (est.)\n";
    cout << "  ~Speedup     : " << fixed << setprecision(1) << (tot_nps / single_est) * num_threads << "x theoretical\n";

    cout << "\n  Moves:\n  ";
    for (int i = 0; i < (int)move_list.size(); i++) {
        if (i % 2 == 0) cout << (i/2 + 1) << ". ";
        cout << move_list[i] << " ";
        if (i % 10 == 9) cout << "\n  ";
    }
    cout << "\n";
    sep('=');
    cout << "\n";
    return 0;
}
