// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — MAIN ENTRY POINT (newengine/main.cpp)
// Pure C++20 Data-Oriented Design. Zero header files, zero OOP overhead.
// Dispatches between standard UCI protocol and TigerFish CLI/JSON commands.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "bitboards.cpp"
#include "zobrist.cpp"
#include "position.cpp"
#include "movegen.cpp"
#include "tt.cpp"
#include "eval.cpp"
#include "search.cpp"
#include "opening_book.cpp"
#include "tablebase.cpp"
#include "rl_interface.cpp"
#include "uci.cpp"
#include "cli.cpp"

namespace tigerfish {
inline void init_engine() noexcept {
    init_bitboards();
    init_zobrist();
    init_pst_tables();
    init_king_shields();
    init_lmr_table();
}
} // namespace tigerfish

int main(int argc, char* argv[]) {
    tigerfish::init_engine();

    // If invoked with no arguments, or explicitly with "uci", launch the standard UCI loop
    if (argc <= 1) {
        tigerfish::run_uci_loop();
        return 0;
    }

    std::string command = argv[1];

    if (command == "uci") {
        tigerfish::run_uci_loop();
        return 0;
    }

    if (command == "interactive") {
        return tigerfish::run_interactive_mode();
    }

    if (command == "analyze") {
        return tigerfish::run_analyze_command(argc, argv);
    }

    if (command == "best") {
        return tigerfish::run_best_command(argc, argv);
    }

    if (command == "perft") {
        return tigerfish::run_perft_command(argc, argv);
    }

    if (command == "bench") {
        return tigerfish::run_bench_command(argc, argv);
    }

    std::cerr << "Unknown command: " << command << "\n";
    std::cerr << "Available commands: uci, interactive, analyze, best, perft, bench\n";
    return 1;
}
