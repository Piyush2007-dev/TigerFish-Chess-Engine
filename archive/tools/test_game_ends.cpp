#include <iostream>
#include <cassert>
#include <string>
#include "chess.cpp"

using namespace std;

void test_clocks_and_history() {
    cout << "Running test_clocks_and_history()..." << endl;
    Board board;
    board.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    assert(board.halfmove_clock == 0);
    assert(board.fullmove_number == 1);

    MoveGenerator mg;
    MoveList moves;
    mg.generate_moves(board, moves);

    // Find move e2-e4 (double push)
    Move e2e4;
    bool found = false;
    for (int i = 0; i < moves.size(); i++) {
        Move m = moves.move_list[i];
        if (Move::from(m) == 12 && Move::to(m) == 28) {
            e2e4 = m;
            found = true;
            break;
        }
    }
    assert(found);

    board.make_move(e2e4, moves);
    // Pawn move resets halfmove clock
    assert(board.halfmove_clock == 0);
    // White played, so fullmove number remains 1
    assert(board.fullmove_number == 1);

    mg.generate_moves(board, moves);
    // Find move g8-f6 (quiet move by Black)
    Move g8f6;
    found = false;
    for (int i = 0; i < moves.size(); i++) {
        Move m = moves.move_list[i];
        if (Move::from(m) == 62 && Move::to(m) == 45) {
            g8f6 = m;
            found = true;
            break;
        }
    }
    assert(found);

    board.make_move(g8f6, moves);
    // Quiet move increments halfmove clock
    assert(board.halfmove_clock == 1);
    // Black played, so fullmove number increments to 2
    assert(board.fullmove_number == 2);

    // Unmake Black's move
    board.unmake_move();
    assert(board.halfmove_clock == 0);
    assert(board.fullmove_number == 1);

    // Unmake White's move
    board.unmake_move();
    assert(board.halfmove_clock == 0);
    assert(board.fullmove_number == 1);

    cout << "test_clocks_and_history() passed!" << endl;
}

void test_checkmate() {
    cout << "Running test_checkmate()..." << endl;
    Board board;
    // Correct Scholar's Mate FEN (White Queen on f7 defended by White Bishop on c4)
    board.set_fen("r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1KBNR b KQkq - 0 4");
    assert(board.get_game_result() == GAME_CHECKMATE);
    cout << "test_checkmate() passed!" << endl;
}

void test_stalemate() {
    cout << "Running test_stalemate()..." << endl;
    Board board;
    // Classic stalemate FEN: Black king on h8, White queen on f7, White king on h1
    board.set_fen("7k/5Q2/8/8/8/8/8/7K b - - 0 1");
    assert(board.get_game_result() == GAME_STALEMATE);
    cout << "test_stalemate() passed!" << endl;
}

void test_fifty_seventy_five_move_rule() {
    cout << "Running test_fifty_seventy_five_move_rule()..." << endl;
    Board board;
    // Play on startpos with halfmove_clock = 99
    board.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 99 1");
    assert(board.get_game_result() == GAME_ONGOING);

    MoveGenerator mg;
    MoveList moves;
    mg.generate_moves(board, moves);

    // Find a quiet move: g1-f3
    Move g1f3;
    bool found = false;
    for (int i = 0; i < moves.size(); i++) {
        Move m = moves.move_list[i];
        if (Move::from(m) == 6 && Move::to(m) == 21) {
            g1f3 = m;
            found = true;
            break;
        }
    }
    assert(found);

    board.make_move(g1f3, moves);
    assert(board.halfmove_clock == 100);
    assert(board.get_game_result() == GAME_FIFTY_MOVE_DRAW);

    // Set FEN with halfmove_clock = 149
    board.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 149 1");
    mg.generate_moves(board, moves);
    board.make_move(g1f3, moves);
    assert(board.halfmove_clock == 150);
    assert(board.get_game_result() == GAME_SEVENTY_FIVE_MOVE_DRAW);

    cout << "test_fifty_seventy_five_move_rule() passed!" << endl;
}

void test_insufficient_material() {
    cout << "Running test_insufficient_material()..." << endl;
    Board board;

    // King vs King
    board.set_fen("k7/8/8/8/8/8/8/K7 w - - 0 1");
    assert(board.get_game_result() == GAME_INSUFFICIENT_MATERIAL);

    // King + Bishop vs King
    board.set_fen("k7/8/8/8/8/8/8/K1B5 w - - 0 1");
    assert(board.get_game_result() == GAME_INSUFFICIENT_MATERIAL);

    // King + Knight vs King
    board.set_fen("k7/8/8/8/8/8/8/K1N5 w - - 0 1");
    assert(board.get_game_result() == GAME_INSUFFICIENT_MATERIAL);

    // King + Bishop vs King + Bishop (same color squares: c1 dark, f8 dark)
    board.set_fen("k4b2/8/8/8/8/8/8/2B4K w - - 0 1");
    assert(board.get_game_result() == GAME_INSUFFICIENT_MATERIAL);

    // King + Bishop vs King + Bishop (different color squares: c1 dark, e8 light)
    board.set_fen("k3b3/8/8/8/8/8/8/2B4K w - - 0 1");
    assert(board.get_game_result() == GAME_ONGOING);

    // With a pawn (not insufficient material)
    board.set_fen("k7/8/8/8/8/8/P7/K7 w - - 0 1");
    assert(board.get_game_result() == GAME_ONGOING);

    cout << "test_insufficient_material() passed!" << endl;
}

int main() {
    cout << "========================================" << endl;
    cout << "Starting Chess Engine Game End Tests" << endl;
    cout << "========================================" << endl;

    test_clocks_and_history();
    test_checkmate();
    test_stalemate();
    test_fifty_seventy_five_move_rule();
    test_insufficient_material();

    cout << "========================================" << endl;
    cout << "All Game End Tests PASSED!" << endl;
    cout << "========================================" << endl;
    return 0;
}
