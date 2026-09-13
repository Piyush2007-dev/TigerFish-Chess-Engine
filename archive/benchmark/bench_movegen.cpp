// bench_movegen.cpp — Measures move generation time for a given position
// Compile: g++ -O2 -std=c++20 -o bench_movegen.exe engine/bench_movegen.cpp
// Usage:   bench_movegen.exe [FEN] [iterations]

#include "rules.cpp"
#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    init_rays();
    init_magics();

    // Default: Middle game complex position (Sicilian Najdorf after 6.Bg5)
    string fen = "rnbqkb1r/1p2pppp/p2p1n2/6B1/3NP3/2N5/PPP2PPP/R2QKB1R w KQkq - 0 7";
    int iterations = 1'000'000;

    if (argc >= 2) fen = argv[1];
    if (argc >= 3) iterations = stoi(argv[2]);

    Board board;
    board.set_fen(fen);
    board.print_board();

    MoveList moves;
    MoveGenerator mg;

    // === Warm up (avoid cold cache / branch prediction noise) ===
    for (int i = 0; i < 10000; i++) {
        moves.clear();
        mg.generate_moves(board, moves);
    }

    int move_count = moves.size();

    // === Benchmark ===
    auto t0 = chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++) {
        moves.clear();
        mg.generate_moves(board, moves);
    }

    auto t1 = chrono::high_resolution_clock::now();

    double total_ns   = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();
    double avg_ns     = total_ns / iterations;
    double avg_us     = avg_ns / 1000.0;
    double per_second = 1e9 / avg_ns;

    cout << "\n========================================\n";
    cout << "  TigerFish Move Generation Benchmark\n";
    cout << "========================================\n";
    cout << "  FEN        : " << fen << "\n";
    cout << "  Iterations : " << iterations << "\n";
    cout << "  Legal Moves: " << move_count << "\n";
    cout << "  Total Time : " << (long long)(total_ns / 1e6) << " ms\n";
    cout << "  Avg / call : " << avg_ns << " ns\n";
    cout << "  Avg / call : " << avg_us << " µs\n";
    cout << "  Calls/sec  : " << (long long)per_second << "\n";
    cout << "========================================\n";

    // Print legal moves
    cout << "\nLegal moves (" << move_count << "):\n  ";
    for (int i = 0; i < moves.size(); i++) {
        cout << move_to_uci(moves.move_list[i]) << " ";
    }
    cout << "\n";

    return 0;
}
