// ═══ OLD ENGINE CLI RUNNER ═════════════════════════════════════════
// CLI tool for the Python benchmark scripts. Takes <FEN> <depth>,
// outputs JSON with move/score/nodes/time/nps/tt_hits/tt_misses.
// Compile: g++ -O3 -std=c++20 -o bench_runner_old.exe bench_runner_old.cpp
// Run:     ./bench_runner_old.exe "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 5

#include "../../engine/oldsearch.cpp"
#include <iostream>
#include <string>
#include <chrono>

using namespace std;
using namespace std::chrono;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <fen> <depth>" << endl;
        return 1;
    }

    init_rays();
    init_magics();

    string fen = argv[1];
    int depth = stoi(argv[2]);

    Board board;
    board.set_fen(fen);

    OldEngine engine;
    engine.configure_search(0, 30000, nullptr);

    auto start = high_resolution_clock::now();
    uint32_t best_move = engine.best_move(board, depth);
    auto end = high_resolution_clock::now();

    double time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
    uint64_t nodes = engine.nodes_searched;
    uint64_t nps = time_ms > 0 ? (uint64_t)(nodes * 1000.0 / time_ms) : nodes;
    int score = engine.root_scores.empty() ? 0 : engine.root_scores[0];

    cout << "{"
         << "\"move\":\"" << (best_move ? move_to_uci(best_move) : "none") << "\","
         << "\"score\":" << score << ","
         << "\"depth\":" << engine.completed_depth << ","
         << "\"nodes\":" << nodes << ","
         << "\"time_ms\":" << time_ms << ","
         << "\"nps\":" << nps << ","
         << "\"tt_hits\":" << engine.tt_hits_old << ","
         << "\"tt_misses\":" << engine.tt_misses_old
         << "}" << endl;

    return 0;
}
