// bench_search_pos.cpp
#include "../../engine/search.cpp"
#include <chrono>
#include <iostream>

long long node_count = 0;

// Instrumented minimax to count exact nodes
int minimax_counted(Board& board, int depth, int alpha, int beta, bool maximizing) {
    node_count++;
    if (depth == 0) return Engine().evaluate(board);

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    if (moves.size() == 0) {
        if (is_in_check(board)) {
            return maximizing ? (-20000 - depth) : (20000 + depth);
        }
        return 0;
    }

    if (maximizing) {
        int best = -100000;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int score = minimax_counted(board, depth - 1, alpha, beta, false);
            board.unmake_move();
            if (score > best) best = score;
            if (best > alpha) alpha = best;
            if (beta <= alpha) break;
        }
        return best;
    } else {
        int best = 100000;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int score = minimax_counted(board, depth - 1, alpha, beta, true);
            board.unmake_move();
            if (score < best) best = score;
            if (best < beta) beta = score;
            if (beta <= alpha) break;
        }
        return best;
    }
}

int main() {
    init_rays();
    init_magics();

    // Position from screenshot (Black to move)
    // White: K g1, R d1, R d4, Q e2, N b3, P a2,b2,c2,e3,g2,h2
    // Black: K e8, R a8, h8, B f8, N g8, Q b6, P a7,b7,e6,f7,g7,h7
    string fen = "r1b1kb1r/pp3ppp/1q2p3/8/3R4/1N2P3/PPP1Q1PP/3R2K1 b kq - 0 1";

    Board board;
    board.set_fen(fen);
    board.print_board();

    cout << "\nBenchmarking Depth 1 to 6 search on this position...\n";

    for (int d = 1; d <= 6; d++) {
        node_count = 0;
        auto t0 = chrono::high_resolution_clock::now();

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        bool maximizing = (board.side_to_move == WHITE);
        int best_score = maximizing ? -100000 : 100000;
        uint32_t best_m = 0;

        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int score = minimax_counted(board, d - 1, -100000, 100000, !maximizing);
            board.unmake_move();
            if (maximizing ? (score > best_score) : (score < best_score)) {
                best_score = score;
                best_m = moves.move_list[i];
            }
        }

        auto t1 = chrono::high_resolution_clock::now();
        double ms = chrono::duration_cast<chrono::microseconds>(t1 - t0).count() / 1000.0;

        cout << "Depth " << d 
             << " | Time: " << ms << " ms"
             << " | Nodes: " << node_count
             << " | Best move: " << move_to_uci(best_m)
             << " | Score: " << best_score << "\n";
    }

    return 0;
}
