/*
    perft_split.cpp
    Compile: g++ -O3 -std=c++20 perft_split.cpp -o perft_split.exe

    Usage (called by compare_perft.py):
        ./perft_split.exe "startpos" 3
        ./perft_split.exe "rnbqkbnr/pppppppp/8/8/8/7P/PPPPPPP1/RNBQKBNR b KQkq - 0 1" 2
        ./perft_split.exe "startpos" 3 h2h3
        ./perft_split.exe "startpos" 3 h2h3 e7e5

    Output format (one line per move):
        a2a3: 380
        b2b3: 420
        ...
        Total: 8902
*/

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include "chess.cpp"   // your engine header

using namespace std;
using namespace std::chrono;

MoveGenerator mg;

// ── convert "e2e4" string to a Move on the given board ────────────────────
// Finds the matching move in the generated move list.
Move parse_move(Board &board, const string &uci) {
    MoveList moves;
    mg.generate_moves(board, moves);

    char f[] = "abcdefgh";

    for (int i = 0; i < moves.size(); i++) {
        Move m = moves.move_list[i];
        int from = Move::from(m);
        int to   = Move::to(m);

        string candidate = "";
        candidate += f[from % 8];
        candidate += char('1' + from / 8);
        candidate += f[to   % 8];
        candidate += char('1' + to   / 8);

        if (Move::is_promotion(m)) {
            // append promotion piece char (lowercase)
            const char promo_chars[] = {'p','n','b','r','q','k','p','n','b','r','q','k'};
            candidate += promo_chars[Move::promotion_piece(m)];
        }

        if (candidate == uci)
            return m;
    }

    cerr << "ERROR: move '" << uci << "' not found in position\n";
    exit(1);
}

// ── recursive perft (no bulk counting) ────────────────────────────────────
uint64_t perft(Board &board, int depth) {
    if (depth == 0) return 1ULL;

    MoveList moves;
    mg.generate_moves(board, moves);

    uint64_t nodes = 0;
    for (int i = 0; i < moves.size(); i++) {
        Board copy = board;
        MoveList dummy;
        copy.make_move(moves.move_list[i], dummy);
        nodes += perft(copy, depth - 1);
    }
    return nodes;
}

// ── split perft: print each root move + subtree count ─────────────────────
void perft_split(Board &board, int depth) {
    MoveList moves;
    mg.generate_moves(board, moves);

    char f[] = "abcdefgh";
    const char promo_chars[] = {'p','n','b','r','q','k','p','n','b','r','q','k'};

    uint64_t total = 0;

    for (int i = 0; i < moves.size(); i++) {
        Move m = moves.move_list[i];
        int from = Move::from(m);
        int to   = Move::to(m);

        string move_str = "";
        move_str += f[from % 8];
        move_str += char('1' + from / 8);
        move_str += f[to   % 8];
        move_str += char('1' + to   / 8);
        if (Move::is_promotion(m))
            move_str += promo_chars[Move::promotion_piece(m)];

        Board copy = board;
        MoveList dummy;
        copy.make_move(m, dummy);

        uint64_t count = perft(copy, depth - 1);
        uint64_t prev = total;
        total += count;
        uint64_t curr = total;

        uint64_t prev_boundary = prev / 100000000ULL;
        uint64_t curr_boundary = curr / 100000000ULL;
        if (curr_boundary > prev_boundary) {
            cout << "[Progress] Completed " << (curr_boundary * 100ULL) << " million nodes...\n";
            cout.flush();
        }

        cout << move_str << ": " << count << "\n";
    }

    cout << "\nTotal: " << total << "\n";
}

// ── main ──────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    init_rays();
    init_magics();
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <fen|startpos> <depth> [move1 move2 ...]\n";
        return 1;
    }

    string fen_arg = argv[1];
    int depth      = stoi(argv[2]);

    Board board;

    // set position
    if (fen_arg == "startpos") {
        board.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    } else {
        board.set_fen(fen_arg);
    }

    // apply any moves passed as extra arguments
    for (int i = 3; i < argc; i++) {
        string uci = argv[i];
        Move m = parse_move(board, uci);
        MoveList dummy;
        board.make_move(m, dummy);
    }

    perft_split(board, depth);

    return 0;
}