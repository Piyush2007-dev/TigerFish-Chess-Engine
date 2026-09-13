#include "search.cpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>

struct SearchOptions {
    int depth_limit = 5;
    uint64_t nodes_limit = 0;
    int time_limit_ms = 0;
    int multi_pv = 1;
    int thread_count = 1;
    std::atomic<bool>* stop_flag = nullptr;
};

struct AnalysisResult {
    std::string best_move;
    float evaluation = 0.0f;
    int eval_cp = 0;
    int depth = 0;
    uint64_t nodes = 0;
    int time_ms = 0;
    uint64_t nps = 0;
    uint64_t tt_hits = 0;
    uint64_t tt_misses = 0;
    int thread_count = 1;
    std::vector<std::string> pv;

    std::string to_json() const {
        std::ostringstream ss;
        ss << "{\n";
        ss << "  \"bestMove\": \"" << best_move << "\",\n";
        ss << "  \"evaluation\": " << std::fixed << std::setprecision(2) << evaluation << ",\n";
        ss << "  \"evalCp\": " << eval_cp << ",\n";
        ss << "  \"depth\": " << depth << ",\n";
        ss << "  \"nodes\": " << nodes << ",\n";
        ss << "  \"timeMs\": " << time_ms << ",\n";
        ss << "  \"nps\": " << nps << ",\n";
        ss << "  \"ttHits\": " << tt_hits << ",\n";
        ss << "  \"ttMisses\": " << tt_misses << ",\n";
        ss << "  \"threads\": " << thread_count << ",\n";
        ss << "  \"pv\": [";
        for (size_t i = 0; i < pv.size(); ++i) {
            ss << "\"" << pv[i] << "\"" << (i + 1 < pv.size() ? ", " : "");
        }
        ss << "]\n";
        ss << "}";
        return ss.str();
    }
};

inline AnalysisResult analyze(const std::string& fen, const SearchOptions& options = SearchOptions()) {
    init_rays();
    init_magics();

    Board board;
    if (!board.set_fen(fen)) return AnalysisResult{};

    Engine engine;
    engine.thread_count = options.thread_count > 0 ? options.thread_count : 1;
    engine.configure_search(options.nodes_limit, options.time_limit_ms, options.stop_flag);
    auto start_time = std::chrono::high_resolution_clock::now();

    int target_depth = options.depth_limit > 0 ? options.depth_limit : 5;
    uint16_t best_packed = engine.best_move(board, target_depth);

    auto end_time = std::chrono::high_resolution_clock::now();
    int time_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    AnalysisResult result;
    result.best_move = best_packed == 0 ? "" : move_to_uci(best_packed);
    result.depth = engine.completed_depth;
    int score_cp = engine.root_scores.empty() ? 0 : engine.root_scores[0];
    result.eval_cp = score_cp;
    if (is_mate_score(score_cp)) {
        int plies = mate_in_plies(score_cp);
        result.evaluation = score_cp > 0 ? (100.0f - (float)plies) : (-100.0f + (float)plies);
    } else {
        result.evaluation = (float)score_cp / 100.0f;
    }
    result.nodes = engine.nodes_searched;
    result.time_ms = time_ms;
    result.nps = time_ms > 0 ? (result.nodes * 1000 / time_ms) : result.nodes;
    result.tt_hits = engine.tt.total_hits;
    result.tt_misses = engine.tt.total_misses;
    result.thread_count = engine.thread_count;

    const int pv_count = min(max(1, options.multi_pv), (int)engine.root_moves.size());
    for (int i = 0; i < pv_count; i++) result.pv.push_back(move_to_uci(engine.root_moves[i]));
    return result;
}

using namespace std;

inline string result_to_status_str(GameResult result);

inline bool apply_uci_move(Board& board, const string& uci_move) {
    MoveList moves;
    MoveGenerator generator;
    generator.generate_moves(board, moves);

    for (int i = 0; i < moves.size(); ++i) {
        if (move_to_uci(moves.move_list[i]) == uci_move) {
            board.make_move(moves.move_list[i]);
            return true;
        }
    }
    return false;
}

int run_uci() {
    const string start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Board board;
    board.set_fen(start_fen);
    Engine engine;

    std::atomic<bool> async_stop_requested{false};
    std::thread async_thread;
    
    // RAII guard to guarantee the UCI async thread is cleanly stopped and joined on any exit path
    struct UciThreadGuard {
        std::atomic<bool>& stop;
        std::thread& t;
        ~UciThreadGuard() {
            stop.store(true, std::memory_order_release);
            if (t.joinable()) t.join();
        }
    } uci_guard{async_stop_requested, async_thread};

    bool async_active = false;
    Board async_board;
    int async_depth = 5;
    uint16_t async_best = 0;
    Engine async_engine;
    uint64_t async_nodes = 0;
    int async_time_ms = 0;

    string line;
    while (getline(cin, line)) {
        istringstream input(line);
        string command;
        input >> command;

        if (command == "uci") {
            cout << "id name TigerFish" << endl;
            cout << "id author TigerFish" << endl;
            cout << "option name Threads type spin default 1 min 1 max 256" << endl;
            cout << "option name Hash type spin default 32 min 1 max 4096" << endl;
            cout << "uciok" << endl;
        } else if (command == "isready") {
            cout << "readyok" << endl;
        } else if (command == "ucinewgame") {
            engine.tt.clear();
            board.set_fen(start_fen);
            if (async_thread.joinable()) {
                async_stop_requested.store(true);
                async_thread.join();
            }
            async_active = false;
            async_stop_requested.store(false);
        } else if (command == "setoption") {
            string name_tok, value_tok;
            input >> name_tok; // "name"
            string opt_name;
            input >> opt_name;
            input >> value_tok; // "value"
            int opt_val;
            input >> opt_val;
            if (opt_name == "Threads") {
                async_engine.thread_count = max(1, min(256, opt_val));
                engine.thread_count = async_engine.thread_count;
            } else if (opt_name == "Hash") {
                // Future: resize TT
            }
        } else if (command == "position") {
            if (async_thread.joinable()) {
                async_stop_requested.store(true);
                async_thread.join();
                async_active = false;
            }
            string position_type;
            input >> position_type;
            bool valid_position = true;
            bool has_moves = false;

            if (position_type == "startpos") {
                board.set_fen(start_fen);
            } else if (position_type == "fen") {
                vector<string> fields;
                string field;
                while (input >> field && field != "moves") fields.push_back(field);
                if (fields.size() != 6) {
                    valid_position = false;
                } else {
                    string fen = fields[0];
                    for (size_t i = 1; i < fields.size(); ++i) fen += " " + fields[i];
                    valid_position = board.set_fen(fen);
                }
                has_moves = (field == "moves");
            } else {
                valid_position = false;
            }

            if (!valid_position) {
                cout << "info string invalid position command" << endl;
                continue;
            }

            string token;
            if (position_type == "startpos") {
                input >> token;
                has_moves = (token == "moves");
            }
            if (has_moves) {
                string uci_move;
                while (input >> uci_move) {
                    if (!apply_uci_move(board, uci_move)) {
                        cout << "info string illegal move " << uci_move << endl;
                        break;
                    }
                }
            }
        } else if (command == "go") {
            if (async_thread.joinable()) {
                async_stop_requested.store(true);
                async_thread.join();
            }
            async_stop_requested.store(false);
            async_board = board;
            async_depth = 256;
            async_time_ms = 0;
            async_best = 0;
            async_nodes = 0;
            uint64_t node_limit = 0;
            string token;
            while (input >> token) {
                if (token == "depth") input >> async_depth;
                else if (token == "nodes") input >> node_limit;
                else if (token == "movetime") input >> async_time_ms;
            }
            // If movetime is set, allow unlimited depth so time governs the search
            if (async_time_ms > 0) async_depth = 256;
            async_depth = max(1, async_depth);

            async_engine = Engine();
              async_engine.configure_search(node_limit, async_time_ms, &async_stop_requested);
              async_engine.thread_count = engine.thread_count;
              async_thread = thread([&]() {
                 async_best = async_engine.best_move(async_board, async_depth);
                 async_nodes = async_engine.nodes_searched;
                 const string best_uci = async_best == 0 ? "0000" : move_to_uci(async_best);
                 int score_cp = async_engine.root_scores.empty() ? 0 : async_engine.root_scores[0];
                 auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(
                     chrono::steady_clock::now() - async_engine.search_start_time).count();
                 uint64_t nps = elapsed_ms > 0 ? (async_nodes * 1000 / elapsed_ms) : async_nodes;
                 cout << "info depth " << async_engine.completed_depth << " score cp " << score_cp
                     << " nodes " << async_nodes << " nps " << nps << " time " << elapsed_ms 
                     << " tt_hit " << async_engine.tt.total_hits << " tt_miss " << async_engine.tt.total_misses 
                     << " pv " << best_uci << endl;
                 cout << "bestmove " << best_uci << endl;
                 async_active = false;
              });
              async_active = true;
        } else if (command == "stop") {
            async_stop_requested.store(true);
            if (async_thread.joinable()) {
                async_thread.join();
                async_active = false;
            }
        } else if (command == "quit") {
            async_stop_requested.store(true);
            if (async_thread.joinable()) async_thread.join();
            return 0;
        } else if (!command.empty()) {
            cout << "info string unknown command " << command << endl;
        }
    }
    async_stop_requested.store(true);
    if (async_thread.joinable()) async_thread.join();
    return 0;
}

int run_interactive() {
    const string start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Engine engine;
    Board board;
    bool initialized = false;

    string line;
    while (getline(cin, line)) {
        istringstream input(line);
        string command;
        input >> command;

        if (command == "quit" || command == "exit") return 0;

        if (command == "newgame") {
            string fen;
            getline(input, fen);
            if (!fen.empty() && fen.front() == ' ') fen.erase(0, 1);
            if (board.set_fen(fen.empty() ? start_fen : fen)) {
                initialized = true;
                cout << "{\"status\": \"ready\"}" << endl;
            } else {
                cout << "{\"error\": \"Invalid FEN\"}" << endl;
            }
        } else if (!initialized) {
            cout << "{\"error\": \"No active game. Send 'newgame' first.\"}" << endl;
        } else if (command == "apply") {
            string uci_move;
            input >> uci_move;
            if (!apply_uci_move(board, uci_move)) {
                cout << "{\"error\": \"Illegal move: " << uci_move << "\"}" << endl;
            } else {
                cout << "{\"fen\": \"" << board.to_fen() << "\", \"status\": \""
                     << result_to_status_str(get_game_result(board)) << "\"}" << endl;
            }
        } else if (command == "best") {
            int depth = 7;
            input >> depth;
            int time_ms = 0;
            if (input >> time_ms && time_ms > 0) {
                engine.configure_search(0, time_ms, nullptr);
            } else {
                engine.configure_search(0, 0, nullptr);
            }
            if (get_game_result(board) != GAME_ONGOING) {
                cout << "{\"best_move\": \"\", \"status\": \"game_over\"}" << endl;
            } else {
                uint16_t best = engine.best_move(board, max(1, depth));
                const string best_uci = best == 0 ? "" : move_to_uci(best);
                if (best != 0) board.make_move(best);
                cout << "{\"best_move\": \"" << best_uci << "\", \"fen\": \""
                     << board.to_fen() << "\", \"status\": \""
                     << result_to_status_str(get_game_result(board)) << "\"}" << endl;
            }
        } else {
            cout << "{\"error\": \"Unknown command: " << command << "\"}" << endl;
        }

        cout << "===READY===" << endl;
    }
    return 0;
}

inline string result_to_status_str(GameResult result) {
    switch (result) {
        case GAME_CHECKMATE:              return "checkmate";
        case GAME_STALEMATE:              return "stalemate";
        case GAME_FIFTY_MOVE_DRAW:        return "fifty_move";
        case GAME_SEVENTY_FIVE_MOVE_DRAW: return "seventy_five_move";
        case GAME_INSUFFICIENT_MATERIAL:  return "insufficient_material";
        case GAME_THREEFOLD_REPETITION:   return "threefold_repetition";
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

    if (argc == 1 || (argc == 2 && string(argv[1]) == "uci")) return run_uci();

    string command = argv[1];

    if (command == "interactive") return run_interactive();

    if (command == "analyze") {
        if (argc < 3) {
            cerr << "Usage: " << argv[0] << " analyze \"<fen>\" [depth] [--nodes <limit>] [--time <ms>] [--threads <count>]" << endl;
            return 1;
        }
        string fen = argv[2];
        Board validation_board;
        if (!validation_board.set_fen(fen)) {
            cerr << "Invalid FEN" << endl;
            return 1;
        }

        SearchOptions options;
        for (int i = 3; i < argc; ++i) {
            string argument = argv[i];
            if (argument == "--nodes" && i + 1 < argc) {
                options.nodes_limit = strtoull(argv[++i], nullptr, 10);
            } else if (argument == "--time" && i + 1 < argc) {
                options.time_limit_ms = atoi(argv[++i]);
            } else if (argument == "--threads" && i + 1 < argc) {
                options.thread_count = atoi(argv[++i]);
            } else {
                options.depth_limit = atoi(argument.c_str());
            }
        }

        AnalysisResult result = analyze(fen, options);
        cout << result.to_json() << endl;
        return 0;
    }

    if (command == "moves") {
        if (argc < 3) {
            cerr << "Usage: " << argv[0] << " moves \"<fen>\"" << endl;
            return 1;
        }
        string fen = argv[2];
        Board board;
        if (!board.set_fen(fen)) {
            cerr << "Invalid FEN" << endl;
            return 1;
        }

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        print_board_state_json(board, moves);
        return 0;
    }

    if (command == "make") {
        if (argc < 4) {
            cerr << "Usage: " << argv[0] << " make \"<fen>\" \"<uci_move>\"" << endl;
            return 1;
        }
        string fen = argv[2];
        string uci_move = argv[3];

        Board board;
        if (!board.set_fen(fen)) {
            cerr << "Invalid FEN" << endl;
            return 1;
        }

        MoveList moves;
        MoveGenerator mg;
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
            cerr << "Illegal move: " << uci_move << endl;
            return 1;
        }

        MoveList new_moves;
        mg.generate_moves(board, new_moves);
        print_board_state_json(board, new_moves);
        return 0;
    }

    if (command == "best") {
        if (argc < 3) {
            cerr << "Usage: " << argv[0] << " best \"<fen>\" [depth] [time_ms]" << endl;
            return 1;
        }
        string fen = argv[2];
        int depth = (argc >= 4) ? atoi(argv[3]) : 7;
        int time_ms = (argc >= 5) ? atoi(argv[4]) : 0;

        Board board;
        if (!board.set_fen(fen)) {
            cerr << "Invalid FEN" << endl;
            return 1;
        }

        Engine engine;
        if (time_ms > 0) engine.configure_search(0, time_ms, nullptr);
        uint16_t best = engine.best_move(board, depth);
        string best_uci = (best != 0) ? move_to_uci(best) : "";

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);
        print_board_state_json(board, moves, best_uci);
        return 0;
    }

    cerr << "Unknown command: " << command << endl;
    return 1;
}
