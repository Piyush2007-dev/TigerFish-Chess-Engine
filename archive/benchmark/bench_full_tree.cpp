// bench_full_tree.cpp — Traces all 20 root moves and shows cutoffs
#include "../../engine/search.cpp"
#include <iostream>
#include <iomanip>

int main() {
    init_rays();
    init_magics();

    Board board;
    board.set_fen(board.START_FEN);

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    cout << "=======================================================================\n";
    cout << "   TIGERFISH DEPTH 3 SEARCH TRACE — ALL 20 LEGAL ROOT MOVES\n";
    cout << "=======================================================================\n\n";

    int alpha = -100000;
    int beta  =  100000;
    Engine engine;

    for (int i = 0; i < moves.size(); i++) {
        uint32_t m = moves.move_list[i];
        string uci = move_to_uci(m);

        board.make_move(m);

        // Track how many nodes this root move evaluates
        int score = engine.minimax(board, 2, alpha, beta, false);

        board.unmake_move();

        cout << "  Root Move #" << setw(2) << (i + 1) << " | " << setw(5) << uci << " | Score: " << setw(5) << score;

        if (score > alpha) {
            alpha = score;
            cout << "  <-- ALPHA FLOOR UPDATED to " << alpha << "!\n";
        } else {
            cout << "  (Worse than Alpha floor of " << alpha << ")\n";
        }
    }

    cout << "\n=======================================================================\n";
    cout << "  SEARCH FINISHED — Best Root Alpha: " << alpha << "\n";
    cout << "=======================================================================\n";

    return 0;
}
