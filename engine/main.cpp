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

inline string result_to_status_str(GameResult result) {
    switch (result) {
        case GAME_CHECKMATE:              return "checkmate";
        case GAME_STALEMATE:              return "stalemate";
        case GAME_FIFTY_MOVE_DRAW:        return "fifty_move";
        case GAME_SEVENTY_FIVE_MOVE_DRAW: return "seventy_five_move";
        case GAME_INSUFFICIENT_MATERIAL:  return "insufficient_material";
        default:                          return "ongoing";
    }
}

inline void print_board_state_json(Board& board, const MoveList& moves, const string& bot_move_uci = "") {
    string grid = "";
    char piece_chars[12] = {'P','N','B','R','Q','K','p','n','b','r','q','k'};
    for (int i = 0; i < 64; ++i) {
        Piece p = board.piece_on[i];
        grid += (p == (Piece)0xF) ? '.' : piece_chars[p];
    }

    GameResult result = get_game_result(board);
    string status_str = result_to_status_str(result);

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
    for (int i = 0; i < moves.size(); ++i) {
        cout << "    \"" << move_to_uci(moves.move_list[i]) << "\"";
        if (i < moves.size() - 1) cout << ",";
        cout << endl;
    }
    cout << "  ]" << endl;
    cout << "}" << endl;
}

int main(int argc, char* argv[]) {
    init_rays();
    init_magics();

    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " [moves|make|best|play|interactive] [args...]" << endl;
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

        print_board_state_json(board, moves);
        return 0;
    }

    // ── apply: apply a UCI move and return new FEN (no search) ──
    else if (command == "apply") {
        if (argc < 4) {
            cerr << "Usage: " << argv[0] << " apply \"<fen>\" \"<uci>\"" << endl;
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

        GameResult result = get_game_result(board);
        string status_str = result_to_status_str(result);

        cout << "{" << endl;
        cout << "  \"fen\": \""    << board.to_fen() << "\"," << endl;
        cout << "  \"status\": \"" << status_str     << "\"" << endl;
        cout << "}" << endl;
        return 0;
    }

    // ── make: apply a UCI move and return the new FEN ────────
    else if (command == "make") {
        if (argc < 4) {
            cerr << "Usage: " << argv[0] << " make \"<fen>\" \"<uci>\" [depth]" << endl;
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

        // Return full state JSON
        MoveList new_moves;
        mg.generate_moves(board, new_moves);
        print_board_state_json(board, new_moves, bot_move_uci);
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

    // ── interactive: persistent engine session (one process per game) ──
    //
    // Protocol (one command per line, responses end with ===READY===):
    //   newgame [fen]       — init board (TT is NOT cleared; it keeps accumulating)
    //   apply <uci_move>    — advance the internal board by one move
    //   best <depth>        — search from current position; also advances board
    //   quit / exit         — terminate
    //
    else if (command == "interactive") {
        // One Engine lives for the whole session → TT persists across all moves
        Engine engine;
        Board  board;
        MoveGenerator mg;
        bool initialized = false;

        string line;
        while (getline(cin, line)) {
            if (line.empty()) continue;

            stringstream ss(line);
            string cmd;
            ss >> cmd;

            // ── quit ────────────────────────────────────────────────
            if (cmd == "quit" || cmd == "exit") {
                break;
            }

            // ── newgame [fen] ────────────────────────────────────────
            else if (cmd == "newgame") {
                string fen;
                getline(ss, fen);
                if (!fen.empty() && fen[0] == ' ') fen = fen.substr(1);
                if (fen.empty()) fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
                board.set_fen(fen);
                // TT intentionally NOT cleared — accumulated knowledge helps
                initialized = true;
                cout << "{\"status\": \"ready\"}" << endl;
                cout << "===READY===" << endl;
            }

            // ── apply <uci_move> ─────────────────────────────────────
            else if (cmd == "apply") {
                string uci_move;
                ss >> uci_move;

                if (!initialized) {
                    cout << "{\"error\": \"No active game. Send 'newgame' first.\"}" << endl;
                    cout << "===READY===" << endl;
                    continue;
                }

                MoveList moves;
                mg.generate_moves(board, moves);

                bool found = false;
                for (int i = 0; i < moves.size(); ++i) {
                    if (move_to_uci(moves.move_list[i]) == uci_move) {
                        board.make_move(moves.move_list[i]);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    cout << "{\"error\": \"Illegal move: " << uci_move << "\"}" << endl;
                } else {
                    GameResult result = get_game_result(board);
                    cout << "{\"fen\": \"" << board.to_fen()
                         << "\", \"status\": \"" << result_to_status_str(result) << "\"}" << endl;
                }
                cout << "===READY===" << endl;
            }

            // ── best <depth> ─────────────────────────────────────────
            else if (cmd == "best") {
                int depth = 7;
                ss >> depth;

                if (!initialized) {
                    cout << "{\"error\": \"No active game. Send 'newgame' first.\"}" << endl;
                    cout << "===READY===" << endl;
                    continue;
                }

                if (get_game_result(board) != GAME_ONGOING) {
                    cout << "{\"best_move\": \"\", \"status\": \"game_over\"}" << endl;
                    cout << "===READY===" << endl;
                    continue;
                }

                uint32_t best = engine.best_move(board, depth);
                string best_uci = "";
                if (best != 0) {
                    best_uci = move_to_uci(best);
                    board.make_move(best);   // advance persistent board with our move
                }

                GameResult result = get_game_result(board);
                cout << "{\"best_move\": \"" << best_uci
                     << "\", \"fen\": \"" << board.to_fen()
                     << "\", \"status\": \"" << result_to_status_str(result) << "\"}" << endl;
                cout << "===READY===" << endl;
            }

            // ── unknown ──────────────────────────────────────────────
            else {
                cout << "{\"error\": \"Unknown command: " << cmd << "\"}" << endl;
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
