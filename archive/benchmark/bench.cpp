// ══════════════════════════════════════════════════════════════
// bench.cpp — TigerFish Engine Speed Benchmark
//
// Plays the engine against itself at configurable depth and
// reports detailed timing stats per move and overall NPS.
//
// Compile:
//   g++ -O2 -std=c++20 -o bench.exe bench.cpp
//
// Run:
//   bench.exe           (depth 5, max 60 moves)
//   bench.exe 4         (depth 4)
//   bench.exe 5 80      (depth 5, max 80 half-moves)
// ══════════════════════════════════════════════════════════════

#include "chess.cpp"
#include <chrono>
#include <iomanip>

using namespace std;
using namespace chrono;

// ── Node-counting engine (standalone, no inheritance) ────────
struct BenchEngine {
    uint64_t nodes = 0;

    // PSTs and piece values copied from Engine
    static constexpr int PIECE_VALUE[12] = {
        100, 320, 330, 500, 900, 20000,   // P N B R Q K
        100, 320, 330, 500, 900, 20000    // p n b r q k
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

    static int mirror(int sq) {
        return (7 - sq / 8) * 8 + (sq % 8);
    }

    int evaluate(Board& board) {
        int score = 0;
        for (int sq = 0; sq < 64; sq++) {
            Piece p = board.piece_on[sq];
            if (p == (Piece)0xF) continue;
            bool is_white = (p < 6);
            int tsq = is_white ? sq : mirror(sq);
            int base = PIECE_VALUE[p];
            int pst  = 0;
            switch (p) {
                case Piece::P: case Piece::p: pst = PAWN_PST[tsq];   break;
                case Piece::N: case Piece::n: pst = KNIGHT_PST[tsq]; break;
                case Piece::B: case Piece::b: pst = BISHOP_PST[tsq]; break;
                case Piece::R: case Piece::r: pst = ROOK_PST[tsq];   break;
                case Piece::Q: case Piece::q: pst = QUEEN_PST[tsq];  break;
                case Piece::K: case Piece::k: pst = KING_PST[tsq];   break;
                default: break;
            }
            if (is_white) score += base + pst;
            else          score -= base + pst;
        }
        return score;
    }

    int minimax(Board& board, int depth, int alpha, int beta, bool maximizing) {
        nodes++;
        if (depth == 0) return evaluate(board);

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        if (moves.size() == 0)
            return board.is_in_check() ?
                (maximizing ? (-20000 - depth) : (20000 + depth)) : 0;

        if (maximizing) {
            int best = -100000;
            for (int i = 0; i < moves.size(); i++) {
                MoveList dummy;
                board.make_move(moves.move_list[i], dummy);
                int score = minimax(board, depth - 1, alpha, beta, false);
                board.unmake_move();
                if (score > best) best = score;
                if (best > alpha) alpha = best;
                if (beta <= alpha) break;
            }
            return best;
        } else {
            int best = 100000;
            for (int i = 0; i < moves.size(); i++) {
                MoveList dummy;
                board.make_move(moves.move_list[i], dummy);
                int score = minimax(board, depth - 1, alpha, beta, true);
                board.unmake_move();
                if (score < best) best = score;
                if (best < beta) beta = best;
                if (beta <= alpha) break;
            }
            return best;
        }
    }

    Move best_move(Board& board, int depth) {
        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        bool maximizing = (board.side_to_move == WHITE);
        int  best_score = maximizing ? -100000 : 100000;
        Move best; best.value = 0;

        for (int i = 0; i < moves.size(); i++) {
            MoveList dummy;
            board.make_move(moves.move_list[i], dummy);
            int score = minimax(board, depth - 1, -100000, 100000, !maximizing);
            board.unmake_move();
            if (maximizing ? (score > best_score) : (score < best_score)) {
                best_score = score;
                best = moves.move_list[i];
            }
        }
        return best;
    }

    void reset_nodes() { nodes = 0; }
};


// ── Pretty print helpers ──────────────────────────────────────
void print_separator(char c = '-', int w = 68) {
    for (int i = 0; i < w; i++) cout << c;
    cout << "\n";
}

string format_duration(double ms) {
    if (ms < 1000) return to_string((int)ms) + " ms";
    return to_string(ms / 1000.0).substr(0, 5) + " s ";
}

string format_nodes(uint64_t n) {
    if (n < 1000)       return to_string(n);
    if (n < 1000000)    return to_string(n / 1000) + "K";
    if (n < 1000000000) return to_string(n / 1000000) + "M";
    return to_string(n / 1000000000) + "B";
}

// ── Main benchmark ────────────────────────────────────────────
int main(int argc, char* argv[]) {
    init_rays();
    init_magics();
    int depth    = (argc >= 2) ? atoi(argv[1]) : 5;
    int max_half = (argc >= 3) ? atoi(argv[2]) : 60;  // half-moves (plies)

    const string START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Board board;
    board.set_fen(START_FEN);

    BenchEngine engine;

    // ── Header ────────────────────────────────────────────────
    cout << "\n";
    print_separator('=');
    cout << "  TigerFish Engine — Self-Play Speed Benchmark\n";
    print_separator('=');
    cout << "  Depth     : " << depth << "\n";
    cout << "  Max plies : " << max_half << "\n";
    cout << "  Start FEN : " << START_FEN << "\n";
    print_separator();
    cout << left
         << setw(5)  << "Ply"
         << setw(8)  << "Move"
         << setw(12) << "Nodes"
         << setw(10) << "Time"
         << setw(12) << "NPS"
         << setw(8)  << "Score"
         << "\n";
    print_separator();

    // ── Game loop ─────────────────────────────────────────────
    uint64_t total_nodes = 0;
    double   total_ms    = 0.0;
    int      ply         = 0;
    vector<string> move_list;

    while (ply < max_half) {
        GameResult result = board.get_game_result();
        if (result != GAME_ONGOING) break;

        engine.reset_nodes();
        auto t0 = high_resolution_clock::now();

        Move best = engine.best_move(board, depth);

        auto t1  = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

        if (best.value == 0) break;  // no legal move

        string uci = move_to_uci(best);
        move_list.push_back(uci);

        uint64_t n = engine.nodes;
        double nps = (ms > 0) ? (n / (ms / 1000.0)) : 0;
        total_nodes += n;
        total_ms    += ms;

        // Evaluate position for score display
        int score = engine.evaluate(board);
        string score_str = (score >= 0 ? "+" : "") + to_string(score);

        cout << left
             << setw(5)  << (ply + 1)
             << setw(8)  << uci
             << setw(12) << format_nodes(n)
             << setw(10) << format_duration(ms)
             << setw(12) << (format_nodes((uint64_t)nps) + "/s")
             << setw(8)  << score_str
             << "\n";

        // Apply the best move using a fresh generate for this position
        {
            MoveList apply_moves;
            MoveGenerator apply_mg;
            apply_mg.generate_moves(board, apply_moves);
            for (int i = 0; i < apply_moves.size(); i++) {
                if (move_to_uci(apply_moves.move_list[i]) == uci) {
                    board.make_move(apply_moves.move_list[i], apply_moves);
                    break;
                }
            }
        }

        ply++;
    }

    // ── Summary ───────────────────────────────────────────────
    print_separator();

    GameResult final_result = board.get_game_result();
    string result_str = "Game in progress";
    if      (final_result == GAME_CHECKMATE)              result_str = board.side_to_move == WHITE ? "Black wins (Checkmate)" : "White wins (Checkmate)";
    else if (final_result == GAME_STALEMATE)              result_str = "Draw (Stalemate)";
    else if (final_result == GAME_FIFTY_MOVE_DRAW)        result_str = "Draw (50-move rule)";
    else if (final_result == GAME_SEVENTY_FIVE_MOVE_DRAW) result_str = "Draw (75-move rule)";
    else if (final_result == GAME_INSUFFICIENT_MATERIAL)  result_str = "Draw (Insufficient material)";
    else if (ply >= max_half)                             result_str = "Stopped (max plies reached)";

    double avg_ms    = (ply > 0) ? total_ms / ply : 0;
    double avg_nodes = (ply > 0) ? (double)total_nodes / ply : 0;
    double total_nps = (total_ms > 0) ? total_nodes / (total_ms / 1000.0) : 0;

    cout << "\n  RESULT      : " << result_str << "\n";
    cout << "  Total plies : " << ply << "\n";
    cout << "  Total time  : " << fixed << setprecision(2) << total_ms / 1000.0 << " s\n";
    cout << "  Total nodes : " << format_nodes(total_nodes) << " (" << total_nodes << ")\n";
    cout << "  Avg time/ply: " << fixed << setprecision(1) << avg_ms << " ms\n";
    cout << "  Avg nodes   : " << format_nodes((uint64_t)avg_nodes) << " per move\n";
    cout << "  Overall NPS : " << format_nodes((uint64_t)total_nps) << " nodes/sec\n";

    // Print move list
    cout << "\n  Moves played:\n  ";
    for (int i = 0; i < (int)move_list.size(); i++) {
        if (i % 2 == 0) cout << (i/2 + 1) << ". ";
        cout << move_list[i] << " ";
        if (i % 10 == 9) cout << "\n  ";
    }
    cout << "\n";
    print_separator('=');
    cout << "\n";

    return 0;
}
