#include "../../engine/eval.cpp"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

struct PositionCase {
    const char* name;
    const char* fen;
};

int main() {
    init_rays();
    init_magics();

    const vector<PositionCase> positions = {
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"middlegame", "r1bq1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/5N2/PPP2PPP/RNBQ1RK1 w - - 0 7"},
        {"endgame", "8/4k3/8/3KP3/8/8/8/8 w - - 0 1"},
        {"castling", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"},
        {"en-passant", "4k3/8/8/2pP4/8/8/8/4K3 w - c6 0 1"},
        {"promotion", "4k3/P7/1K6/8/8/8/8/8 w - - 0 1"}
    };

    int checks = 0;
    int failures = 0;

    for (const PositionCase& position : positions) {
        Board board;
        if (!board.set_fen(position.fen)) {
            cerr << "INVALID TEST FEN: " << position.name << '\n';
            return 1;
        }

        const int original_score = evaluate(board);
        MoveList moves;
        MoveGenerator generator;
        generator.generate_moves(board, moves);

        for (int i = 0; i < moves.size(); ++i) {
            const uint32_t move = moves.move_list[i];
            const string move_name = move_to_uci(move);

            board.make_move(move);
            const int incremental_score = evaluate(board);

            Board rebuilt;
            if (!rebuilt.set_fen(board.to_fen())) {
                cerr << "REBUILD FAILED: " << position.name << ' ' << move_name << '\n';
                failures++;
            } else {
                const int rebuilt_score = evaluate(rebuilt);
                checks++;
                if (incremental_score != rebuilt_score) {
                    cerr << "MISMATCH: " << position.name << ' ' << move_name
                         << " incremental=" << incremental_score
                         << " rebuilt=" << rebuilt_score << '\n';
                    failures++;
                }
            }

            board.unmake_move();
            checks++;
            if (evaluate(board) != original_score) {
                cerr << "UNMAKE MISMATCH: " << position.name << ' ' << move_name
                     << " original=" << original_score
                     << " restored=" << evaluate(board) << '\n';
                failures++;
            }
        }
    }

    cout << "incremental evaluation checks: " << checks
         << ", failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
