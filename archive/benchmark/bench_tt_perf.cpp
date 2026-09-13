#include "../../engine/search.cpp"
#include <iostream>
#include <chrono>
#include <cassert>

using namespace std;
using namespace std::chrono;

uint64_t node_count = 0;

int minimax_bench(Engine& engine, Board& board, int depth, int alpha, int beta, bool maximizing) {
    node_count++;
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
            int eval = minimax_bench(engine, board, depth - 1, alpha, beta, false);
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
            int eval = minimax_bench(engine, board, depth - 1, alpha, beta, true);
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

void test_zobrist_consistency() {
    cout << "--- TEST 1: Zobrist Hash Consistency Check ---" << endl;
    Board board;
    init_magics();

    // Check starting position
    uint64_t full_hash = board.compute_zobrist_hash();
    assert(board.zobrist_hash == full_hash);
    cout << "[PASS] Initial FEN Zobrist hash matches: 0x" << hex << board.zobrist_hash << dec << endl;

    // Do a random walk of 100 moves testing make_move / unmake_move
    srand(42);
    for (int step = 0; step < 100; step++) {
        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        if (moves.size() == 0) break;

        uint32_t m = moves.move_list[rand() % moves.size()];
        board.make_move(m);

        uint64_t expected = board.compute_zobrist_hash();
        if (board.zobrist_hash != expected) {
            cout << "[FAIL] Zobrist hash drift after move " << step << "!" << endl;
            cout << "Incremental: 0x" << hex << board.zobrist_hash << endl;
            cout << "Computed:    0x" << expected << dec << endl;
            assert(false);
        }

        board.unmake_move();
        if (board.zobrist_hash != board.compute_zobrist_hash()) {
            cout << "[FAIL] Zobrist hash drift after unmake!" << endl;
            assert(false);
        }

        board.make_move(m); // Keep the move
    }
    cout << "[PASS] 100-move random walk Zobrist hash consistency verified!" << endl << endl;
}

void benchmark_tt() {
    cout << "--- TEST 2: Transposition Table Performance Benchmark ---" << endl;
    init_magics();

    Engine engine;
    Board board; // Start position

    for (int d = 1; d <= 7; d++) {
        node_count = 0;
        auto t0 = high_resolution_clock::now();
        int score = minimax_bench(engine, board, d, -100000, 100000, true);
        auto t1 = high_resolution_clock::now();

        auto duration_ms = duration_cast<milliseconds>(t1 - t0).count();

        cout << "Depth " << d << ": Score = " << score 
             << " | Nodes: " << node_count 
             << " | Time: " << duration_ms << " ms" << endl;
    }
}

int main() {
    test_zobrist_consistency();
    benchmark_tt();
    return 0;
}
