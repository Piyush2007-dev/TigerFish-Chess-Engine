// ══════════════════════════════════════════════════════════════
// main.cpp — Entry point for TigerFish chess engine
//
// Includes search.cpp (which pulls in rules.cpp, board.cpp,
// magic_lut.cpp, eval_lut.cpp) and exposes CLI for server.js.
//
// Compile:
//   g++ -O2 -std=c++20 -o game.exe engine/main.cpp
// ══════════════════════════════════════════════════════════════
#include "search.cpp"   
int main(int argc, char* argv[]) {
    init_rays();
    init_magics();

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

        GameResult result = get_game_result(board);
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
        cout << "  \"in_check\": " << (is_in_check(board) ? "true" : "false") << "," << endl;
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

        uint32_t matched_move = 0;
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

        board.make_move(matched_move);

        // Auto-run bot move search on the same board if game is ongoing
        string bot_move_uci = "";
        if (get_game_result(board) == GAME_ONGOING) {
            int depth = (argc >= 5) ? atoi(argv[4]) : 6;
            Engine engine;
            uint32_t best = engine.best_move(board, depth);
            if (best != 0) {
                bot_move_uci = move_to_uci(best);
                board.make_move(best);
            }
        }

        // Return full state JSON (same as "moves") so server gets updated grid too
        MoveList new_moves;
        mg.generate_moves(board, new_moves);

        string grid = "";
        char piece_chars[12] = {'P','N','B','R','Q','K','p','n','b','r','q','k'};
        for (int i = 0; i < 64; ++i) {
            Piece p = board.piece_on[i];
            grid += (p == (Piece)0xF) ? '.' : piece_chars[p];
        }

        GameResult result = get_game_result(board);
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
        cout << "  \"in_check\": " << (is_in_check(board) ? "true" : "false") << "," << endl;
        cout << "  \"grid\": \""   << grid << "\"," << endl;
        if (!bot_move_uci.empty()) {
            cout << "  \"bot_move\": \"" << bot_move_uci << "\"," << endl;
        }
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
        int depth  = (argc >= 4) ? atoi(argv[3]) : 6;  // default depth 6

        Board board;
        board.set_fen(fen);

        Engine engine;
        uint32_t best = engine.best_move(board, depth);

        cout << "{" << endl;
        cout << "  \"best_move\": \"" << move_to_uci(best) << "\"" << endl;
        cout << "}" << endl;
        return 0;
    }

    // ── play: interactive terminal chess game loop ─────────────
    else if (command == "play") {
        int depth = (argc >= 3) ? atoi(argv[2]) : 6;
        Board board;
        board.set_fen(board.START_FEN);
        MoveGenerator mg;

        cout << "===========================================" << endl;
        cout << "  TigerFish Chess Engine — Terminal Play   " << endl;
        cout << "  Type moves in UCI format (e.g. e2e4)    " << endl;
        cout << "  Type 'undo' to take back your last turn  " << endl;
        cout << "  Type 'exit' to quit the game             " << endl;
        cout << "===========================================" << endl;
        cout << endl;

        board.print_board();

        while (true) {
            GameResult result = get_game_result(board);
            if (result != GAME_ONGOING) {
                cout << "\nGame Over! ";
                if (result == GAME_CHECKMATE) {
                    cout << "Checkmate! " << (board.side_to_move == WHITE ? "Black wins!" : "White wins!") << endl;
                } else if (result == GAME_STALEMATE) {
                    cout << "Draw by Stalemate." << endl;
                } else if (result == GAME_FIFTY_MOVE_DRAW) {
                    cout << "Draw by 50-move rule." << endl;
                } else if (result == GAME_SEVENTY_FIVE_MOVE_DRAW) {
                    cout << "Draw by 75-move rule." << endl;
                } else if (result == GAME_INSUFFICIENT_MATERIAL) {
                    cout << "Draw by Insufficient Material." << endl;
                }
                break;
            }

            cout << "\nEnter move: ";
            string user_input;
            if (!(cin >> user_input)) break;

            if (user_input == "exit") {
                cout << "Exiting play mode." << endl;
                break;
            }

            if (user_input == "undo") {
                if (board.move_history.size() >= 2) {
                    board.unmake_move();
                    board.unmake_move();
                    cout << "\nUndid last turn." << endl;
                    board.print_board();
                } else if (board.move_history.size() == 1) {
                    board.unmake_move();
                    cout << "\nUndid last move." << endl;
                    board.print_board();
                } else {
                    cout << "No moves to undo." << endl;
                }
                continue;
            }

            MoveList moves;
            mg.generate_moves(board, moves);

            uint32_t matched_move = 0;
            bool found = false;
            for (int i = 0; i < moves.size(); ++i) {
                if (move_to_uci(moves.move_list[i]) == user_input) {
                    matched_move = moves.move_list[i];
                    found = true;
                    break;
                }
            }

            if (!found) {
                cout << "Invalid / Illegal move! Try again." << endl;
                continue;
            }

            board.make_move(matched_move);
            cout << endl;
            board.print_board();

            result = get_game_result(board);
            if (result != GAME_ONGOING) continue;

            cout << "\nTigerFish is thinking (depth " << depth << ")..." << endl;
            Engine engine;
            uint32_t best = engine.best_move(board, depth);
            if (best != 0) {
                cout << "TigerFish plays: " << move_to_uci(best) << endl;
                board.make_move(best);
                cout << endl;
                board.print_board();
            } else {
                cout << "TigerFish has no moves." << endl;
            }
        }
        return 0;
    }

    // ── interactive: persistent stdin/stdout loop for server ─────
    else if (command == "interactive") {
        string line;
        MoveGenerator mg;
        while (getline(cin, line)) {
            if (line.empty()) continue;
            stringstream ss(line);
            string cmd;
            ss >> cmd;
            if (cmd == "exit" || cmd == "quit") {
                break;
            }
            else if (cmd == "moves") {
                string fen;
                getline(ss, fen);
                if (!fen.empty() && fen[0] == ' ') fen = fen.substr(1);
                
                Board board;
                board.set_fen(fen);
                MoveList moves;
                mg.generate_moves(board, moves);
                
                string grid = "";
                char piece_chars[12] = {'P','N','B','R','Q','K','p','n','b','r','q','k'};
                for (int i = 0; i < 64; ++i) {
                    Piece p = board.piece_on[i];
                    grid += (p == (Piece)0xF) ? '.' : piece_chars[p];
                }
                
                GameResult result = get_game_result(board);
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
                cout << "  \"in_check\": " << (is_in_check(board) ? "true" : "false") << "," << endl;
                cout << "  \"grid\": \""   << grid << "\"," << endl;
                cout << "  \"moves\": ["   << endl;
                for (int i = 0; i < moves.size(); ++i) {
                    cout << "    \"" << move_to_uci(moves.move_list[i]) << "\"";
                    if (i < moves.size() - 1) cout << ",";
                    cout << endl;
                }
                cout << "  ]" << endl;
                cout << "}" << endl;
                cout << "===READY===" << endl;
            }
            else if (cmd == "make") {
                string uci_move;
                int depth;
                ss >> uci_move >> depth;
                
                string fen;
                getline(ss, fen);
                if (!fen.empty() && fen[0] == ' ') fen = fen.substr(1);
                
                Board board;
                board.set_fen(fen);
                
                MoveList moves;
                mg.generate_moves(board, moves);
                
                uint32_t matched_move = 0;
                bool found = false;
                for (int i = 0; i < moves.size(); ++i) {
                    if (move_to_uci(moves.move_list[i]) == uci_move) {
                        matched_move = moves.move_list[i];
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    cout << "{\"error\": \"Illegal move: " << uci_move << "\"}" << endl;
                    cout << "===READY===" << endl;
                    continue;
                }
                
                board.make_move(matched_move);
                
                string bot_move_uci = "";
                if (depth > 0 && get_game_result(board) == GAME_ONGOING) {
                    Engine engine;
                    uint32_t best = engine.best_move(board, depth);
                    if (best != 0) {
                        bot_move_uci = move_to_uci(best);
                        board.make_move(best);
                    }
                }
                
                MoveList new_moves;
                mg.generate_moves(board, new_moves);
                
                string grid = "";
                char piece_chars[12] = {'P','N','B','R','Q','K','p','n','b','r','q','k'};
                for (int i = 0; i < 64; ++i) {
                    Piece p = board.piece_on[i];
                    grid += (p == (Piece)0xF) ? '.' : piece_chars[p];
                }
                
                GameResult result = get_game_result(board);
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
                cout << "  \"in_check\": " << (is_in_check(board) ? "true" : "false") << "," << endl;
                cout << "  \"grid\": \""   << grid << "\"," << endl;
                if (!bot_move_uci.empty()) {
                    cout << "  \"bot_move\": \"" << bot_move_uci << "\"," << endl;
                }
                cout << "  \"moves\": ["   << endl;
                for (int i = 0; i < new_moves.size(); ++i) {
                    cout << "    \"" << move_to_uci(new_moves.move_list[i]) << "\"";
                    if (i < new_moves.size() - 1) cout << ",";
                    cout << endl;
                }
                cout << "  ]" << endl;
                cout << "}" << endl;
                cout << "===READY===" << endl;
            }
        }
        return 0;
    }

    else {
        cerr << "Unknown command: " << command << endl;
        cerr << "Valid commands: moves, make, best, play, interactive" << endl;
        return 1;
    }
}
