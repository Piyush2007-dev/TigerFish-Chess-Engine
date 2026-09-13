// ═══ NODES-TO-DEPTH BENCHMARK (FIXED TIME) ═══════════════════════════
// Runs both engines (old minimax vs enhanced) on the same positions
// with a FIXED TIME budget (default 5s per move). Reports which
// engine searches MORE nodes in the same time, and which reaches
// DEEPER search at the same time limit.
//
// Compile: g++ -O3 -std=c++20 -Wl,--stack,67108864 -o bench_nodes.exe bench_nodes_vs_depth.cpp
// Run:     ./bench_nodes.exe              (5s default)
//          ./bench_nodes.exe 10           (10s per move)
//          ./bench_nodes.exe 5 1          (5s, single thread)
// ══════════════════════════════════════════════════════════════════════

#include "../../engine/search.cpp"
#include "../../engine/oldsearch.cpp"

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <chrono>
#include <atomic>
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

// Result for one engine on one position at one depth
struct RunResult {
    uint64_t nodes;
    double   time_ms;
    uint64_t nps;
    int      depth_reached;
    uint32_t best_move;
    int      score;
};

static RunResult run_engine(Board& board, int max_depth, int time_ms, bool use_old) {
    RunResult r{};
    r.best_move = 0;

    if (use_old) {
        OldEngine engine;
        engine.configure_search(0, time_ms, nullptr);
        auto t0 = high_resolution_clock::now();
        r.best_move = engine.best_move(board, max_depth);
        auto t1 = high_resolution_clock::now();
        r.time_ms      = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
        r.nodes        = engine.nodes_searched;
        r.depth_reached = engine.completed_depth;
        r.score        = engine.root_scores.empty() ? 0 : engine.root_scores[0];
    } else {
        Engine engine;
        engine.configure_search(0, time_ms, nullptr);
        auto t0 = high_resolution_clock::now();
        r.best_move = engine.best_move(board, max_depth);
        auto t1 = high_resolution_clock::now();
        r.time_ms      = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
        r.nodes        = engine.nodes_searched;
        r.depth_reached = engine.completed_depth;
        r.score        = engine.root_scores.empty() ? 0 : engine.root_scores[0];
    }

    r.nps = r.time_ms > 0 ? (uint64_t)(r.nodes * 1000.0 / r.time_ms) : r.nodes;
    return r;
}

static void print_header() {
    cout << left
         << setw(14) << "Pos"
         << setw(8)  << "Depth"
         << setw(7)  << "Engine"
         << setw(14) << "Nodes"
         << setw(12) << "Time(ms)"
         << setw(14) << "NPS"
         << setw(10) << "Reached"
         << setw(12) << "Score"
         << setw(10) << "Move"
         << endl;
    cout << string(101, '-') << endl;
}

static void print_row(const string& pos, int depth, const string& eng,
                      const RunResult& r) {
    cout << left
         << setw(14) << pos
         << setw(8)  << depth
         << setw(7)  << eng
         << setw(14) << r.nodes
         << setw(12) << fixed << setprecision(1) << r.time_ms
         << setw(14) << r.nps
         << setw(10) << r.depth_reached
         << setw(12) << r.score
         << setw(10) << (r.best_move ? move_to_uci(r.best_move) : "---")
         << endl;
}

int main(int argc, char* argv[]) {
    int time_per_move = 5000;  // default: 5 seconds
    if (argc > 1) time_per_move = atoi(argv[1]);
    if (time_per_move < 100) time_per_move = 100;

    init_rays();
    init_magics();

    auto positions = get_positions();
    vector<int> depths = {3, 4, 5, 6, 7, 8};

    cout << "\n==============================================" << endl;
    cout << "  NODES-TO-DEPTH BENCHMARK (FIXED TIME)" << endl;
    cout << "  Time per move: " << time_per_move << " ms" << endl;
    cout << "  Positions: " << positions.size() << endl;
    cout << "  Depths: ";
    for (int d : depths) cout << d << " ";
    cout << "\n==============================================\n" << endl;

    // ── Phase 1: Table — Old vs New at each depth ──────────────────
    print_header();

    // Aggregate stats per depth
    struct DepthAgg {
        uint64_t old_nodes = 0, new_nodes = 0;
        double   old_time = 0, new_time = 0;
        int      old_deeper = 0, new_deeper = 0;
        int      count = 0;
    };
    vector<DepthAgg> agg(depths.back() + 1);

    for (auto& pos : positions) {
        Board board;
        board.set_fen(pos.fen);

        for (int d : depths) {
            RunResult old_r = run_engine(board, d, time_per_move, true);
            RunResult new_r = run_engine(board, d, time_per_move, false);

            print_row(pos.name, d, "Old", old_r);
            print_row(pos.name, d, "New", new_r);

            cout << endl;  // blank line between positions

            // Aggregate
            agg[d].old_nodes += old_r.nodes;
            agg[d].new_nodes += new_r.nodes;
            agg[d].old_time  += old_r.time_ms;
            agg[d].new_time  += new_r.time_ms;
            agg[d].count++;
            if (old_r.depth_reached > new_r.depth_reached) agg[d].old_deeper++;
            if (new_r.depth_reached > old_r.depth_reached) agg[d].new_deeper++;
        }
    }

    // ── Phase 2: Summary table ──────────────────────────────────────
    cout << "\n==============================================" << endl;
    cout << "  SUMMARY: Old vs New (averaged over " << positions.size() << " positions)" << endl;
    cout << "  Time limit: " << time_per_move << " ms per move\n" << endl;

    cout << left
         << setw(8)  << "Depth"
         << setw(16) << "Old Nodes"
         << setw(16) << "New Nodes"
         << setw(12) << "Ratio"
         << setw(14) << "Old NPS"
         << setw(14) << "New NPS"
         << setw(14) << "Old Deeper"
         << setw(14) << "New Deeper"
         << endl;
    cout << string(108, '-') << endl;

    for (int d : depths) {
        auto& a = agg[d];
        if (a.count == 0) continue;
        uint64_t avg_old_nodes = a.old_nodes / a.count;
        uint64_t avg_new_nodes = a.new_nodes / a.count;
        uint64_t avg_old_nps   = a.old_time > 0 ? (uint64_t)(a.old_nodes * 1000.0 / a.old_time) : 0;
        uint64_t avg_new_nps   = a.new_time > 0 ? (uint64_t)(a.new_nodes * 1000.0 / a.new_time) : 0;
        double ratio = avg_old_nodes > 0 ? (double)avg_new_nodes / avg_old_nodes : 0;

        cout << left
             << setw(8)  << d
             << setw(16) << avg_old_nodes
             << setw(16) << avg_new_nodes
             << setw(12) << fixed << setprecision(2) << ratio << "x"
             << setw(14) << avg_old_nps
             << setw(14) << avg_new_nps
             << setw(14) << (to_string(a.old_deeper) + "/" + to_string(a.count))
             << setw(14) << (to_string(a.new_deeper) + "/" + to_string(a.count))
             << endl;
    }

    // ── Phase 3: Overall totals ─────────────────────────────────────
    uint64_t total_old_nodes = 0, total_new_nodes = 0;
    double   total_old_time = 0, total_new_time = 0;
    int      total_tests = 0;

    for (int d : depths) {
        auto& a = agg[d];
        total_old_nodes += a.old_nodes;
        total_new_nodes += a.new_nodes;
        total_old_time  += a.old_time;
        total_new_time  += a.new_time;
        total_tests += a.count;
    }

    cout << string(108, '-') << endl;
    cout << left
         << setw(8)  << "TOTAL"
         << setw(16) << total_old_nodes
         << setw(16) << total_new_nodes
         << setw(12) << (total_old_nodes > 0 ? (to_string((double)total_new_nodes / total_old_nodes).substr(0, 5) + "x") : string("n/a"))
         << setw(14) << (total_old_time > 0 ? (uint64_t)(total_old_nodes * 1000.0 / total_old_time) : 0)
         << setw(14) << (total_new_time > 0 ? (uint64_t)(total_new_nodes * 1000.0 / total_new_time) : 0)
         << endl;

    cout << "\n  " << total_tests << " position×depth combinations tested" << endl;
    cout << "  Old avg time: " << (total_tests > 0 ? total_old_time / total_tests : 0) << " ms" << endl;
    cout << "  New avg time: " << (total_tests > 0 ? total_new_time / total_tests : 0) << " ms" << endl;

    // ── Phase 4: Verdict ────────────────────────────────────────────
    int new_faster_nodes = 0, old_faster_nodes = 0;
    for (int d : depths) {
        auto& a = agg[d];
        if (a.count == 0) continue;
        if (a.new_nodes > a.old_nodes) new_faster_nodes++;
        else old_faster_nodes++;
    }

    cout << "\n  VERDICT: " << endl;
    if (new_faster_nodes > old_faster_nodes) {
        cout << "  New engine searches MORE nodes in the same time at "
             << new_faster_nodes << "/" << depths.size() << " depths" << endl;
        cout << "  → Enhanced pruning lets it SEARCH FASTER, not just smarter" << endl;
    } else if (old_faster_nodes > new_faster_nodes) {
        cout << "  Old engine searches more nodes (less pruning overhead)" << endl;
        cout << "  → But new engine may find BETTER moves with fewer nodes" << endl;
    } else {
        cout << "  Equal node throughput — strength difference is in move quality" << endl;
    }

    cout << "\n==============================================" << endl;
    return 0;
}
