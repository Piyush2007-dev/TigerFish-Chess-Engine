#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — CLI & JSON PROTOCOL (newengine/cli.cpp)
// 100% compatible with existing Lichess bots, web backend, analyze, bench, perft.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "position.cpp"
#include "movegen.cpp"
#include "tt.cpp"
#include "search.cpp"
#include "opening_book.cpp"
#include "perft.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>

namespace tigerfish {

// ── Game Result Helper ───────────────────────────────────────────────────────
inline std::string position_status_string(const Position& pos) {
    MoveList moves;
    generate_legal_moves(pos, moves);

    if (moves.empty()) {
        if (pos.state->checkers != 0) {
            return (pos.side_to_move == WHITE) ? "checkmate_black_wins" : "checkmate_white_wins";
        }
        return "stalemate";
    }

    if (is_fifty_moves(pos)) return "fifty_move_draw";
    if (is_repetition(pos, 3)) return "threefold_repetition";
    if (is_insufficient_material(pos)) return "insufficient_material";

    return "ongoing";
}

// ── Interactive Subprocess Protocol (Used by lichess_bot.py) ─────────────────
inline int run_interactive_mode() {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

    TranspositionTable tt;
    init_tt(tt, 32); // 32 MB TT for persistent match play

    static constexpr int MAX_CLI_STATES = 2048;
    Position pos;
    StateInfo states[MAX_CLI_STATES];
    Move move_history[MAX_CLI_STATES];
    int state_idx = 0;

    std::string start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    set_fen(pos, start_fen, states[0]);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command == "newgame") {
            clear_tt(tt);
            state_idx = 0;
            std::string remaining;
            std::getline(input, remaining);
            size_t first = remaining.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                start_fen = remaining.substr(first);
                size_t last = start_fen.find_last_not_of(" \t\r\n");
                if (last != std::string::npos) start_fen = start_fen.substr(0, last + 1);
            } else {
                start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
            }
            set_fen(pos, start_fen, states[0]);
            std::cout << "{\"status\": \"ready\"}\n===READY===\n" << std::flush;
        } else if (command == "apply") {
            std::string uci_move;
            input >> uci_move;

            MoveList moves;
            generate_legal_moves(pos, moves);
            Move target = MOVE_NONE;

            for (int i = 0; i < moves.size(); ++i) {
                if (moves[i].to_uci() == uci_move) {
                    target = moves[i];
                    break;
                }
            }

            if (target.is_none()) {
                std::cout << "{\"error\": \"illegal_move: " << uci_move << "\"}\n===READY===\n" << std::flush;
            } else {
                if (state_idx + 1 < MAX_CLI_STATES) {
                    state_idx++;
                    move_history[state_idx] = target;
                    make_move(pos, target, states[state_idx]);
                    std::cout << "{\"status\": \"ok\", \"fen\": \"" << get_fen(pos) << "\"}\n===READY===\n" << std::flush;
                } else {
                    std::cout << "{\"error\": \"max_moves_exceeded\"}\n===READY===\n" << std::flush;
                }
            }
        } else if (command == "best") {
            int depth = 7;
            input >> depth;
            int time_ms = 0;
            input >> time_ms;

            std::string status = position_status_string(pos);
            if (status != "ongoing") {
                std::cout << "{\"best_move\": \"\", \"status\": \"" << status << "\"}\n===READY===\n" << std::flush;
            } else {
                SearchLimits limits;
                limits.depth = (depth > 0) ? depth : 7;
                limits.time_limit_ms = (time_ms > 0) ? time_ms : 0;

                SearchResult res = search_position(pos, limits, tt);
                Move chosen_move = res.best_move;

                if (!chosen_move.is_none()) {
                    if (state_idx + 1 < MAX_CLI_STATES) {
                        state_idx++;
                        move_history[state_idx] = chosen_move;
                        make_move(pos, chosen_move, states[state_idx]);
                        std::string new_status = position_status_string(pos);
                        std::cout << "{\"best_move\": \"" << chosen_move.to_uci()
                                  << "\", \"status\": \"" << new_status
                                  << "\", \"fen\": \"" << get_fen(pos) << "\"}\n===READY===\n" << std::flush;
                    } else {
                        std::cout << "{\"error\": \"max_moves_exceeded\"}\n===READY===\n" << std::flush;
                    }
                } else {
                    std::cout << "{\"best_move\": \"\", \"status\": \"" << status << "\"}\n===READY===\n" << std::flush;
                }
            }
        } else if (command == "fen") {
            std::cout << "{\"fen\": \"" << get_fen(pos) << "\"}\n===READY===\n" << std::flush;
        } else if (command == "status") {
            std::cout << "{\"status\": \"" << position_status_string(pos) << "\"}\n===READY===\n" << std::flush;
        } else if (command == "eval") {
            int score = evaluate(pos);
            std::cout << "{\"eval\": " << std::fixed << std::setprecision(2) << (score / 100.0f) << "}\n===READY===\n" << std::flush;
        } else if (command == "undo") {
            if (state_idx > 0) {
                undo_move(pos, move_history[state_idx]);
                state_idx--;
                std::cout << "{\"status\": \"ok\", \"fen\": \"" << get_fen(pos) << "\"}\n===READY===\n" << std::flush;
            } else {
                std::cout << "{\"error\": \"no_moves_to_undo\"}\n===READY===\n" << std::flush;
            }
        } else if (command == "quit" || command == "exit") {
            break;
        } else {
            std::cout << "{\"error\": \"unknown_command\"}\n===READY===\n" << std::flush;
        }
    }

    free_tt(tt);
    return 0;
}

// ── Analyze Command (Outputs Full Structured JSON) ───────────────────────────
inline int run_analyze_command(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " analyze \"<fen>\" [depth] [--threads N] [--nodes N] [--movetime N]\n";
        return 1;
    }

    init_bitboards();
    init_zobrist();
    init_pst_tables();

    std::string fen = argv[2];
    Position pos;
    StateInfo state;
    if (!set_fen(pos, fen, state)) {
        std::cerr << "Error: invalid FEN string\n";
        return 1;
    }

    SearchLimits limits;
    limits.depth = (argc >= 4 && argv[3][0] != '-') ? std::stoi(argv[3]) : 10;

    int tt_size = 64;
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--depth" && i + 1 < argc) limits.depth = std::stoi(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) limits.thread_count = std::stoi(argv[++i]);
        else if (arg == "--nodes" && i + 1 < argc) limits.max_nodes = std::stoull(argv[++i]);
        else if ((arg == "--movetime" || arg == "--time") && i + 1 < argc) limits.time_limit_ms = std::stoi(argv[++i]);
        else if (arg == "--hash" && i + 1 < argc) tt_size = std::stoi(argv[++i]);
    }

    if (tt_size == 64 && limits.thread_count >= 8) {
        tt_size = 256; // Prevent TT cache thrashing under high concurrency
    }

    TranspositionTable tt;
    init_tt(tt, tt_size);

    SearchResult res = search_position(pos, limits, tt);

    float eval_val = is_mate_score(res.score)
        ? (res.score > 0 ? (100.0f - mate_in_plies(res.score)) : (-100.0f + mate_in_plies(res.score)))
        : (static_cast<float>(res.score) / 100.0f);

    std::cout << "{\n";
    std::cout << "  \"bestMove\": \"" << res.best_move.to_uci() << "\",\n";
    std::cout << "  \"evaluation\": " << std::fixed << std::setprecision(2) << eval_val << ",\n";
    std::cout << "  \"evalCp\": " << res.score << ",\n";
    std::cout << "  \"depth\": " << res.depth << ",\n";
    std::cout << "  \"nodes\": " << res.nodes << ",\n";
    std::cout << "  \"timeMs\": " << res.time_ms << ",\n";
    std::cout << "  \"nps\": " << res.nps << ",\n";
    std::cout << "  \"ttHits\": " << res.tt_hits << ",\n";
    std::cout << "  \"ttMisses\": " << res.tt_misses << ",\n";
    std::cout << "  \"threads\": " << limits.thread_count << ",\n";
    std::cout << "  \"pv\": [";
    for (size_t i = 0; i < res.pv.size(); ++i) {
        std::cout << "\"" << res.pv[i].to_uci() << "\"" << (i + 1 < res.pv.size() ? ", " : "");
    }
    std::cout << "]\n";
    std::cout << "}\n" << std::flush;

    free_tt(tt);
    return 0;
}

// ── Best Move Command ────────────────────────────────────────────────────────
inline int run_best_command(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " best \"<fen>\" [depth] [time_ms]\n";
        return 1;
    }

    init_bitboards();
    init_zobrist();
    init_pst_tables();

    std::string fen = argv[2];
    Position pos;
    StateInfo state;
    if (!set_fen(pos, fen, state)) {
        std::cerr << "Error: invalid FEN string\n";
        return 1;
    }

    SearchLimits limits;
    limits.depth = (argc >= 4) ? std::stoi(argv[3]) : 10;
    limits.time_limit_ms = (argc >= 5) ? std::stoi(argv[4]) : 0;

    TranspositionTable tt;
    init_tt(tt, 32);

    SearchResult res = search_position(pos, limits, tt);
    std::cout << "{\"best_move\": \"" << res.best_move.to_uci() << "\"}\n" << std::flush;

    free_tt(tt);
    return 0;
}

// ── Perft Command ────────────────────────────────────────────────────────────
inline int run_perft_command(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " perft [bench | \"<fen>\" <depth> [--threads N] [--split] [--tt]]\n";
        return 1;
    }

    init_bitboards();
    init_zobrist();
    init_pst_tables();

    std::string arg2 = argv[2];
    if (arg2 == "bench") {
        run_perft_benchmark_suite();
        return 0;
    }

    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " perft \"<fen>\" <depth> [--threads N] [--split] [--tt]\n";
        return 1;
    }

    std::string fen = arg2;
    if (fen == "startpos") {
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    } else if (fen == "kiwipete") {
        fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    }

    int depth = std::stoi(argv[3]);
    int threads = 1; // default to 1 for backward compatibility
    int split_depth = 0; // 0 = auto default
    bool split = false;
    bool use_tt = false;
    bool stream = false;

    size_t tt_size_mb = 256;

    for (int i = 4; i < argc; ++i) {
        std::string opt = argv[i];
        if (opt == "--threads" && i + 1 < argc) {
            threads = std::stoi(argv[++i]);
        } else if (opt == "--split") {
            split = true;
        } else if (opt == "--tt") {
            use_tt = true;
        } else if (opt == "--stream") {
            stream = true;
        } else if (opt == "--tt-size" && i + 1 < argc) {
            tt_size_mb = std::stoull(argv[++i]);
        } else if ((opt == "--split-depth" || opt == "--work-depth" || opt == "--work-size" || opt == "-w") && i + 1 < argc) {
            split_depth = std::stoi(argv[++i]);
        }
    }

    Position pos;
    StateInfo state;
    if (!set_fen(pos, fen, state)) {
        std::cerr << "Error: invalid FEN string\n";
        return 1;
    }

    PerftResult res = run_perft(pos, depth, threads, use_tt, split, tt_size_mb, stream, split_depth);

    if (split) {
        for (const auto& sm : res.split_results) {
            std::cout << sm.first << ": " << sm.second << "\n";
        }
    }

    std::cout << "Perft depth " << depth << ": " << res.total_nodes << " nodes in "
              << res.elapsed_ms << " ms (" << static_cast<uint64_t>(res.nps) << " NPS)\n" << std::flush;
    return 0;
}

// ── Bench Command ────────────────────────────────────────────────────────────
inline int run_bench_command(int argc, char* argv[]) {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

    int depth = 12;
    int threads = 1;
    bool root_parallel = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--ybwc" || arg == "--root" || arg == "--root-parallel") {
            root_parallel = true;
        } else if (i == 2 && arg.find_first_not_of("0123456789") == std::string::npos) {
            depth = std::stoi(arg);
        } else if (i == 3 && arg.find_first_not_of("0123456789") == std::string::npos) {
            threads = std::stoi(arg);
        }
    }

    const std::vector<std::string> BENCH_POSITIONS = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r1bqkb1r/pppp1ppp/2n5/4p3/2B1n3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 5",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"
    };

    TranspositionTable tt;
    init_tt(tt, 64);

    uint64_t total_nodes = 0;
    auto t0 = std::chrono::high_resolution_clock::now();

    std::cout << "TigerFish Benchmark (depth " << depth << " across " << BENCH_POSITIONS.size()
              << " positions, " << threads << " thread" << (threads > 1 ? "s" : "")
              << (root_parallel ? ", Root-Parallel YBWC" : ", Lazy SMP") << ")...\n";

    for (size_t i = 0; i < BENCH_POSITIONS.size(); ++i) {
        Position pos;
        StateInfo state;
        set_fen(pos, BENCH_POSITIONS[i], state);

        SearchLimits limits;
        limits.depth = depth;
        limits.thread_count = threads;
        limits.root_parallel = root_parallel;

        SearchResult res = search_position(pos, limits, tt);
        total_nodes += res.nodes;
        std::cout << "  Pos #" << (i + 1) << ": move " << res.best_move.to_uci()
                  << " score " << res.score << " nodes " << res.nodes << "\n";
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
    uint64_t overall_nps = (elapsed_sec > 0) ? static_cast<uint64_t>(total_nodes / elapsed_sec) : total_nodes;

    std::cout << "========================================================\n";
    std::cout << "Total Nodes: " << total_nodes << "\n";
    std::cout << "Total Time:  " << elapsed_sec << " s\n";
    std::cout << "Overall NPS: " << overall_nps << "\n";
    std::cout << "========================================================\n" << std::flush;

    free_tt(tt);
    return 0;
}

} // namespace tigerfish
