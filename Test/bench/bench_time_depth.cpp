// ═══ FIXED-TIME DEPTH BENCHMARK ═══════════════════════════════════════
// Both engines search for exactly N seconds (default 5) from each
// position. No depth cap — just let them run. Shows how DEEP each
// engine reaches and how many NODES it burns in the same wall time.
//
// Compile: g++ -O3 -std=c++20 -Wl,--stack,67108864 -o bench_time.exe bench_time_depth.cpp
// Run:     ./bench_time.exe              (5s default)
//          ./bench_time.exe 10           (10s per position)
//          ./bench_time.exe 3 1          (3s, quick test)
// ══════════════════════════════════════════════════════════════════════

#include "../../engine/search.cpp"
#include "../../engine/oldsearch.cpp"

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <chrono>
#include <cmath>

using namespace std;
using namespace chrono;

struct Position {
    string name;
    string fen;
};

static vector<Position> get_positions() {
    return {
        {"startpos",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"sicilian",    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
        {"italian",     "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3"},
        {"midgame",     "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 4"},
        {"complex",     "r1bq1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7"},
        {"rook_end",    "8/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1"},
        {"pawn_end",    "8/4k3/8/4P3/8/8/4K3/8 w - - 0 1"},
    };
}

struct RunResult {
    uint64_t nodes;
    double   time_ms;
    uint64_t nps;
    int      depth_reached;
    uint32_t best_move;
    int      score;
};

static RunResult run_timed(Board& board, int time_ms, bool use_old) {
    RunResult r{};
    r.best_move = 0;
    int big_depth = 100;  // no real depth limit — time is the limit

    if (use_old) {
        OldEngine engine;
        engine.configure_search(0, time_ms, nullptr);
        auto t0 = high_resolution_clock::now();
        r.best_move = engine.best_move(board, big_depth);
        auto t1 = high_resolution_clock::now();
        r.time_ms       = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
        r.nodes         = engine.nodes_searched;
        r.depth_reached = engine.completed_depth;
        r.score         = engine.root_scores.empty() ? 0 : engine.root_scores[0];
    } else {
        Engine engine;
        engine.configure_search(0, time_ms, nullptr);
        auto t0 = high_resolution_clock::now();
        r.best_move = engine.best_move(board, big_depth);
        auto t1 = high_resolution_clock::now();
        r.time_ms       = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
        r.nodes         = engine.nodes_searched;
        r.depth_reached = engine.completed_depth;
        r.score         = engine.root_scores.empty() ? 0 : engine.root_scores[0];
    }

    r.nps = r.time_ms > 0 ? (uint64_t)(r.nodes * 1000.0 / r.time_ms) : r.nodes;
    return r;
}

int main(int argc, char* argv[]) {
    int time_sec = 5;
    if (argc > 1) time_sec = atoi(argv[1]);
    if (time_sec < 1) time_sec = 1;
    int time_ms = time_sec * 1000;

    init_rays();
    init_magics();
    auto positions = get_positions();

    cout << "\n========================================================" << endl;
    cout << "  FIXED-TIME DEPTH BENCHMARK: " << time_sec << "s per position" << endl;
    cout << "  No depth limit — each engine runs for exactly "
         << time_sec << " seconds" << endl;
    cout << "========================================================\n" << endl;

    // ── Per-position table ─────────────────────────────────────────
    cout << left
         << setw(12) << "Position"
         << setw(8)  << "Engine"
         << setw(12) << "Depth"
         << setw(16) << "Nodes"
         << setw(14) << "Time(ms)"
         << setw(14) << "NPS"
         << setw(12) << "Score"
         << setw(10) << "Move"
         << endl;
    cout << string(98, '-') << endl;

    uint64_t sum_old_nodes = 0, sum_new_nodes = 0;
    double   sum_old_time = 0, sum_new_time = 0;
    int      old_wins_depth = 0, new_wins_depth = 0;
    int      n = 0;

    for (auto& pos : positions) {
        Board board;
        board.set_fen(pos.fen);

        RunResult old_r = run_timed(board, time_ms, true);
        RunResult new_r = run_timed(board, time_ms, false);

        cout << left
             << setw(12) << pos.name
             << setw(8)  << "OLD"
             << setw(12) << old_r.depth_reached
             << setw(16) << old_r.nodes
             << setw(14) << fixed << setprecision(1) << old_r.time_ms
             << setw(14) << old_r.nps
             << setw(12) << old_r.score
             << setw(10) << (old_r.best_move ? move_to_uci(old_r.best_move) : "---")
             << endl;

        cout << left
             << setw(12) << ""
             << setw(8)  << "NEW"
             << setw(12) << new_r.depth_reached
             << setw(16) << new_r.nodes
             << setw(14) << fixed << setprecision(1) << new_r.time_ms
             << setw(14) << new_r.nps
             << setw(12) << new_r.score
             << setw(10) << (new_r.best_move ? move_to_uci(new_r.best_move) : "---")
             << endl;

        cout << endl;

        sum_old_nodes += old_r.nodes;
        sum_new_nodes += new_r.nodes;
        sum_old_time  += old_r.time_ms;
        sum_new_time  += new_r.time_ms;
        if (old_r.depth_reached > new_r.depth_reached) old_wins_depth++;
        if (new_r.depth_reached > old_r.depth_reached) new_wins_depth++;
        n++;
    }

    // ── Summary ────────────────────────────────────────────────────
    cout << "========================================================" << endl;
    cout << "  SUMMARY (" << n << " positions, " << time_sec << "s each)" << endl;
    cout << "========================================================\n" << endl;

    cout << left
         << setw(18) << ""
         << setw(18) << "OLD ENGINE"
         << setw(18) << "NEW ENGINE"
         << endl;
    cout << string(54, '-') << endl;

    cout << left
         << setw(18) << "Total nodes"
         << setw(18) << sum_old_nodes
         << setw(18) << sum_new_nodes
         << endl;

    cout << left
         << setw(18) << "Avg nodes/pos"
         << setw(18) << (n > 0 ? sum_old_nodes / n : 0)
         << setw(18) << (n > 0 ? sum_new_nodes / n : 0)
         << endl;

    cout << left
         << setw(18) << "Avg NPS"
         << setw(18) << (sum_old_time > 0 ? (uint64_t)(sum_old_nodes * 1000.0 / sum_old_time) : 0)
         << setw(18) << (sum_new_time > 0 ? (uint64_t)(sum_new_nodes * 1000.0 / sum_new_time) : 0)
         << endl;

    cout << left
         << setw(18) << "Avg time(ms)"
         << setw(18) << fixed << setprecision(1) << (n > 0 ? sum_old_time / n : 0)
         << setw(18) << fixed << setprecision(1) << (n > 0 ? sum_new_time / n : 0)
         << endl;

    cout << left
         << setw(18) << "Deeper wins"
         << setw(18) << (to_string(old_wins_depth) + "/" + to_string(n))
         << setw(18) << (to_string(new_wins_depth) + "/" + to_string(n))
         << endl;

    cout << left
         << setw(18) << "Nodes ratio"
         << setw(18) << "1.00x"
         << setw(18) << (sum_old_nodes > 0
             ? (to_string((double)sum_new_nodes / sum_old_nodes).substr(0, 6) + "x")
             : string("n/a"))
         << endl;

    cout << "\n========================================================" << endl;

    // ── Verdict ────────────────────────────────────────────────────
    cout << "  VERDICT:" << endl;

    if (new_wins_depth > old_wins_depth) {
        cout << "  NEW engine reaches DEEPER at " << new_wins_depth << "/" << n
             << " positions in " << time_sec << "s" << endl;
        cout << "  → Pruning (null-move, LMR, killers) lets it search deeper," << endl;
        cout << "    not just faster. Fewer nodes but MORE depth = smarter search." << endl;
    } else if (old_wins_depth > new_wins_depth) {
        cout << "  OLD engine reaches deeper at " << old_wins_depth << "/" << n
             << " positions in " << time_sec << "s" << endl;
    } else {
        cout << "  Both reach the same depth — strength difference is in evaluation" << endl;
    }

    double node_ratio = sum_old_nodes > 0 ? (double)sum_new_nodes / sum_old_nodes : 0;
    cout << "\n  Old searches " << (node_ratio > 0 ? (1.0/node_ratio) : 0)
         << "x MORE nodes but may not reach deeper depth" << endl;
    cout << "  → This is the pruning trade-off: fewer nodes = smarter tree" << endl;

    cout << "\n========================================================" << endl;
    return 0;
}
