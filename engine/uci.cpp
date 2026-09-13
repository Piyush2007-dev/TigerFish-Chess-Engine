#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — UCI PROTOCOL (newengine/uci.cpp)
// Universal Chess Interface (UCI) standard for Arena, CuteChess, GUIs, and bots.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "position.cpp"
#include "movegen.cpp"
#include "tt.cpp"
#include "search.cpp"
#include "opening_book.cpp"

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>

namespace tigerfish {

inline void run_uci_loop() {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

    TranspositionTable tt;
    init_tt(tt, 64); // 64 MB default TT

    static constexpr int MAX_UCI_STATES = 2048;
    Position pos;
    StateInfo states[MAX_UCI_STATES];
    int state_idx = 0;

    std::string start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    set_fen(pos, start_fen, states[0]);

    std::string book_path = "";
    int thread_count = 1;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string command;
        if (!(ss >> command)) continue;

        if (command == "uci") {
            std::cout << "id name TigerFish 2.0 (NewEngine)\n";
            std::cout << "id author TigerFish\n";
            std::cout << "option name Hash type spin default 64 min 1 max 1024\n";
            std::cout << "option name Threads type spin default 1 min 1 max 64\n";
            std::cout << "option name BookPath type string default \"\"\n";
            std::cout << "uciok\n" << std::flush;
        } else if (command == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (command == "setoption") {
            std::string name_token, option_name, value_token, option_val;
            ss >> name_token >> option_name;
            if (ss >> value_token >> option_val) {
                if (option_name == "Hash") {
                    size_t mb = std::stoull(option_val);
                    init_tt(tt, mb);
                } else if (option_name == "Threads") {
                    thread_count = std::stoi(option_val);
                    if (thread_count >= 8 && tt.cluster_count <= (64ULL * 1024 * 1024 / 32)) {
                        init_tt(tt, 256);
                    }
                } else if (option_name == "BookPath") {
                    book_path = option_val;
                }
            }
        } else if (command == "ucinewgame") {
            clear_tt(tt);
            state_idx = 0;
            set_fen(pos, start_fen, states[0]);
        } else if (command == "position") {
            std::string type;
            ss >> type;
            state_idx = 0;

            if (type == "startpos") {
                set_fen(pos, start_fen, states[0]);
            } else if (type == "fen") {
                std::string fen_part, fen;
                for (int i = 0; i < 6 && ss >> fen_part; ++i) {
                    if (i > 0) fen += " ";
                    fen += fen_part;
                }
                set_fen(pos, fen, states[0]);
            }

            std::string moves_token;
            if (ss >> moves_token && moves_token == "moves") {
                std::string move_str;
                while (ss >> move_str) {
                    MoveList legal_moves;
                    generate_legal_moves(pos, legal_moves);

                    Move matching_move = MOVE_NONE;
                    for (int i = 0; i < legal_moves.size(); ++i) {
                        if (legal_moves[i].to_uci() == move_str) {
                            matching_move = legal_moves[i];
                            break;
                        }
                    }

                    if (!matching_move.is_none()) {
                        if (state_idx + 1 < MAX_UCI_STATES) {
                            state_idx++;
                            make_move(pos, matching_move, states[state_idx]);
                        }
                    }
                }
            }
        } else if (command == "go") {
            SearchLimits limits;
            std::string token;
            int wtime = 0, btime = 0, winc = 0, binc = 0;

            while (ss >> token) {
                if (token == "depth") ss >> limits.depth;
                else if (token == "nodes") ss >> limits.max_nodes;
                else if (token == "movetime") ss >> limits.time_limit_ms;
                else if (token == "wtime") ss >> wtime;
                else if (token == "btime") ss >> btime;
                else if (token == "winc") ss >> winc;
                else if (token == "binc") ss >> binc;
                else if (token == "infinite") limits.infinite = true;
            }

            // Allocate time if game clock was provided
            if (limits.time_limit_ms == 0 && (wtime > 0 || btime > 0)) {
                int my_time = (pos.side_to_move == WHITE) ? wtime : btime;
                int my_inc  = (pos.side_to_move == WHITE) ? winc : binc;
                limits.time_limit_ms = std::max(50, (my_time / 25) + static_cast<int>(my_inc * 0.75));
            }

            if (limits.time_limit_ms > 0) {
                limits.depth = 64;
            }

            limits.thread_count = thread_count;
            if (limits.depth <= 0) limits.depth = 64;

            // Probe opening book first
            Move book_move = probe_opening_book(pos, book_path);
            if (!book_move.is_none()) {
                std::cout << "bestmove " << book_move.to_uci() << "\n" << std::flush;
                continue;
            }

            // Search
            SearchResult res = search_position(pos, limits, tt);
            std::cout << "info depth " << res.depth << " score cp " << res.score
                      << " nodes " << res.nodes << " time " << res.time_ms
                      << " nps " << res.nps << "\n";
            std::cout << "bestmove " << res.best_move.to_uci() << "\n" << std::flush;
        } else if (command == "quit") {
            break;
        }
    }

    free_tt(tt);
}

} // namespace tigerfish
