// bench_movegen_random.cpp
// Generates random positions by playing random moves, then benchmarks move generation across all of them.
// Compile: g++ -O2 -std=c++20 -o bench_random.exe old_files/benchmark/bench_movegen_random.cpp
// Usage:   bench_random.exe [num_positions] [iterations_per_pos]

#include "../../engine/rules.cpp"
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <climits>

int main(int argc, char* argv[]) {
    init_rays();
    init_magics();

    int num_positions  = 200;
    int iters_per_pos  = 50000;

    if (argc >= 2) num_positions = stoi(argv[1]);
    if (argc >= 3) iters_per_pos = stoi(argv[2]);

    mt19937 rng(42); // fixed seed for reproducibility

    // Generate random boards by playing random moves
    cout << "Generating " << num_positions << " random positions...\n";

    vector<Board> boards;
    boards.reserve(num_positions);

    MoveGenerator mg;
    MoveList moves;

    // Generate positions with different ply depths for variety
    int generated = 0;
    while ((int)boards.size() < num_positions) {
        int ply = uniform_int_distribution<int>(5, 40)(rng);
        Board board;
        board.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        bool ok = true;
        for (int i = 0; i < ply; i++) {
            moves.clear();
            mg.generate_moves(board, moves);
            if (moves.size() == 0) { ok = false; break; }
            int idx = uniform_int_distribution<int>(0, moves.size() - 1)(rng);
            board.make_move(moves.move_list[idx]);
        }
        // Skip terminal positions (checkmate/stalemate)
        moves.clear();
        mg.generate_moves(board, moves);
        if (!ok || moves.size() == 0) continue;
        boards.push_back(board);
    }

    cout << "Generated " << boards.size() << " positions. Starting benchmark...\n\n";

    // === Warm up ===
    for (int i = 0; i < 5000; i++) {
        moves.clear();
        mg.generate_moves(boards[i % boards.size()], moves);
    }

    // === Benchmark all positions ===
    long long total_ns = 0;
    long long total_moves = 0;
    long long min_ns = LLONG_MAX, max_ns = 0;
    int min_moves_pos = 0, max_moves_pos = 0;
    int min_movecnt = INT_MAX, max_movecnt = 0;

    vector<double> per_pos_ns(boards.size());

    for (int p = 0; p < (int)boards.size(); p++) {
        auto t0 = chrono::high_resolution_clock::now();
        int mc = 0;
        for (int i = 0; i < iters_per_pos; i++) {
            moves.clear();
            mg.generate_moves(boards[p], moves);
            mc = moves.size();
        }
        auto t1 = chrono::high_resolution_clock::now();

        long long ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();
        double avg = (double)ns / iters_per_pos;
        per_pos_ns[p] = avg;
        total_ns += ns;
        total_moves += mc;

        if (ns < min_ns) { min_ns = ns; min_moves_pos = p; }
        if (ns > max_ns) { max_ns = ns; max_moves_pos = p; }
        if (mc < min_movecnt) min_movecnt = mc;
        if (mc > max_movecnt) max_movecnt = mc;
    }

    // Compute median
    vector<double> sorted_ns = per_pos_ns;
    sort(sorted_ns.begin(), sorted_ns.end());
    double median_ns = sorted_ns[sorted_ns.size() / 2];

    double overall_avg_ns  = (double)total_ns / ((long long)boards.size() * iters_per_pos);
    double avg_moves        = (double)total_moves / boards.size();
    double calls_per_sec    = 1e9 / overall_avg_ns;

    cout << "╔══════════════════════════════════════════════════════╗\n";
    cout << "║      TigerFish Random Position Benchmark Results     ║\n";
    cout << "╚══════════════════════════════════════════════════════╝\n";
    cout << "  Positions tested     : " << boards.size() << "\n";
    cout << "  Iterations / position: " << iters_per_pos << "\n";
    cout << "  Total iterations     : " << (long long)boards.size() * iters_per_pos << "\n";
    cout << "\n";
    cout << "  ── Timing ──\n";
    cout << "  Average per call     : " << overall_avg_ns  << " ns\n";
    cout << "  Median  per call     : " << median_ns       << " ns\n";
    cout << "  Fastest position     : " << (double)min_ns / iters_per_pos << " ns (pos #" << min_moves_pos << ")\n";
    cout << "  Slowest position     : " << (double)max_ns / iters_per_pos << " ns (pos #" << max_moves_pos << ")\n";
    cout << "\n";
    cout << "  ── Move Counts ──\n";
    cout << "  Avg legal moves      : " << avg_moves    << "\n";
    cout << "  Min legal moves      : " << min_movecnt  << "\n";
    cout << "  Max legal moves      : " << max_movecnt  << "\n";
    cout << "\n";
    cout << "  ── Throughput ──\n";
    cout << "  Calls / second       : " << (long long)calls_per_sec << "\n";
    cout << "  Nodes / second est.  : ~" << (long long)(calls_per_sec / 1e6) << "M\n";
    cout << "══════════════════════════════════════════════════════════\n";

    return 0;
}
