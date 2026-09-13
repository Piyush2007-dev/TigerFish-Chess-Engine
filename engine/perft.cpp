// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — STANDALONE HIGH SPEED PERFT (newengine/perft.cpp)
//
// Usage:
//   perft bench                            (Runs full 1, 16, 24 thread test suite)
//   perft startpos 6                       (Default auto-detect threads)
//   perft startpos 6 --threads 24          (24 threads parallel perft)
//   perft startpos 6 --split               (Displays per-root-move node counts)
//   perft startpos 8 --tt                  (Transposition Table accelerated perft)
//   perft "<fen>" <depth> [--threads N] [--split] [--tt]
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "position.cpp"
#include "movegen.cpp"
#include "perft.h"

#include <iostream>
#include <string>

using namespace tigerfish;

int main(int argc, char* argv[]) {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

    if (argc < 2) {
        std::cout << "TigerFish 2.0 High-Performance Parallel Perft Engine\n\n";
        std::cout << "Usage:\n";
        std::cout << "  perft bench                                   Run standardized multi-core benchmark suite\n";
        std::cout << "  perft startpos <depth> [options]              Perft on initial board position\n";
        std::cout << "  perft kiwipete <depth> [options]              Perft on Kiwipete position\n";
        std::cout << "  perft \"<fen>\" <depth> [options]               Perft on custom FEN string\n\n";
        std::cout << "Options:\n";
        std::cout << "  --threads <N>                                 Number of worker threads (default: all cores)\n";
        std::cout << "  --split                                       Display move-by-move split breakdown\n";
        std::cout << "  --tt                                          Enable 65,536-bucket Transposition Table\n";
        std::cout << "  --tt-size <MB>                                Set TT memory size in Megabytes (default: 256)\n";
        std::cout << "  --split-depth <N> (or --work-depth, -w)       Set work pool split depth (default: 3 for depth>=6)\n";
        std::cout << "  --stream                                      Live telemetry monitor (NPS, ETA, %)\n";
        return 0;
    }

    std::string arg1 = argv[1];
    if (arg1 == "bench") {
        run_perft_benchmark_suite();
        return 0;
    }

    if (argc < 3) {
        std::cerr << "Error: missing depth argument.\n";
        std::cerr << "Example: perft startpos 6\n";
        return 1;
    }

    std::string fen = arg1;
    if (fen == "startpos") {
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    } else if (fen == "kiwipete") {
        fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    }

    int depth = std::stoi(argv[2]);
    int threads = 0; // 0 = auto-detect hardware cores
    int split_depth = 0; // 0 = auto default (3 for depth>=6, 2 for depth>=3, 1 otherwise)
    bool split = false;
    bool use_tt = false;
    bool stream = false;

    size_t tt_size_mb = 256;

    for (int i = 3; i < argc; ++i) {
        std::string opt = argv[i];
        if (opt == "--threads" && i + 1 < argc) {
            threads = std::stoi(argv[++i]);
        } else if (opt == "--split") {
            split = true;
        } else if (opt == "--tt") {
            use_tt = true;
        } else if (opt == "--stream") {
            stream = true;
        } else if (opt == "--tt-size" && i + 1 < argc) {
            tt_size_mb = std::stoull(argv[++i]);
        } else if ((opt == "--split-depth" || opt == "--work-depth" || opt == "--work-size" || opt == "-w") && i + 1 < argc) {
            split_depth = std::stoi(argv[++i]);
        }
    }

    Position pos;
    StateInfo state;
    if (!set_fen(pos, fen, state)) {
        std::cerr << "Error: invalid FEN string: " << fen << "\n";
        return 1;
    }

    int effective_split = (split_depth > 0) ? std::min(split_depth, std::max(1, depth - 1))
                                            : (depth >= 6 ? 3 : (depth >= 3 ? 2 : 1));

    std::cout << "\nRunning Perft Depth " << depth << " ("
              << (use_tt ? "TT-Accelerated" : "Raw Tree") << ", "
              << (threads > 0 ? std::to_string(threads) : "Auto") << " Threads, "
              << "Work Pool Depth: " << effective_split << ")...\n\n";

    PerftResult res = run_perft(pos, depth, threads, use_tt, split, tt_size_mb, stream, split_depth);

    if (split) {
        std::cout << "--------------------------------------\n";
        std::cout << "  Move         Subtree Nodes          \n";
        std::cout << "--------------------------------------\n";
        for (const auto& sm : res.split_results) {
            std::cout << "  " << std::setw(6) << std::left << sm.first
                      << "       " << std::setw(16) << std::right << sm.second << "\n";
        }
        std::cout << "--------------------------------------\n";
    }

    std::cout << "======================================\n";
    std::cout << "Total Nodes: " << res.total_nodes << "\n";
    std::cout << "Elapsed Time: " << std::fixed << std::setprecision(2) << res.elapsed_ms << " ms\n";
    std::cout << "Throughput:   " << std::fixed << std::setprecision(2) << (res.nps / 1'000'000.0) << " M NPS";
    if (res.nps >= 1'000'000'000.0) {
        std::cout << " (" << std::setprecision(2) << (res.nps / 1'000'000'000.0) << " Billion NPS)";
    }
    std::cout << "\n======================================\n";

    return 0;
}
