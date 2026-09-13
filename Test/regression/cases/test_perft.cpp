// ═══ PERFT TESTS ═════════════════════════════════════════════════
// Verifies move generation node counts against known correct values.
// Compile: g++ -O2 -std=c++20 -I engine -o perft.exe test_perft.cpp
// Run:     ./perft.exe

#include "../../../archive/engine_v0/rules.cpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <string>

using namespace std;
using namespace std::chrono;

struct PerftTestCase {
    string name;
    string fen;
    int depth;
    uint64_t expected_nodes;
};

uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1ULL;
    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);
    
    uint64_t nodes = 0;
    for (int i = 0; i < moves.size(); ++i) {
        board.make_move(moves.move_list[i]);
        nodes += perft(board, depth - 1);
        board.unmake_move();
    }
    return nodes;
}

void print_divide(Board& board, int depth) {
    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);
 
    uint64_t total = 0;
    for (int i = 0; i < moves.size(); ++i) {
        uint16_t move = moves.move_list[i];
        board.make_move(move);
        uint64_t nodes = perft(board, depth - 1);
        board.unmake_move();
        total += nodes;
        cout << move_to_uci(move) << ": " << nodes << "\n";
    }
    cout << "Total: " << total << "\n";
}

int main(int argc, char* argv[]) {
    init_rays();
    init_magics();

    if (argc == 3 && string(argv[1]) == "--divide") {
        Board board;
        board.set_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        print_divide(board, stoi(argv[2]));
        return 0;
    }

    vector<PerftTestCase> tests = {
        {"Initial Position - Depth 5", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609ULL},
        {"Kiwipete - Depth 4", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603ULL},
        {"Position 3 - Depth 4", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238ULL},
        {"Position 4 - Depth 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333ULL},
        {"Position 5 - Depth 4", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487ULL},
        {"Position 6 - Depth 4", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594ULL}
    };

    int passed = 0;
    cout << "[\n";
    for (size_t i = 0; i < tests.size(); ++i) {
        const auto& tc = tests[i];
        Board board;
        board.set_fen(tc.fen);

        auto start = high_resolution_clock::now();
        uint64_t nodes = perft(board, tc.depth);
        auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();

        bool is_pass = (nodes == tc.expected_nodes);
        if (is_pass) passed++;

        cout << "  {\n";
        cout << "    \"name\": \"" << tc.name << "\",\n";
        cout << "    \"depth\": " << tc.depth << ",\n";
        cout << "    \"expected\": " << tc.expected_nodes << ",\n";
        cout << "    \"actual\": " << nodes << ",\n";
        cout << "    \"time_ms\": " << elapsed << ",\n";
        cout << "    \"passed\": " << (is_pass ? "true" : "false") << "\n";
        cout << "  }" << (i + 1 < tests.size() ? "," : "") << "\n";
    }
    cout << "]\n";

    return (passed == (int)tests.size()) ? 0 : 1;
}
