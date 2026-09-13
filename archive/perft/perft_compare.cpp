/*
    perft_compare.cpp  —  parallel split-perft vs Stockfish
    Compile : g++ -O3 -std=c++20 -pthread perft_compare.cpp -o perft_compare.exe

    Usage:
        perft_compare.exe <fen|startpos> <depth> [--threads N] [move1 move2 ...]

    Examples:
        perft_compare.exe "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" 6
        perft_compare.exe startpos 6 --threads 4
        perft_compare.exe "r3k2r/..." 5 --threads 8 e2e4

    By default uses all logical CPU cores.
    Stockfish path: set env var STOCKFISH, or place exe next to this binary.
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

// ── ANSI colours (Windows 10+ supports VT100 in console) ───────────────
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

// ── Recursive perft (no hash table — pure node count) ──────────────────
// Board passed by value so each thread gets its own copy.
uint64_t perft(Board board, int depth) {
    if (depth == 0) return 1ULL;

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    uint64_t nodes = 0;
    for (int i = 0; i < moves.size(); i++) {
        Board child = board;
        MoveList dummy;
        child.make_move(moves.move_list[i], dummy);
        nodes += perft(child, depth - 1);
    }
    return nodes;
}

// ── Parallel split-perft ───────────────────────────────────────────────
// Each root move is an independent task. An atomic counter acts as a
// work-stealing queue so faster threads pick up more work automatically.
struct SplitResult {
    string   move_str;
    uint64_t count = 0;
};

vector<SplitResult> parallel_split_perft(Board& root, int depth, int num_threads) {
    MoveGenerator mg;
    MoveList root_moves;
    mg.generate_moves(root, root_moves);

    int n = root_moves.size();
    vector<SplitResult> results(n);

    // Pre-fill move strings so we can index them without a mutex
    for (int i = 0; i < n; i++)
        results[i].move_str = move_to_uci(root_moves.move_list[i]);

    atomic<int> next_task(0);   // work-stealing index
    atomic<uint64_t> global_nodes(0);
    mutex print_mtx;

    auto worker = [&]() {
        while (true) {
            int idx = next_task.fetch_add(1, memory_order_relaxed);
            if (idx >= n) break;

            Board child = root;
            MoveList dummy;
            child.make_move(root_moves.move_list[idx], dummy);

            uint64_t count = perft(child, depth - 1);
            results[idx].count = count;

            uint64_t prev = global_nodes.fetch_add(count, memory_order_relaxed);
            uint64_t curr = prev + count;

            uint64_t prev_boundary = prev / 100000000ULL;
            uint64_t curr_boundary = curr / 100000000ULL;
            if (curr_boundary > prev_boundary) {
                lock_guard<mutex> lock(print_mtx);
                printf("[Progress] Completed %llu million nodes...\n", curr_boundary * 100ULL);
                fflush(stdout);
            }
        }
    };

    int threads_to_use = min(num_threads, n);
    vector<thread> pool;
    pool.reserve(threads_to_use);
    for (int i = 0; i < threads_to_use; i++)
        pool.emplace_back(worker);
    for (auto& t : pool)
        t.join();

    return results;
}

// ── Run Stockfish and parse its split-perft output ─────────────────────
// Writes UCI commands to a temp file, runs SF with stdin redirect.
map<string,uint64_t> run_stockfish(const string& sf_exe,
                                    const string& fen,
                                    int depth,
                                    const vector<string>& pre_moves) {
    // Build UCI command string
    string pos;
    if (fen == "startpos" || fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
        pos = "position startpos";
    else
        pos = "position fen " + fen;

    if (!pre_moves.empty()) {
        pos += " moves";
        for (auto& m : pre_moves) { pos += " "; pos += m; }
    }

    string uci_input = "uci\nisready\n" + pos +
                       "\ngo perft " + to_string(depth) + "\nquit\n";

    // Write to temp file for stdin redirect
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
        // Match lines like:  a2a3: 380   or   a7a8q: 12
        char mv[16]; unsigned long long cnt;
        if (sscanf(buf, "%15[a-h1-8qrbnQRBN]: %llu", mv, &cnt) == 2) {
            string s = mv;
            // lowercase
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
    // Build a map from engine results for easy lookup
    map<string,uint64_t> eng;
    for (auto& r : eng_moves) eng[r.move_str] = r.count;

    // Union of all move keys, sorted
    vector<string> all_keys;
    for (auto& [k,v] : eng) all_keys.push_back(k);
    for (auto& [k,v] : sf)  if (!eng.count(k)) all_keys.push_back(k);
    sort(all_keys.begin(), all_keys.end());

    // Header
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
    // Enable ANSI on Windows
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

    for (int i = 3; i < argc; i++) {
        if (string(argv[i]) == "--threads" && i + 1 < argc) {
            n_threads = stoi(argv[++i]);
        } else {
            pre_moves.push_back(argv[i]);
        }
    }

    // Stockfish path
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

    // Set up board
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

    // Header
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

    // ── Run Stockfish in a background thread ───────────────────────────
    map<string,uint64_t> sf_result;
    printf("Starting Stockfish (background)...\n");
    thread sf_thread([&]() {
        sf_result = run_stockfish(sf_path, fen_arg, depth, pre_moves);
    });

    // ── Run parallel perft ─────────────────────────────────────────────
    printf("Running engine with %d thread(s)...\n\n", n_threads);
    auto t0 = high_resolution_clock::now();

    vector<SplitResult> eng_result = parallel_split_perft(board, depth, n_threads);

    auto t1 = high_resolution_clock::now();
    double elapsed = duration<double>(t1 - t0).count();
    uint64_t total_nodes = 0;
    for (auto& r : eng_result) total_nodes += r.count;

    printf("Engine  : %llu nodes in %.2f sec  (%.1f Mnps)\n\n",
        (unsigned long long)total_nodes, elapsed,
        total_nodes / elapsed / 1e6);

    // Wait for Stockfish
    sf_thread.join();
    printf("Stockfish: %zu root moves received\n\n", sf_result.size());

    // ── Compare ────────────────────────────────────────────────────────
    int diffs = compare_and_print(eng_result, sf_result);

    printf("\n");
    if (diffs == 0) {
        printf("%s%s All moves match perfectly! (depth %d)%s\n",
            COL_BOLD, COL_GREEN, depth, COL_RESET);
    } else {
        printf("%s%s %d difference(s) found.%s\n",
            COL_BOLD, COL_RED, diffs, COL_RESET);
        printf("\n%sTip: re-run adding a bad move + --depth %d to drill down.%s\n",
            COL_YELLOW, depth - 1, COL_RESET);
    }
    printf("\n");

    return (diffs == 0) ? 0 : 1;
}
