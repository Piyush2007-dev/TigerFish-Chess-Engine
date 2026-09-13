#include <iostream>
#include <chrono>
#include "chess.cpp"

using namespace std;
using namespace std::chrono;

MoveGenerator mg;

// ── piece name helper ──────────────────────────────────────────────────────
const char* piece_name(int p) {
    switch(p) {
        case 0:  return "P(white pawn)";
        case 1:  return "N(white knight)";
        case 2:  return "B(white bishop)";
        case 3:  return "R(white rook)";
        case 4:  return "Q(white queen)";
        case 5:  return "K(white king)";
        case 6:  return "p(black pawn)";
        case 7:  return "n(black knight)";
        case 8:  return "b(black bishop)";
        case 9:  return "r(black rook)";
        case 10: return "q(black queen)";
        case 11: return "k(black king)";
        default: return "UNKNOWN";
    }
}

// ── perft (no bulk counting) ───────────────────────────────────────────────
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

// ── perft test ─────────────────────────────────────────────────────────────
void perft_test(Board &board, int max_depth) {
    for (int depth = 1; depth <= max_depth; depth++) {
        auto start = high_resolution_clock::now();
        uint64_t nodes = perft(board, depth);
        auto end = high_resolution_clock::now();
        auto ms = duration_cast<milliseconds>(end - start).count();
        cout << "Perft(" << depth << ") = " << nodes << "   [" << ms << " ms]\n";
    }
}

// ── split perft ────────────────────────────────────────────────────────────
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

        string mv = "";
        mv += f[from % 8];
        mv += char('1' + from / 8);
        mv += f[to   % 8];
        mv += char('1' + to   / 8);
        if (Move::is_promotion(m))
            mv += promo_chars[Move::promotion_piece(m)];

        Board copy = board;
        MoveList dummy;
        copy.make_move(m, dummy);
        uint64_t count = perft(copy, depth - 1);
        total += count;

        cout << mv << ": " << count << "\n";
    }
    cout << "\nTotal: " << total << "\n";
}

// ── debug: print every move with full details ──────────────────────────────
void debug_movelist(Board &board) {
    MoveList moves;
    mg.generate_moves(board, moves);

    char f[] = "abcdefgh";
    const char promo_chars[] = {'p','n','b','r','q','k','p','n','b','r','q','k'};

    cout << "\n=== Move list (" << moves.size() << " moves) ===\n";
    cout << "  #   move    piece                from_sq  to_sq  flags\n";
    cout << "  -   ----    -----                -------  -----  -----\n";

    for (int i = 0; i < moves.size(); i++) {
        Move m = moves.move_list[i];
        int from = Move::from(m);
        int to   = Move::to(m);
        int pc   = (int)Move::piece(m);

        string mv = "";
        mv += f[from % 8];
        mv += char('1' + from / 8);
        mv += f[to   % 8];
        mv += char('1' + to   / 8);
        if (Move::is_promotion(m))
            mv += promo_chars[Move::promotion_piece(m)];

        // flags
        string flags = "";
        if (Move::is_capture(m))    flags += "CAP ";
        if (Move::is_promotion(m))  flags += "PROMO ";
        if (Move::is_castle(m))     flags += "CASTLE ";
        if (Move::is_en_passant(m)) flags += "EP ";
        if (Move::is_double_push(m))flags += "DOUBLE ";
        if (flags.empty()) flags = "-";

        cout << "  " << i+1 << "\t"
             << mv << "\t"
             << piece_name(pc) << "\t"
             << "from=" << from << "(" << f[from%8] << (from/8+1) << ")"
             << "  to=" << to   << "(" << f[to%8]   << (to/8+1)   << ")"
             << "  " << flags
             << "\n";
    }

    cout << "\n";

    // also print the board so we can verify piece positions
    board.print_board();
    cout << "\n";

    // verify: for each move, check the from_square actually has the right piece
    cout << "=== Sanity check: from_square must have the piece ===\n";
    bool found_bug = false;
    for (int i = 0; i < moves.size(); i++) {
        Move m = moves.move_list[i];
        int from = Move::from(m);
        int pc   = (int)Move::piece(m);

        bool has_piece = (board.bitboards[pc] >> from) & 1ULL;
        if (!has_piece) {
            string mv = "";
            mv += f[from % 8];
            mv += char('1' + from / 8);
            mv += f[Move::to(m) % 8];
            mv += char('1' + Move::to(m) / 8);

            cout << "*** BUG FOUND: move " << mv
                 << " claims piece=" << piece_name(pc)
                 << " on square " << from
                 << " (" << f[from%8] << (from/8+1) << ")"
                 << " but that square is EMPTY or has a different piece!\n";
            found_bug = true;
        }
    }
    if (!found_bug)
        cout << "All from_squares are valid.\n";
    cout << "\n";
}

// ── main ──────────────────────────────────────────────────────────────────
int main() {
    init_rays();
    init_magics();
    Board board;

    // ── Test 1: starting position perft ───────────────────────────────────
    cout << "=== Starting Position Perft ===\n";
    perft_test(board, 6);
    cout << "\n";

    // ── Test 2: debug the position after h2h3 ─────────────────────────────
    cout << "=== Position after h2h3 (black to move) ===\n";
    board.set_fen("rnbqkbnr/pppppppp/8/8/8/7P/PPPPPPP1/RNBQKBNR b KQkq - 0 1");

    // full debug output — shows every move with piece + from_sq + sanity check
    debug_movelist(board);

    // ── Test 3: split perft at depth 1 in this position ───────────────────
    cout << "=== Split perft depth 1 (compare against stockfish) ===\n";
    cout << "Stockfish command: position fen rnbqkbnr/pppppppp/8/8/8/7P/PPPPPPP1/RNBQKBNR b KQkq - 0 1\n";
    cout << "                   go perft 1\n\n";
    perft_split(board, 1);

    return 0;
}