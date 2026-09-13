#include "../../engine/search.cpp"
#include <iostream>
#include <chrono>
#include <vector>

using namespace std;
using namespace std::chrono;

uint64_t benchmark_nodes = 0;

// Search function supporting flags for TT and MVV-LVA
int minimax_4cases(Engine& engine, Board& board, int depth, int alpha, int beta, bool maximizing, bool use_tt, bool use_mvvlva) {
    benchmark_nodes++;
    if (depth == 0) return engine.evaluate(board);

    int alpha_orig = alpha;
    int beta_orig = beta;

    uint32_t tt_move = 0;
    int tt_eval = 0;
    if (use_tt && engine.tt.lookup(board.zobrist_hash, depth, alpha, beta, tt_eval, tt_move)) {
        return tt_eval;
    }

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    // If MVV-LVA is disabled for this test case, strip the MVV-LVA ordering (restore raw order or shuffle)
    if (!use_mvvlva) {
        // Reverse captures to remove MVV-LVA priority
        for (int i = 0; i < moves.count / 2; i++) {
            swap(moves.move_list[i], moves.move_list[moves.count - 1 - i]);
        }
    }

    if (use_tt && tt_move != 0) {
        for (int i = 0; i < moves.size(); i++) {
            if (moves.move_list[i] == tt_move) {
                swap(moves.move_list[0], moves.move_list[i]);
                break;
            }
        }
    }

    if (moves.size() == 0) {
        if (is_in_check(board)) {
            return maximizing ? (-20000 - depth) : (20000 + depth);
        }
        return 0;
    }

    uint32_t best_move = 0;

    if (maximizing) {
        int max_eval = -100000;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int eval = minimax_4cases(engine, board, depth - 1, alpha, beta, false, use_tt, use_mvvlva);
            board.unmake_move();

            if (eval > max_eval) {
                max_eval = eval;
                best_move = moves.move_list[i];
            }
            alpha = max(alpha, max_eval);
            if (beta <= alpha) break;
        }

        if (use_tt) {
            TTFlag flag = TT_EXACT;
            if (max_eval <= alpha_orig) flag = TT_ALPHA;
            else if (max_eval >= beta) flag = TT_BETA;
            engine.tt.store(board.zobrist_hash, depth, max_eval, flag, best_move);
        }
        return max_eval;
    } else {
        int min_eval = 100000;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i]);
            int eval = minimax_4cases(engine, board, depth - 1, alpha, beta, true, use_tt, use_mvvlva);
            board.unmake_move();

            if (eval < min_eval) {
                min_eval = eval;
                best_move = moves.move_list[i];
            }
            beta = min(beta, min_eval);
            if (beta <= alpha) break;
        }

        if (use_tt) {
            TTFlag flag = TT_EXACT;
            if (min_eval >= beta_orig) flag = TT_BETA;
            else if (min_eval <= alpha_orig) flag = TT_ALPHA;
            engine.tt.store(board.zobrist_hash, depth, min_eval, flag, best_move);
        }
        return min_eval;
    }
}

int main() {
    init_magics();
    Engine engine;

    vector<pair<string, string>> test_positions = {
        {"1. Standard Start Position", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"2. Italian Game Middlegame", "r1bqk2r/pppp1ppp/2n2n2/4p3/2B1P3/2P2N2/PPPP1PPP/R1BQK2R b KQkq - 0 5"},
        {"3. Tactical Position (Kiweteve)", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"}
    };

    int depth = 6;

    cout << "==========================================================================" << endl;
    cout << "          4-CASE PERFORMANCE MATRIX ACROSS MULTIPLE POSITIONS (DEPTH 6)" << endl;
    cout << "==========================================================================" << endl << endl;

    struct CaseConfig {
        string name;
        bool tt;
        bool mvvlva;
    } cases[4] = {
        {"Case 1: TT OFF | MVV-LVA OFF (Baseline)", false, false},
        {"Case 2: TT OFF | MVV-LVA ON ", false, true},
        {"Case 3: TT ON  | MVV-LVA OFF", true,  false},
        {"Case 4: TT ON  | MVV-LVA ON  (Full Engine)", true,  true}
    };

    for (const auto& pos : test_positions) {
        cout << "--------------------------------------------------------------------------" << endl;
        cout << "POSITION: " << pos.first << endl;
        cout << "FEN: " << pos.second << endl;
        cout << "--------------------------------------------------------------------------" << endl;

        Board board;
        board.set_fen(pos.second);

        uint64_t base_nodes = 0;
        long long base_time = 0;

        for (int c = 0; c < 4; c++) {
            benchmark_nodes = 0;
            engine.tt.clear();

            auto t0 = high_resolution_clock::now();
            int eval = minimax_4cases(engine, board, depth, -100000, 100000, board.side_to_move == WHITE, cases[c].tt, cases[c].mvvlva);
            auto t1 = high_resolution_clock::now();
            long long ms = duration_cast<milliseconds>(t1 - t0).count();

            if (c == 0) {
                base_nodes = benchmark_nodes;
                base_time = ms;
            }

            double node_reduction = (double)base_nodes / (double)benchmark_nodes;
            double speedup = (base_time > 0) ? (double)base_time / (double)ms : 1.0;

            cout << cases[c].name << endl;
            cout << "   Score: " << eval 
                 << " | Nodes: " << benchmark_nodes << " (" << node_reduction << "x reduction)"
                 << " | Time: " << ms << " ms (" << speedup << "x speedup)" << endl;
        }
        cout << endl;
    }

    return 0;
}
