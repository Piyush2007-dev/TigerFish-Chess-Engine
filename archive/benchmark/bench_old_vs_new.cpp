#include "../../engine/search.cpp"
#include <iostream>
#include <chrono>
#include <vector>

using namespace std;
using namespace std::chrono;

// Old search without TT
uint64_t old_nodes = 0;
int minimax_old(Engine& engine, Board& board, int depth, int alpha, int beta, bool maximizing) {
    old_nodes++;
    if (depth == 0) return engine.evaluate(board);

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
        int max_eval = -100000;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int eval = minimax_old(engine, board, depth - 1, alpha, beta, false);
            board.unmake_move();
            max_eval = max(max_eval, eval);
            alpha = max(alpha, max_eval);
            if (beta <= alpha) break;
        }
        return max_eval;
    } else {
        int min_eval = 100000;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int eval = minimax_old(engine, board, depth - 1, alpha, beta, true);
            board.unmake_move();
            min_eval = min(min_eval, eval);
            beta = min(beta, min_eval);
            if (beta <= alpha) break;
        }
        return min_eval;
    }
}

// New search with TT
uint64_t new_nodes = 0;
int minimax_new(Engine& engine, Board& board, int depth, int alpha, int beta, bool maximizing) {
    new_nodes++;
    if (depth == 0) return engine.evaluate(board);

    int alpha_orig = alpha;
    int beta_orig = beta;

    uint32_t tt_move = 0;
    int tt_score = 0;
    if (engine.tt.lookup(board.zobrist_hash, depth, alpha, beta, tt_score, tt_move)) {
        return tt_score;
    }

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    if (moves.size() == 0) {
        if (is_in_check(board)) {
            return maximizing ? (-20000 - depth) : (20000 + depth);
        }
        return 0;
    }

    if (tt_move != 0) {
        for (int i = 0; i < moves.size(); i++) {
            if (moves.move_list[i] == tt_move) {
                swap(moves.move_list[0], moves.move_list[i]);
                break;
            }
        }
    }

    uint32_t best_move_found = 0;

    if (maximizing) {
        int max_eval = -100000;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int eval = minimax_new(engine, board, depth - 1, alpha, beta, false);
            board.unmake_move();
            if (eval > max_eval) {
                max_eval = eval;
                best_move_found = moves.move_list[i];
            }
            alpha = max(alpha, max_eval);
            if (beta <= alpha) break;
        }
        TTFlag flag = TT_EXACT;
        if (max_eval <= alpha_orig) flag = TT_ALPHA;
        else if (max_eval >= beta) flag = TT_BETA;
        engine.tt.store(board.zobrist_hash, depth, max_eval, flag, best_move_found);
        return max_eval;
    } else {
        int min_eval = 100000;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int eval = minimax_new(engine, board, depth - 1, alpha, beta, true);
            board.unmake_move();
            if (eval < min_eval) {
                min_eval = eval;
                best_move_found = moves.move_list[i];
            }
            beta = min(beta, min_eval);
            if (beta <= alpha) break;
        }
        TTFlag flag = TT_EXACT;
        if (min_eval >= beta_orig) flag = TT_BETA;
        else if (min_eval <= alpha_orig) flag = TT_ALPHA;
        engine.tt.store(board.zobrist_hash, depth, min_eval, flag, best_move_found);
        return min_eval;
    }
}

int main() {
    init_magics();
    Engine engine;

    vector<string> test_fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", // Start pos
        "r1bqk2r/pppp1ppp/2n2n2/4p3/2B1P3/2P2N2/PPPP1PPP/R1BQK2R b KQkq - 0 5", // Italian game
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", // Kiweteve
        "r2q1rk1/ppp2ppp/2n1bn2/2b1p3/3pP3/3P1NPP/PPP2PB1/R1BQ1RK1 w - - 0 1" // Middlegame
    };

    cout << "==========================================================================" << endl;
    cout << "          COMPARING OLD SEARCH (NO TT) VS NEW SEARCH (WITH TT)" << endl;
    cout << "==========================================================================" << endl << endl;

    int depth = 6;

    for (size_t i = 0; i < test_fens.size(); i++) {
        Board board;
        board.set_fen(test_fens[i]);

        cout << "--- POSITION " << (i + 1) << " ---" << endl;
        cout << "FEN: " << test_fens[i] << endl;

        // 1. OLD SEARCH
        old_nodes = 0;
        auto t0 = high_resolution_clock::now();
        int score_old = minimax_old(engine, board, depth, -100000, 100000, board.side_to_move == WHITE);
        auto t1 = high_resolution_clock::now();
        long long ms_old = duration_cast<milliseconds>(t1 - t0).count();

        // 2. NEW SEARCH WITH TT
        new_nodes = 0;
        engine.tt.clear();
        auto t2 = high_resolution_clock::now();
        int score_new = minimax_new(engine, board, depth, -100000, 100000, board.side_to_move == WHITE);
        auto t3 = high_resolution_clock::now();
        long long ms_new = duration_cast<milliseconds>(t3 - t2).count();

        cout << "OLD (No TT) : Score = " << score_old << " | Nodes: " << old_nodes << " | Time: " << ms_old << " ms" << endl;
        cout << "NEW (With TT): Score = " << score_new << " | Nodes: " << new_nodes << " | Time: " << ms_new << " ms" << endl;
        
        double speedup = (ms_old > 0) ? (double)ms_old / (double)ms_new : 1.0;
        double node_reduction = (double)old_nodes / (double)new_nodes;

        cout << "⚡ NODE REDUCTION: " << node_reduction << "x fewer nodes" << endl;
        cout << "⚡ SPEEDUP:        " << speedup << "x faster!" << endl << endl;
    }

    return 0;
}
