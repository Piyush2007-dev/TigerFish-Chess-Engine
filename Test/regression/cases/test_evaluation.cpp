// ═══ BASIC EVALUATION TESTS ══════════════════════════════════════
// Tests eval sign convention: white up a queen = positive, etc.
// Compile: g++ -O2 -std=c++20 -I engine -o evaluation.exe test_evaluation.cpp
// Run:     ./evaluation.exe

#include "../../../archive/engine_v0/eval.cpp"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

struct EvaluationTest {
    string name;
    string fen;
    int expected_sign;
};

int main() {
    init_rays();
    init_magics();

    const vector<EvaluationTest> tests = {
        {"white up a queen", "4k3/8/8/8/8/8/8/3QK3 w - - 0 1", 1},
        {"black up a queen", "3qk3/8/8/8/8/8/8/4K3 w - - 0 1", -1},
        {"white extra pawn", "4k3/8/8/8/8/8/P7/4K3 w - - 0 1", 1},
        {"black extra pawn", "4k3/7p/8/8/8/8/8/4K3 w - - 0 1", -1}
    };

    int passed = 0;
    for (const EvaluationTest& test : tests) {
        Board board;
        board.set_fen(test.fen);
        const int score = evaluate(board);
        const bool ok = (score > 0 && test.expected_sign > 0) || (score < 0 && test.expected_sign < 0);
        cout << test.name << ": " << score << " " << (ok ? "PASS" : "FAIL") << "\n";
        passed += ok ? 1 : 0;
    }

    return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
