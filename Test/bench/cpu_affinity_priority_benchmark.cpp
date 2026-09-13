#include "engine/perft.h"
#include <iostream>
#include <windows.h>

using namespace tigerfish;

int main(int argc, char* argv[]) {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    Position root;
    StateInfo st;
    set_fen(root, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", st);

    std::cout << "Testing Depth 8 TT with pinned 24 threads...\n";
    // Target split depth 4
    PerftResult res = run_perft(root, 8, 24, true, false, 512, false);
    std::cout << "Nodes: " << res.total_nodes << " in " << res.elapsed_ms << " ms (" << res.nps/1e6 << " M NPS)\n";
    return 0;
}
