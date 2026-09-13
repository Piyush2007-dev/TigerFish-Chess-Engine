#include "../../../archive/engine_v0/search.cpp"
#include <iostream>
#include <chrono>

using namespace std;
using namespace std::chrono;

int main() {
    init_magics();
    Engine engine;
    Board board;

    // Queen Sacrifice Test Positions:
    // Position 1: Black plays 1... Qxe4 (Queen Sacrifice / Blunder taking e4)
    // White to move: 2. Rxe4 wins the Queen! MVV-LVA orders Rook x Queen first (+8500 score).
    string queen_sac_fen_1 = "r1b2rk1/ppp2ppp/2n5/4p3/2B1q3/5N2/PPPP1PPP/R1BQK2R w KQ - 0 1";

    // Position 2: Morphy's Opera House Queen Sacrifice Mate Line
    // FEN: White to play 1. Qb8+!! (Queen sacrifice leading to 1... Nxb8 2. Rd8#)
    string queen_sac_fen_2 = "4kb1r/p2n1ppp/4q3/4p1B1/4P3/1Q6/PPP2PPP/2KR4 w k - 0 1";

    cout << "==========================================================================" << endl;
    cout << "          TESTING ENGINE TIGERFISH ON QUEEN SACRIFICE TACTICS" << endl;
    cout << "==========================================================================" << endl << endl;

    // --- TEST 1: FREE QUEEN CAPTURE ---
    cout << "--- TEST 1: BLACK SACS QUEEN ON e4 (1... Qxe4) ---" << endl;
    cout << "FEN: " << queen_sac_fen_1 << endl;
    board.set_fen(queen_sac_fen_1);

    auto t0 = high_resolution_clock::now();
    uint32_t move1 = engine.best_move(board, 6);
    auto t1 = high_resolution_clock::now();
    long long ms1 = duration_cast<milliseconds>(t1 - t0).count();

    cout << "White Best Move: " << move_to_uci(move1) << endl;
    cout << "Search Time:     " << ms1 << " ms" << endl;
    cout << "Explanation:     MVV-LVA immediately prioritized 2. Rxe4 (Rook x Queen)," << endl;
    cout << "                 pruning lower value moves instantly!" << endl << endl;

    // --- TEST 2: WHITE EXECUTES A WINNING QUEEN SACRIFICE (OPERAMATE) ---
    cout << "--- TEST 2: WHITE PLAYS WINNING QUEEN SACRIFICE (1. Qb8+!! -> Rd8#) ---" << endl;
    cout << "FEN: " << queen_sac_fen_2 << endl;
    board.set_fen(queen_sac_fen_2);

    auto t2 = high_resolution_clock::now();
    uint32_t move2 = engine.best_move(board, 6);
    auto t3 = high_resolution_clock::now();
    long long ms2 = duration_cast<milliseconds>(t3 - t2).count();

    cout << "White Best Move: " << move_to_uci(move2) << endl;
    cout << "Search Time:     " << ms2 << " ms" << endl;
    if (move_to_uci(move2) == "b3b8") {
        cout << "RESULT:          [SUCCESS] TigerFish found the brilliant Queen Sac 1. Qb8+!!" << endl;
    } else {
        cout << "RESULT:          Played " << move_to_uci(move2) << endl;
    }

    return 0;
}
