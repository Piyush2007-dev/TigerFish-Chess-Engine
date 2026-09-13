/*
    perft_compare_dynamic.cpp  —  parallel split-perft with dynamic load balancing vs Stockfish
    Compile : g++ -O3 -std=c++20 -pthread perft_compare_dynamic.cpp -o perft_compare_dynamic.exe

    Usage:
        perft_compare_dynamic.exe <fen|startpos> <depth> [--threads N] [move1 move2 ...]

    Examples:
        perft_compare_dynamic.exe startpos 7
*/

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include "../archive/chess.cpp"

using namespace std;
using namespace std::chrono;

// ── ANSI colours ───────────────────────────────────────────────────────
static const char* COL_RED    = "\033[91m";
static const char* COL_GREEN  = "\033[92m";
static const char* COL_YELLOW = "\033[93m";
static const char* COL_CYAN   = "\033[96m";
static const char* COL_BOLD   = "\033[1m";
static const char* COL_RESET  = "\033[0m";

// ── Move helpers ────────────────────────────────────────────────────────
static const char FILE_CHARS[] = "abcdefgh";
static const char PROMO_CHARS[] = {'p','n','b','r','q','k','p','n','b','r','q','k'};

string move_to_uci(Move m) {
    int from = Move::from(m);
    int to   = Move::to(m);
    string s;
    s += FILE_CHARS[from % 8];
    s += char('1' + from / 8);
    s += FILE_CHARS[to   % 8];
    s += char('1' + to   / 8);
    if (Move::is_promotion(m))
        s += PROMO_CHARS[(int)Move::promotion_piece(m)];
    return s;
}

Move parse_move(Board& board, const string& uci) {
    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);
    for (int i = 0; i < moves.size(); i++) {
        if (move_to_uci(moves.move_list[i]) == uci)
            return moves.move_list[i];
    }
    fprintf(stderr, "ERROR: move '%s' not found in position\n", uci.c_str());
    exit(1);
}

// ── Recursive perft with Transposition Table ──────────────────────────
struct TTEntry {
    uint64_t key = 0;
    uint64_t nodes = 0;
    int8_t depth = 0;
    
    // Verification fields to be 100% collision-safe:
    uint64_t white_occ = 0;
    uint64_t black_occ = 0;
    uint8_t castling_rights = 0;
    uint8_t en_passant = 0;
    uint8_t side_to_move = 0;
};

struct GlobalTT {
    vector<TTEntry> table;
    mutex* locks;
    size_t size_mask;

    GlobalTT(size_t size) {
        table.resize(size);
        locks = new mutex[65536];
        size_mask = size - 1;
    }
    ~GlobalTT() {
        delete[] locks;
    }
    GlobalTT(const GlobalTT&) = delete;
    GlobalTT& operator=(const GlobalTT&) = delete;

    uint64_t lookup(uint64_t key, int depth, const Board& board) {
        size_t idx = key & size_mask;
        size_t lock_idx = key & 65535;
        
        lock_guard<mutex> lock(locks[lock_idx]);
        const TTEntry& entry = table[idx];
        if (entry.key == key &&
            entry.depth == depth &&
            entry.white_occ == board.occupancy[WHITE] &&
            entry.black_occ == board.occupancy[BLACK] &&
            entry.castling_rights == board.castling_rights &&
            entry.en_passant == board.en_passant &&
            entry.side_to_move == board.side_to_move) {
            return entry.nodes;
        }
        return 0;
    }

    void store(uint64_t key, uint64_t nodes, int depth, const Board& board) {
        size_t idx = key & size_mask;
        size_t lock_idx = key & 65535;
        
        lock_guard<mutex> lock(locks[lock_idx]);
        if (table[idx].depth < depth) { // depth-preferred replacement
            table[idx] = {
                key,
                nodes,
                (int8_t)depth,
                board.occupancy[WHITE],
                board.occupancy[BLACK],
                board.castling_rights,
                board.en_passant,
                (uint8_t)board.side_to_move
            };
        }
    }
};

inline uint64_t compute_board_hash(const Board& board) {
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (int p = 0; p < 12; p++) {
        uint64_t x = board.bitboards[p];
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    h ^= ((uint64_t)board.side_to_move) * 0xbf58476d1ce4e5b9ULL;
    h ^= ((uint64_t)board.castling_rights) * 0x94d049bb133111ebULL;
    h ^= ((uint64_t)board.en_passant) * 0xbf58476d1ce4e5b9ULL;
    return h;
}

// ── Recursive perft with Global Transposition Table (Template Specialized) ──
template <bool USE_TT>
uint64_t perft(Board& board, int depth, GlobalTT& tt) {
    if (depth == 0) return 1ULL;

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    if (depth == 1) {
        return (uint64_t)moves.size();
    }

    if constexpr (!USE_TT) {
        uint64_t nodes = 0;
        MoveList dummy;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i], dummy);
            nodes += perft<USE_TT>(board, depth - 1, tt);
            board.unmake_move();
        }
        return nodes;
    } else {
        if (depth <= 2) {
            uint64_t nodes = 0;
            MoveList dummy;
            for (int i = 0; i < moves.size(); i++) {
                board.make_move(moves.move_list[i], dummy);
                nodes += perft<USE_TT>(board, depth - 1, tt);
                board.unmake_move();
            }
            return nodes;
        }

        // Lookup in TT (only for depth >= 3)
        uint64_t key = compute_board_hash(board);
        uint64_t cached_nodes = tt.lookup(key, depth, board);
        if (cached_nodes != 0) {
            return cached_nodes;
        }

        uint64_t nodes = 0;
        MoveList dummy;
        for (int i = 0; i < moves.size(); i++) {
            board.make_move(moves.move_list[i], dummy);
            nodes += perft<USE_TT>(board, depth - 1, tt);
            board.unmake_move();
        }

        // Store in TT (only cache depths >= 3)
        tt.store(key, nodes, depth, board);

        return nodes;
    }
}

// ── Fine-grained Task definition ───────────────────────────────────────
struct Task {
    int root_move_idx;
    Board board;
    int remaining_depth;
};

// Recursively generate tasks down to the target split depth
void generate_tasks(Board board, int current_depth, int target_split_depth, int root_move_idx, int total_depth, vector<Task>& tasks) {
    if (current_depth == target_split_depth) {
        tasks.push_back({root_move_idx, board, total_depth - target_split_depth});
        return;
    }

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    if (moves.size() == 0) {
        // Terminal node reached before split depth; contributes 0 nodes to deeper depths
        return;
    }

    for (int i = 0; i < moves.size(); i++) {
        Board child = board;
        MoveList dummy;
        child.make_move(moves.move_list[i], dummy);
        
        int next_root_idx = (current_depth == 0) ? i : root_move_idx;
        generate_tasks(child, current_depth + 1, target_split_depth, next_root_idx, total_depth, tasks);
    }
}

struct SplitResult {
    string   move_str;
    uint64_t count = 0;
};

// ── Work-Stealing Parallel split-perft ─────────────────────────────────
vector<SplitResult> parallel_split_perft(Board& root, int depth, int num_threads) {
    MoveGenerator mg;
    MoveList root_moves;
    mg.generate_moves(root, root_moves);

    int n = root_moves.size();
    vector<SplitResult> results(n);

    for (int i = 0; i < n; i++) {
        results[i].move_str = move_to_uci(root_moves.move_list[i]);
        results[i].count = 0;
    }

    // Determine target split depth dynamically based on the total requested depth.
    int split_depth = 1;
    if (depth > 5) {
        split_depth = 3;  // Splits at depth 3 (~8,900 tasks for startpos)
    } else if (depth > 2) {
        split_depth = 2;  // Splits at depth 2 (~400 tasks for startpos)
    }

    if (split_depth >= depth) {
        split_depth = depth - 1;
    }
    if (split_depth < 1) {
        split_depth = 1;
    }

    printf("Generating load-balanced sub-tasks (split depth = %d)...\n", split_depth);
    vector<Task> tasks;
    generate_tasks(root, 0, split_depth, -1, depth, tasks);
    printf("Generated %zu independent tasks.\n", tasks.size());

    int num_tasks = (int)tasks.size();
    atomic<int> next_task(0);
    vector<atomic<uint64_t>> move_counts(n);
    for (int i = 0; i < n; i++) {
        move_counts[i].store(0);
    }
    atomic<uint64_t> global_nodes(0);
    mutex print_mtx;

    bool use_tt = (depth >= 9);
    GlobalTT tt(use_tt ? (1 << 23) : 1); // 8.3M entries (335MB) shared globally only if enabled
    auto worker = [&]() {
        while (true) {
            int idx = next_task.fetch_add(1, memory_order_relaxed);
            if (idx >= num_tasks) break;

            const Task& task = tasks[idx];
            Board local_board = task.board;
            uint64_t count;
            if (use_tt) {
                count = perft<true>(local_board, task.remaining_depth, tt);
            } else {
                count = perft<false>(local_board, task.remaining_depth, tt);
            }

            move_counts[task.root_move_idx].fetch_add(count, memory_order_relaxed);

            uint64_t prev = global_nodes.fetch_add(count, memory_order_relaxed);
            uint64_t curr = prev + count;

            uint64_t prev_boundary = prev / 1000000000ULL;
            uint64_t curr_boundary = curr / 1000000000ULL;
            if (curr_boundary > prev_boundary) {
                lock_guard<mutex> lock(print_mtx);
                printf("[Progress] Completed %llu billion nodes...\n", curr_boundary);
                fflush(stdout);
            }
        }
    };

    int threads_to_use = min(num_threads, num_tasks);
    if (threads_to_use < 1) threads_to_use = 1;

    vector<thread> pool;
    pool.reserve(threads_to_use);
    for (int i = 0; i < threads_to_use; i++)
        pool.emplace_back(worker);
    for (auto& t : pool)
        t.join();

    // Copy results from atomic accumulators
    for (int i = 0; i < n; i++) {
        results[i].count = move_counts[i].load();
    }

    return results;
}

// ── Run Stockfish and parse its split-perft output ─────────────────────
map<string,uint64_t> run_stockfish(const string& sf_exe,
                                    const string& fen,
                                    int depth,
                                    const vector<string>& pre_moves,
                                    int num_threads) {
    string pos;
    if (fen == "startpos" || fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
        pos = "position startpos";
    else
        pos = "position fen " + fen;

    if (!pre_moves.empty()) {
        pos += " moves";
        for (auto& m : pre_moves) { pos += " "; pos += m; }
    }

    string uci_input = "uci\nsetoption name Threads value " + to_string(num_threads) + "\nisready\n" + pos +
                       "\ngo perft " + to_string(depth) + "\nquit\n";

    const char* tmpname = "perft_compare_sf_stdin.tmp";
    {
        FILE* f = fopen(tmpname, "w");
        if (!f) { fprintf(stderr, "ERROR: cannot create temp file\n"); exit(1); }
        fputs(uci_input.c_str(), f);
        fclose(f);
    }

    string cmd = "\"" + sf_exe + "\" < " + tmpname;
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        fprintf(stderr, "ERROR: cannot run Stockfish: %s\n", sf_exe.c_str());
        exit(1);
    }

    map<string,uint64_t> result;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
        char mv[16]; unsigned long long cnt;
        if (sscanf(buf, "%15[a-h1-8qrbnQRBN]: %llu", mv, &cnt) == 2) {
            string s = mv;
            for (char& c : s) c = (char)tolower(c);
            result[s] = (uint64_t)cnt;
        }
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    remove(tmpname);
    return result;
}

// ── Print comparison table ─────────────────────────────────────────────
int compare_and_print(const vector<SplitResult>& eng_moves,
                       const map<string,uint64_t>& sf) {
    map<string,uint64_t> eng;
    for (auto& r : eng_moves) eng[r.move_str] = r.count;

    vector<string> all_keys;
    for (auto& [k,v] : eng) all_keys.push_back(k);
    for (auto& [k,v] : sf)  if (!eng.count(k)) all_keys.push_back(k);
    sort(all_keys.begin(), all_keys.end());

    printf("%-10s  %14s  %14s  %10s\n", "MOVE", "YOUR ENGINE", "STOCKFISH", "DIFF");
    printf("%s\n", string(54, '-').c_str());

    uint64_t eng_total = 0, sf_total = 0;
    int diffs = 0;

    for (auto& mv : all_keys) {
        bool has_e = eng.count(mv);
        bool has_s = sf.count(mv);

        if (!has_e) {
            printf("%s%-10s  %14s  %14llu  %10s%s\n",
                COL_RED, mv.c_str(), "MISSING",
                (unsigned long long)sf.at(mv), "<- missing", COL_RESET);
            sf_total += sf.at(mv);
            diffs++;
        } else if (!has_s) {
            printf("%s%-10s  %14llu  %14s  %10s%s\n",
                COL_RED, mv.c_str(),
                (unsigned long long)eng.at(mv), "MISSING", "<- EXTRA", COL_RESET);
            eng_total += eng.at(mv);
            diffs++;
        } else {
            uint64_t e = eng.at(mv);
            uint64_t s = sf.at(mv);
            eng_total += e;
            sf_total  += s;
            int64_t d  = (int64_t)e - (int64_t)s;

            if (d != 0) {
                char sign = d > 0 ? '+' : ' ';
                printf("%s%-10s  %14llu  %14llu  %c%9lld%s\n",
                    COL_YELLOW, mv.c_str(),
                    (unsigned long long)e, (unsigned long long)s,
                    sign, (long long)d, COL_RESET);
                diffs++;
            } else {
                printf("%-10s  %14llu  %14llu  %10s\n",
                    mv.c_str(),
                    (unsigned long long)e, (unsigned long long)s, "ok");
            }
        }
    }

    printf("%s\n", string(54, '-').c_str());

    int64_t total_diff = (int64_t)eng_total - (int64_t)sf_total;
    const char* tot_col = (total_diff == 0) ? COL_GREEN : COL_RED;
    char sign = total_diff > 0 ? '+' : ' ';
    printf("%s%s%-10s  %14llu  %14llu  %c%9lld%s\n",
        COL_BOLD, tot_col, "TOTAL",
        (unsigned long long)eng_total, (unsigned long long)sf_total,
        sign, (long long)total_diff, COL_RESET);

    return diffs;
}

// ── main ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    init_rays();
    init_magics();
#ifdef _WIN32
    system(""); // triggers VT processing on modern Windows
#endif

    if (argc < 3) {
        fprintf(stderr,
            "Usage: %s <fen|startpos> <depth> [--threads N] [move1 move2 ...]\n",
            argv[0]);
        return 1;
    }

    string fen_arg  = argv[1];
    int    depth    = stoi(argv[2]);
    int    n_threads = (int)thread::hardware_concurrency();
    if (n_threads < 1) n_threads = 1;
    vector<string> pre_moves;

    bool no_stockfish = false;
    for (int i = 3; i < argc; i++) {
        if (string(argv[i]) == "--threads" && i + 1 < argc) {
            n_threads = stoi(argv[++i]);
        } else if (string(argv[i]) == "--no-sf") {
            no_stockfish = true;
        } else {
            pre_moves.push_back(argv[i]);
        }
    }

    string sf_path = "./stockfish-windows-x86-64-avx2.exe";
    if (const char* env = getenv("STOCKFISH")) sf_path = env;
#ifdef _WIN32
    if (sf_path.rfind("./", 0) == 0) {
        sf_path = sf_path.substr(2);
    } else if (sf_path.rfind(".\\", 0) == 0) {
        sf_path = sf_path.substr(2);
    }
    for (char &c : sf_path) {
        if (c == '/') c = '\\';
    }
#endif

    Board board;
    if (fen_arg == "startpos")
        board.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    else
        board.set_fen(fen_arg);

    for (auto& mv : pre_moves) {
        Move m = parse_move(board, mv);
        MoveList dummy;
        board.make_move(m, dummy);
    }

    printf("\n%s%s%s\n", COL_CYAN, string(60, '=').c_str(), COL_RESET);
    printf("%s  FEN    : %s%s\n", COL_CYAN, fen_arg.c_str(), COL_RESET);
    if (!pre_moves.empty()) {
        printf("%s  MOVES  : ", COL_CYAN);
        for (auto& m : pre_moves) printf("%s ", m.c_str());
        printf("%s\n", COL_RESET);
    }
    printf("%s  DEPTH  : %d%s\n", COL_CYAN, depth,     COL_RESET);
    printf("%s  THREADS: %d / %d logical cores%s\n",
        COL_CYAN, n_threads,
        (int)thread::hardware_concurrency(), COL_RESET);
    printf("%s%s%s\n\n", COL_CYAN, string(60, '=').c_str(), COL_RESET);

    map<string,uint64_t> sf_result;
    thread sf_thread;
    if (!no_stockfish) {
        printf("Starting Stockfish (background with %d threads)...\n", n_threads);
        sf_thread = thread([&]() {
            sf_result = run_stockfish(sf_path, fen_arg, depth, pre_moves, n_threads);
        });
    }

    printf("Running load-balanced engine with %d thread(s)...\n", n_threads);
    auto t0 = high_resolution_clock::now();

    vector<SplitResult> eng_result = parallel_split_perft(board, depth, n_threads);

    auto t1 = high_resolution_clock::now();
    double elapsed = duration<double>(t1 - t0).count();
    uint64_t total_nodes = 0;
    for (auto& r : eng_result) total_nodes += r.count;

    printf("Engine  : %llu nodes in %.2f sec  (%.1f Mnps)\n\n",
        (unsigned long long)total_nodes, elapsed,
        total_nodes / elapsed / 1e6);

    if (!no_stockfish) {
        sf_thread.join();
        printf("Stockfish: %zu root moves received\n\n", sf_result.size());

        int diffs = compare_and_print(eng_result, sf_result);

        printf("\n");
        if (diffs == 0) {
            printf("%s%s All moves match perfectly! (depth %d)%s\n",
                COL_BOLD, COL_GREEN, depth, COL_RESET);
        } else {
            printf("%s%s %d difference(s) found.%s\n",
                COL_BOLD, COL_RED, diffs, COL_RESET);
        }
        printf("\n");

        return (diffs == 0) ? 0 : 1;
    } else {
        printf("MOVE           YOUR ENGINE\n");
        printf("--------------------------\n");
        for (auto& r : eng_result) {
            printf("%-10s  %14llu\n", r.move_str.c_str(), (unsigned long long)r.count);
        }
        printf("--------------------------\n");
        printf("%sTOTAL          %14llu%s\n\n", COL_BOLD, (unsigned long long)total_nodes, COL_RESET);
        return 0;
    }
}
