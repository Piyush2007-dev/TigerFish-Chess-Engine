// ═══ TACTICAL SEARCH TESTS ══════════════════════════════════════
// Solves 3 tactical puzzles (queen mate, back rank, hanging rook).
// Compile: g++ -O2 -std=c++20 -I engine -o tactics.exe test_search_tactics.cpp
// Run:     ./tactics.exe

#include "../../../archive/engine_v0/search.cpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <string>

using namespace std;
using namespace std::chrono;

struct TacticalTest {
    string id;
    string name;
    string fen;
    string expected_move;
};

int main() {
    init_rays();
    init_magics();

    vector<TacticalTest> tests = {
        {"mate_in_1_queen", "Queen Mate in 1", "r1bqkb1r/pppp1ppp/2n5/4p3/2B1n3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 0 4", "f3f7"},
        {"mate_in_1_backrank", "Back Rank Mate in 1", "6k1/5ppp/8/8/8/8/8/1R4K1 w - - 0 1", "b1b8"},
        {"hanging_piece", "Hanging Rook Capture", "r1bq1rk1/pppp1ppp/2n5/4p3/4R3/5N2/PPPP1PPP/R1BQ2K1 b - - 0 1", "d7d5"}
    };

    int solved = 0;
    uint64_t total_nodes = 0;
    auto total_start = high_resolution_clock::now();

    cout << "[\n";
    for (size_t i = 0; i < tests.size(); ++i) {
        const auto& tc = tests[i];
        Board board;
        board.set_fen(tc.fen);

        Engine engine;
        auto start = high_resolution_clock::now();
        uint16_t best = engine.best_move(board, 3); // Depth 3
        auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();

        string actual_move = move_to_uci(best);
        bool is_correct = (actual_move == tc.expected_move);
        if (is_correct) solved++;

        cout << "  {\n";
        cout << "    \"id\": \"" << tc.id << "\",\n";
        cout << "    \"name\": \"" << tc.name << "\",\n";
        cout << "    \"expected\": \"" << tc.expected_move << "\",\n";
        cout << "    \"actual\": \"" << actual_move << "\",\n";
        cout << "    \"time_ms\": " << elapsed << ",\n";
        cout << "    \"solved\": " << (is_correct ? "true" : "false") << "\n";
        cout << "  }" << (i + 1 < tests.size() ? "," : "") << "\n";
    }
    cout << "]\n";

    return (solved == (int)tests.size()) ? 0 : 1;
}
