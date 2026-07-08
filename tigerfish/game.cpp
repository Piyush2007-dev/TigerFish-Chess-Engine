// ══════════════════════════════════════════════════════════════
// game.cpp — Entry point for TigerFish chess engine
//
// Includes the full engine library (chess.cpp) and exposes a
// CLI interface used by server.js via child_process.execFile().
//
// Commands:
//   chess.exe moves "<fen>"          → JSON: board state + legal moves
//   chess.exe make   "<fen>" "<uci>" → plain text: new FEN after move
//   chess.exe best   "<fen>" [depth] → JSON: { "best_move": "e2e4" }
//
// Compile:
//   g++ -O2 -std=c++17 -o chess.exe game.cpp
// ══════════════════════════════════════════════════════════════
































#include "chess.cpp"   
int main(int argc, char* argv[]) {

    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " [moves|make|best] [args...]" << endl;
        return 1;
    }

    string command = argv[1];

    // ── moves: return full board state + legal move list ─────
    if (command == "moves") {
        if (argc < 3) {
            cerr << "Usage: " << argv[0] << " moves \"<fen>\"" << endl;
            return 1;
        }
        string fen = argv[2];
        Board board;
        board.set_fen(fen);

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        // 64-char grid string (a1=index 0, h8=index 63)
        string grid = "";
        char piece_chars[12] = {'P','N','B','R','Q','K','p','n','b','r','q','k'};
        for (int i = 0; i < 64; ++i) {
            Piece p = board.piece_on[i];
            grid += (p == (Piece)0xF) ? '.' : piece_chars[p];
        }

        GameResult result = board.get_game_result();
        string status_str = "ongoing";
        if      (result == GAME_CHECKMATE)              status_str = "checkmate";
        else if (result == GAME_STALEMATE)              status_str = "stalemate";
        else if (result == GAME_FIFTY_MOVE_DRAW)        status_str = "fifty_move";
        else if (result == GAME_SEVENTY_FIVE_MOVE_DRAW) status_str = "seventy_five_move";
        else if (result == GAME_INSUFFICIENT_MATERIAL)  status_str = "insufficient_material";

        cout << "{" << endl;
        cout << "  \"fen\": \""    << board.to_fen() << "\"," << endl;
        cout << "  \"side\": \""   << (board.side_to_move == WHITE ? "white" : "black") << "\"," << endl;
        cout << "  \"status\": \"" << status_str << "\"," << endl;
        cout << "  \"in_check\": " << (board.is_in_check() ? "true" : "false") << "," << endl;
        cout << "  \"grid\": \""   << grid << "\"," << endl;
        cout << "  \"moves\": ["   << endl;
        for (int i = 0; i < moves.size(); ++i) {
            cout << "    \"" << move_to_uci(moves.move_list[i]) << "\"";
            if (i < moves.size() - 1) cout << ",";
            cout << endl;
        }
        cout << "  ]" << endl;
        cout << "}" << endl;
        return 0;
    }

    // ── make: apply a UCI move and return the new FEN ────────
    else if (command == "make") {
        if (argc < 4) {
            cerr << "Usage: " << argv[0] << " make \"<fen>\" \"<uci>\"" << endl;
            return 1;
        }
        string fen      = argv[2];
        string uci_move = argv[3];

        Board board;
        board.set_fen(fen);

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        Move matched_move;
        bool found = false;
        for (int i = 0; i < moves.size(); ++i) {
            if (move_to_uci(moves.move_list[i]) == uci_move) {
                matched_move = moves.move_list[i];
                found = true;
                break;
            }
        }

        if (!found) {
            cerr << "Illegal move: " << uci_move << endl;
            return 1;
        }

        board.make_move(matched_move, moves);

        // Return full state JSON (same as "moves") so server gets updated grid too
        MoveList new_moves;
        mg.generate_moves(board, new_moves);

        string grid = "";
        char piece_chars[12] = {'P','N','B','R','Q','K','p','n','b','r','q','k'};
        for (int i = 0; i < 64; ++i) {
            Piece p = board.piece_on[i];
            grid += (p == (Piece)0xF) ? '.' : piece_chars[p];
        }

        GameResult result = board.get_game_result();
        string status_str = "ongoing";
        if      (result == GAME_CHECKMATE)              status_str = "checkmate";
        else if (result == GAME_STALEMATE)              status_str = "stalemate";
        else if (result == GAME_FIFTY_MOVE_DRAW)        status_str = "fifty_move";
        else if (result == GAME_SEVENTY_FIVE_MOVE_DRAW) status_str = "seventy_five_move";
        else if (result == GAME_INSUFFICIENT_MATERIAL)  status_str = "insufficient_material";

        cout << "{" << endl;
        cout << "  \"fen\": \""    << board.to_fen() << "\"," << endl;
        cout << "  \"side\": \""   << (board.side_to_move == WHITE ? "white" : "black") << "\"," << endl;
        cout << "  \"status\": \"" << status_str << "\"," << endl;
        cout << "  \"in_check\": " << (board.is_in_check() ? "true" : "false") << "," << endl;
        cout << "  \"grid\": \""   << grid << "\"," << endl;
        cout << "  \"moves\": ["   << endl;
        for (int i = 0; i < new_moves.size(); ++i) {
            cout << "    \"" << move_to_uci(new_moves.move_list[i]) << "\"";
            if (i < new_moves.size() - 1) cout << ",";
            cout << endl;
        }
        cout << "  ]" << endl;
        cout << "}" << endl;
        return 0;
    }

    // ── best: run minimax and return the best move ────────────
    else if (command == "best") {
        if (argc < 3) {
            cerr << "Usage: " << argv[0] << " best \"<fen>\" [depth]" << endl;
            return 1;
        }
        string fen = argv[2];
        int depth  = (argc >= 4) ? atoi(argv[3]) : 3;  // default depth 3

        Board board;
        board.set_fen(fen);

        Engine engine;
        Move best = engine.best_move(board, depth);

        cout << "{" << endl;
        cout << "  \"best_move\": \"" << move_to_uci(best) << "\"" << endl;
        cout << "}" << endl;
        return 0;
    }

    else {
        cerr << "Unknown command: " << command << endl;
        cerr << "Valid commands: moves, make, best" << endl;
        return 1;
    }
}
