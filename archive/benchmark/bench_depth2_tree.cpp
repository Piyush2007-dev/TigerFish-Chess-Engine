// bench_depth2_tree.cpp — Traces all Black replies under 1. d2d4 at Depth 2
#include "../../engine/search.cpp"
#include <iostream>
#include <iomanip>

int main() {
    init_rays();
    init_magics();

    Board board;
    board.set_fen(board.START_FEN);

    // Root is searching 1. d2d4 with Alpha = +20 (from earlier Move d2d3)
    int root_alpha = 20;
    int root_beta  = 100000;
    Engine engine;

    MoveList root_moves;
    MoveGenerator mg_root;
    mg_root.generate_moves(board, root_moves);

    uint32_t f2f3_move = 0;
    for (int i = 0; i < root_moves.size(); i++) {
        if (move_to_uci(root_moves.move_list[i]) == "f2f3") {
            f2f3_move = root_moves.move_list[i];
            break;
        }
    }
    board.make_move(f2f3_move);

    MoveList black_moves;
    MoveGenerator mg;
    mg.generate_moves(board, black_moves);

    cout << "=======================================================================\n";
    cout << "   BLACK'S REPLIES TO 1. f2f3 (Alpha Floor passed from Root = +40)\n";
    cout << "=======================================================================\n\n";

    int root_alpha_f3 = 40; // Alpha floor established by 1. d2d4
    int beta = root_beta;
    int best_black = 100000;

    for (int i = 0; i < black_moves.size(); i++) {
        uint32_t m = black_moves.move_list[i];
        string uci = move_to_uci(m);

        board.make_move(m);
        // Search at Depth 1 (White's reply)
        int score = engine.minimax(board, 1, root_alpha, beta, true);
        board.unmake_move();

        cout << "  Black Reply #" << setw(2) << (i + 1) << " | " << setw(5) << uci << " | Score: " << setw(5) << score;

        if (score < best_black) best_black = score;
        if (best_black < beta)  beta = best_black;

        // CUTOFF CHECK: Is Beta <= Root Alpha (+40)?
        if (beta <= root_alpha_f3) {
            cout << "  <-- BETA CUTOFF TRIGGERED! (Beta " << beta << " <= Alpha " << root_alpha_f3 << ")\n";
            cout << "  *** PRUNED! Remaining " << (black_moves.size() - i - 1) << " Black replies SKIPPED! ***\n";
            break;
        } else {
            cout << "  (Beta ceiling updated to " << beta << ")\n";
        }
    }

    cout << "\n=======================================================================\n";
    cout << "  1. d2d4 EVALUATION COMPLETE — Final Black Score: " << best_black << "\n";
    cout << "=======================================================================\n";

    return 0;
}
