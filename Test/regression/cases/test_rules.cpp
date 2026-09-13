// ═══ BASIC RULES TESTS ══════════════════════════════════════════
// Tests legal move generation: checkmate, stalemate, EP, castling, etc.
// Compile: g++ -O2 -std=c++20 -I engine -o rules.exe test_rules.cpp
// Run:     ./rules.exe

#include "../../../archive/engine_v0/rules.cpp"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

struct RuleTest {
    string name;
    string fen;
    int expected_moves;
    GameResult expected_result;
};

bool contains_move(const MoveList& moves, const string& uci) {
    for (int i = 0; i < moves.size(); ++i) {
        if (move_to_uci(moves.move_list[i]) == uci) return true;
    }
    return false;
}

bool verify_make_unmake(Board& board, const MoveList& moves) {
    const string original_fen = board.to_fen();
    const uint64_t original_hash = board.zobrist_hash;

    for (int i = 0; i < moves.size(); ++i) {
        board.make_move(moves.move_list[i]);
        board.unmake_move();
        if (board.to_fen() != original_fen || board.zobrist_hash != original_hash ||
            board.zobrist_hash != board.compute_zobrist_hash()) {
            return false;
        }
    }
    return true;
}

int main() {
    init_rays();
    init_magics();

    const vector<RuleTest> tests = {
        {"initial legal moves", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 20, GAME_ONGOING},
        {"checkmate", "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1", 0, GAME_CHECKMATE},
        {"stalemate", "7k/5Q2/7K/8/8/8/8/8 b - - 0 1", 0, GAME_STALEMATE},
        {"en passant available", "8/8/8/3pP3/8/8/8/4K2k w - d6 0 1", 7, GAME_ONGOING},
        {"promotion choices", "7k/P7/8/8/8/8/8/4K3 w - - 0 1", 9, GAME_ONGOING},
        {"double check only king moves", "4k3/8/8/8/1b6/8/4r3/4K3 w - - 0 1", 3, GAME_ONGOING}
    };

    int passed = 0;
    for (const RuleTest& test : tests) {
        Board board;
        board.set_fen(test.fen);
        MoveList moves;
        MoveGenerator generator;
        generator.generate_moves(board, moves);

        bool ok = moves.size() == test.expected_moves && get_game_result(board) == test.expected_result &&
                  verify_make_unmake(board, moves);
        if (test.name == "en passant available") ok = ok && contains_move(moves, "e5d6");
        if (test.name == "promotion choices") {
            ok = ok && contains_move(moves, "a7a8q") && contains_move(moves, "a7a8r") &&
                 contains_move(moves, "a7a8b") && contains_move(moves, "a7a8n");
        }
        if (test.name == "double check only king moves") {
            for (int i = 0; i < moves.size(); ++i) {
                ok = ok && board.piece_on[move_from(moves.move_list[i])] == Piece::K;
            }
        }

        cout << test.name << ": " << (ok ? "PASS" : "FAIL") << "\n";
        passed += ok ? 1 : 0;
    }

    Board castling_board;
    castling_board.set_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    MoveList castling_moves;
    MoveGenerator generator;
    generator.generate_moves(castling_board, castling_moves);
    const bool castling_ok = contains_move(castling_moves, "e1g1") && contains_move(castling_moves, "e1c1");
    cout << "castling: " << (castling_ok ? "PASS" : "FAIL") << "\n";
    passed += castling_ok ? 1 : 0;

    Board pinned_board;
    pinned_board.set_fen("4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1");
    MoveList pinned_moves;
    generator.generate_moves(pinned_board, pinned_moves);
    const bool pin_ok = !contains_move(pinned_moves, "e2d2") && contains_move(pinned_moves, "e2e8");
    cout << "pinned piece: " << (pin_ok ? "PASS" : "FAIL") << "\n";
    passed += pin_ok ? 1 : 0;

    return passed == static_cast<int>(tests.size()) + 2 ? 0 : 1;
}
