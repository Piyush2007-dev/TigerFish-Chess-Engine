// bench_movegen_scale.cpp
// Streaming benchmark: generate position → benchmark it → discard it.
// No RAM explosion. Scales to any number of positions.
// Compile: g++ -O3 -std=c++20 -o bench_scale.exe old_files/benchmark/bench_movegen_scale.cpp
// Usage:   bench_scale.exe [total_positions]

#include "../../engine/rules.cpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <climits>
#include <iomanip>

int main(int argc, char* argv[]) {
    init_rays();
    init_magics();

    long long total_positions = 2'000'000;
    if (argc >= 2) total_positions = stoll(argv[1]);

    mt19937 rng(42);
    uniform_int_distribution<int> ply_dist(5, 40);
    MoveGenerator mg;
    MoveList moves;

    // Histogram buckets for timing distribution (in ns)
    // 0-100, 100-150, 150-200, 200-250, 250-300, 300-400, 400+
    const int BUCKETS = 7;
    const int bucket_bounds[] = {100, 150, 200, 250, 300, 400, INT_MAX};
    long long histogram[BUCKETS] = {};
    long long histogram_moves[BUCKETS] = {};  // total moves per bucket

    long long tested = 0;
    long long skipped = 0;
    double total_ns = 0;
    double min_ns = 1e18, max_ns = 0;
    long long total_moves = 0;
    int min_movecnt = INT_MAX, max_movecnt = 0;

    // Progress reporting every 10%
    long long report_interval = total_positions / 10;
    long long next_report = report_interval;

    cout << "TigerFish Streaming Position Benchmark\n";
    cout << "Testing " << total_positions / 1'000'000.0 << "M positions...\n\n";

    auto bench_start = chrono::high_resolution_clock::now();

    while (tested < total_positions) {
        // Build a random board by playing random moves
        int ply = ply_dist(rng);
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

        // Skip terminal positions
        moves.clear();
        mg.generate_moves(board, moves);
        if (!ok || moves.size() == 0) { skipped++; continue; }
        int mc = moves.size();

        // Time a single generate_moves call
        auto t0 = chrono::high_resolution_clock::now();
        moves.clear();
        mg.generate_moves(board, moves);
        auto t1 = chrono::high_resolution_clock::now();

        double ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();
        total_ns += ns;
        total_moves += mc;
        tested++;

        if (ns < min_ns) min_ns = ns;
        if (ns > max_ns) max_ns = ns;
        if (mc < min_movecnt) min_movecnt = mc;
        if (mc > max_movecnt) max_movecnt = mc;

        // Histogram
        for (int b = 0; b < BUCKETS; b++) {
            if ((int)ns < bucket_bounds[b]) {
                histogram[b]++;
                histogram_moves[b] += mc;
                break;
            }
        }

        // Progress report
        if (tested >= next_report) {
            double pct = 100.0 * tested / total_positions;
            double avg_so_far = total_ns / tested;
            cout << "  [" << fixed << setprecision(0) << pct << "%] "
                 << tested / 1'000'000.0 << "M done | "
                 << "avg so far: " << setprecision(1) << avg_so_far << " ns\n";
            next_report += report_interval;
        }
    }

    auto bench_end = chrono::high_resolution_clock::now();
    double wall_ms = chrono::duration_cast<chrono::microseconds>(bench_end - bench_start).count() / 1000.0;

    double avg_ns      = total_ns / tested;
    double avg_moves   = (double)total_moves / tested;
    double calls_per_s = 1e9 / avg_ns;

    cout << "\n╔══════════════════════════════════════════════════════╗\n";
    cout << "║    TigerFish Scale Benchmark — " << fixed << setprecision(1)
         << total_positions / 1e6 << "M positions      ║\n";
    cout << "╚══════════════════════════════════════════════════════╝\n";
    cout << "  Positions tested : " << tested << "\n";
    cout << "  Skipped (no moves): " << skipped << "\n";
    cout << "  Wall clock time  : " << wall_ms / 1000.0 << " seconds\n";
    cout << "\n  ── Timing (per generate_moves call) ──\n";
    cout << "  Average          : " << setprecision(2) << avg_ns << " ns\n";
    cout << "  Fastest          : " << min_ns << " ns\n";
    cout << "  Slowest          : " << max_ns << " ns\n";
    cout << "  Spread           : " << max_ns - min_ns << " ns\n";
    cout << "\n  ── Move Counts ──\n";
    cout << "  Average moves    : " << setprecision(2) << avg_moves << "\n";
    cout << "  Min moves seen   : " << min_movecnt << "\n";
    cout << "  Max moves seen   : " << max_movecnt << "\n";
    cout << "\n  ── Throughput ──\n";
    cout << "  Calls / second   : " << fixed << setprecision(0) << calls_per_s << "\n";
    cout << "  Nodes / second   : ~" << calls_per_s / 1e6 << "M\n";

    cout << "\n  ── Timing Distribution (ns per call) ──\n";
    const char* labels[] = {"<100", "100-150", "150-200", "200-250", "250-300", "300-400", "400+"};
    for (int b = 0; b < BUCKETS; b++) {
        double pct = 100.0 * histogram[b] / tested;
        double avg_mv = histogram[b] > 0 ? (double)histogram_moves[b] / histogram[b] : 0;
        cout << "  " << setw(9) << left << labels[b]
             << " : " << setw(10) << right << histogram[b]
             << " positions (" << setprecision(1) << setw(5) << pct << "%) "
             << " avg moves=" << setprecision(1) << avg_mv << "\n";
    }
    cout << "══════════════════════════════════════════════════════════\n";

    return 0;
}
