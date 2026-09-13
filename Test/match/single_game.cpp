#include "../../engine/oldsearch.cpp"
#include "../../engine/search.cpp"
#include <iostream>
#include <chrono>
using namespace std;
using namespace chrono;

int main(int argc, char* argv[]) {
    int time_ms = (argc > 1) ? std::stoi(argv[1]) : 2000;
    int max_halves = (argc > 2) ? std::stoi(argv[2]) : 200;
    init_rays();
    init_magics();

    Board board;
    board.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    Engine white_engine;
    OldEngine black_engine;
    white_engine.tt.clear();
    black_engine.tt.clear();

    cout << "=== SINGLE GAME: Enhanced(White) vs Old(Black) ===" << endl;
    cout << "Time per move: " << time_ms << "ms | Max half-moves: " << max_halves << endl;
    cout << "==================================================" << endl;

    auto game_start = steady_clock::now();
    uint64_t w_total_nodes = 0, b_total_nodes = 0;
    long long w_total_time = 0, b_total_time = 0;

    for (int half = 0; half < max_halves; half++) {
        bool white = (board.side_to_move == WHITE);

        MoveList moves;
        MoveGenerator gen;
        gen.generate_moves(board, moves);
        if (moves.size() == 0) break;

        auto t0 = steady_clock::now();
        uint32_t best = 0;

        if (white) {
            white_engine.configure_search(0, time_ms, nullptr);
            best = white_engine.best_move(board, 100);
        } else {
            black_engine.configure_search(0, time_ms, nullptr);
            best = black_engine.best_move(board, 100);
        }

        auto t1 = steady_clock::now();
        long long ms = duration_cast<milliseconds>(t1 - t0).count();
        uint64_t nodes = white ? white_engine.nodes_searched : black_engine.nodes_searched;

        if (white) { w_total_nodes += nodes; w_total_time += ms; }
        else       { b_total_nodes += nodes; b_total_time += ms; }

        if (best == 0) { cout << "No move found!" << endl; break; }

        string eng = white ? "Enh" : "Old";
        cout << half+1 << ". " << eng << " " << move_to_uci(best)
             << " [" << nodes << " nodes, " << ms << "ms]"
             << " depth=" << (white ? white_engine.completed_depth : black_engine.completed_depth)
             << endl;

        board.make_move(best);

        if (board.is_repetition(3)) { cout << "\n--- 3fold repetition ---" << endl; break; }
        if (board.halfmove_clock >= 100) { cout << "\n--- 50-move rule ---" << endl; break; }
    }

    auto game_end = steady_clock::now();
    long long total_ms = duration_cast<milliseconds>(game_end - game_start).count();

    MoveList final_moves;
    MoveGenerator fg;
    fg.generate_moves(board, final_moves);
    string result = "1/2-1/2";
    if (final_moves.size() == 0 && is_in_check(board))
        result = (board.side_to_move == WHITE) ? "0-1" : "1-0";

    cout << "\n==================================================" << endl;
    cout << "RESULT: " << result << endl;
    cout << "Total time: " << (total_ms/1000.0) << "s" << endl;
    cout << "White (Enh): " << w_total_nodes << " nodes, " << (w_total_time/1000.0) << "s" << endl;
    cout << "Black (Old): " << b_total_nodes << " nodes, " << (b_total_time/1000.0) << "s" << endl;
    cout << "Enh NPS: " << (w_total_time > 0 ? w_total_nodes*1000/w_total_time : 0) << endl;
    cout << "Old NPS: " << (b_total_time > 0 ? b_total_nodes*1000/b_total_time : 0) << endl;
    cout << "==================================================" << endl;
    return 0;
}
