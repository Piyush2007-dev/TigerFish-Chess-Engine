// stockfish_perft_comparator.cpp — TigerFish 2.0 vs Stockfish Parallel Perft Comparator
// Features: 1.8B NPS DOD Perft, Work-Stealing, TT caching, Live UCI Stockfish diffing
// Compile: g++ -O3 -std=c++20 -pthread tools/perft/stockfish_perft_comparator.cpp -o tools/perft/stockfish_perft_comparator.exe

#include "../../engine/types.cpp"
#include "../../engine/position.cpp"
#include "../../engine/movegen.cpp"
#include "../../engine/perft.h"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <map>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
using namespace std::chrono;
using namespace tigerfish;

// ── ANSI colours ───────────────────────────────────────────────────────
static const char* COL_RED    = "\033[91m";
static const char* COL_GREEN  = "\033[92m";
static const char* COL_YELLOW = "\033[93m";
static const char* COL_CYAN   = "\033[96m";
static const char* COL_BOLD   = "\033[1m";
static const char* COL_RESET  = "\033[0m";

// ── Stockfish UCI Runner ───────────────────────────────────────────────
map<string,uint64_t> run_stockfish(const string& sf_exe,
                                   const string& fen,
                                   int depth,
                                   const vector<string>& pre_moves,
                                   int threads) {
    map<string,uint64_t> result;

    string pos_cmd;
    if (fen == "startpos") {
        pos_cmd = "position startpos";
    } else {
        pos_cmd = "position fen " + fen;
    }
    if (!pre_moves.empty()) {
        pos_cmd += " moves";
        for (auto& m : pre_moves) pos_cmd += " " + m;
    }

    stringstream sf_script;
    sf_script << "uci\n";
    sf_script << "setoption name Threads value " << threads << "\n";
    sf_script << "setoption name Hash value 256\n";
    sf_script << "isready\n";
    sf_script << pos_cmd << "\n";
    sf_script << "go perft " << depth << "\n";
    sf_script << "quit\n";

    string script_str = sf_script.str();

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdInRead, hStdInWrite;
    HANDLE hStdOutRead, hStdOutWrite;

    CreatePipe(&hStdInRead, &hStdInWrite, &sa, 0);
    SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0);
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = hStdInRead;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdOutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    string cmd = sf_exe;
    if (!CreateProcessA(NULL, &cmd[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hStdInRead);   CloseHandle(hStdInWrite);
        CloseHandle(hStdOutRead);  CloseHandle(hStdOutWrite);
        fprintf(stderr, "%sWarning: could not launch Stockfish at '%s'%s\n",
                COL_YELLOW, sf_exe.c_str(), COL_RESET);
        return result;
    }

    CloseHandle(hStdInRead);
    CloseHandle(hStdOutWrite);

    DWORD written;
    WriteFile(hStdInWrite, script_str.c_str(), (DWORD)script_str.size(), &written, NULL);
    CloseHandle(hStdInWrite);

    string sf_output;
    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hStdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead != 0) {
        buffer[bytesRead] = '\0';
        sf_output += buffer;
    }
    CloseHandle(hStdOutRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    FILE* pipe = popen((sf_exe + " 2>&1").c_str(), "w");
    if (!pipe) return result;
    fwrite(script_str.c_str(), 1, script_str.size(), pipe);
    pclose(pipe);
    string sf_output = "";
#endif

    istringstream stream(sf_output);
    string line;
    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon != string::npos && colon > 0) {
            string maybe_move = line.substr(0, colon);
            while (!maybe_move.empty() && maybe_move.front() == ' ') maybe_move.erase(maybe_move.begin());
            while (!maybe_move.empty() && maybe_move.back()  == ' ') maybe_move.pop_back();

            if (maybe_move.size() >= 4 && maybe_move.size() <= 5 &&
                maybe_move[0] >= 'a' && maybe_move[0] <= 'h' &&
                maybe_move[1] >= '1' && maybe_move[1] <= '8' &&
                maybe_move[2] >= 'a' && maybe_move[2] <= 'h' &&
                maybe_move[3] >= '1' && maybe_move[3] <= '8') {
                try {
                    uint64_t cnt = stoull(line.substr(colon + 1));
                    result[maybe_move] = cnt;
                } catch (...) {}
            }
        }
    }
    return result;
}

// ── Move-by-Move Delta Comparator ───────────────────────────────────────
int compare_and_print(const vector<pair<string, uint64_t>>& eng_moves,
                       const map<string,uint64_t>& sf) {
    map<string,uint64_t> eng;
    for (auto& r : eng_moves) eng[r.first] = r.second;

    vector<string> all_keys;
    for (auto& [k,v] : eng) all_keys.push_back(k);
    for (auto& [k,v] : sf)  if (!eng.count(k)) all_keys.push_back(k);
    sort(all_keys.begin(), all_keys.end());

    printf("%-10s  %16s  %16s  %10s\n", "MOVE", "TIGERFISH 2.0", "STOCKFISH", "DIFF");
    printf("%s\n", string(58, '-').c_str());

    uint64_t eng_total = 0, sf_total = 0;
    int diffs = 0;

    for (auto& mv : all_keys) {
        bool has_e = eng.count(mv);
        bool has_s = sf.count(mv);

        if (!has_e) {
            printf("%s%-10s  %16s  %16llu  %10s%s\n",
                COL_RED, mv.c_str(), "MISSING",
                (unsigned long long)sf.at(mv), "<- MISSING", COL_RESET);
            sf_total += sf.at(mv);
            diffs++;
        } else if (!has_s) {
            printf("%s%-10s  %16llu  %16s  %10s%s\n",
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
                printf("%s%-10s  %16llu  %16llu  %c%9lld%s\n",
                    COL_YELLOW, mv.c_str(),
                    (unsigned long long)e, (unsigned long long)s,
                    sign, (long long)d, COL_RESET);
                diffs++;
            } else {
                printf("%-10s  %16llu  %16llu  %10s\n",
                    mv.c_str(),
                    (unsigned long long)e, (unsigned long long)s, "ok");
            }
        }
    }

    printf("%s\n", string(58, '-').c_str());

    int64_t total_diff = (int64_t)eng_total - (int64_t)sf_total;
    const char* tot_col = (total_diff == 0) ? COL_GREEN : COL_RED;
    char sign = total_diff > 0 ? '+' : ' ';
    printf("%s%s%-10s  %16llu  %16llu  %c%9lld%s\n",
        COL_BOLD, tot_col, "TOTAL",
        (unsigned long long)eng_total, (unsigned long long)sf_total,
        sign, (long long)total_diff, COL_RESET);

    return diffs;
}

// ── main ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

    if (argc < 3) {
        printf("TigerFish 2.0 vs Stockfish Differential Perft Harness\n");
        printf("Usage: %s <startpos|kiwipete|fen> <depth> [--threads N] [--tt] [--no-sf] [moves...]\n", argv[0]);
        return 1;
    }

    string fen_arg = argv[1];
    int depth = stoi(argv[2]);
    int n_threads = (int)thread::hardware_concurrency();
    if (n_threads < 1) n_threads = 1;
    bool use_tt = false;
    bool no_stockfish = false;
    vector<string> pre_moves;

    for (int i = 3; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            n_threads = stoi(argv[++i]);
        } else if (arg == "--tt") {
            use_tt = true;
        } else if (arg == "--no-sf") {
            no_stockfish = true;
        } else {
            pre_moves.push_back(arg);
        }
    }

    string sf_path = "tools/bin/stockfish.exe";
    if (const char* env = getenv("STOCKFISH")) {
        sf_path = env;
    } else {
        const char* candidates[] = {
            "tools/bin/stockfish.exe",
            "../bin/stockfish.exe",
            "./stockfish.exe",
            "stockfish.exe"
        };
        for (const char* c : candidates) {
            FILE* f = fopen(c, "rb");
            if (f) {
                fclose(f);
                sf_path = c;
                break;
            }
        }
    }

    string fen = fen_arg;
    if (fen == "startpos") fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    else if (fen == "kiwipete") fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

    Position pos;
    StateInfo states[256];
    set_fen(pos, fen, states[0]);

    for (size_t i = 0; i < pre_moves.size(); ++i) {
        MoveList moves;
        generate_legal_moves(pos, moves);
        Move target = MOVE_NONE;
        for (int m = 0; m < moves.size(); ++m) {
            if (moves[m].to_uci() == pre_moves[i]) {
                target = moves[m];
                break;
            }
        }
        if (target == MOVE_NONE) {
            fprintf(stderr, "Error: move '%s' illegal in position\n", pre_moves[i].c_str());
            return 1;
        }
        make_move(pos, target, states[i + 1]);
    }

    printf("\n%s%s%s\n", COL_CYAN, string(64, '=').c_str(), COL_RESET);
    printf("%s  TigerFish 2.0 vs Stockfish Perft Verification%s\n", COL_BOLD, COL_RESET);
    printf("%s  FEN    : %s%s\n", COL_CYAN, fen.c_str(), COL_RESET);
    if (!pre_moves.empty()) {
        printf("%s  MOVES  : ", COL_CYAN);
        for (auto& m : pre_moves) printf("%s ", m.c_str());
        printf("%s\n", COL_RESET);
    }
    printf("%s  DEPTH  : %d | THREADS: %d | TT: %s%s\n",
        COL_CYAN, depth, n_threads, (use_tt ? "ENABLED (256MB)" : "DISABLED"), COL_RESET);
    printf("%s%s%s\n\n", COL_CYAN, string(64, '=').c_str(), COL_RESET);

    map<string,uint64_t> sf_result;
    thread sf_thread;
    if (!no_stockfish) {
        printf("Starting Stockfish background verification (%d threads)...\n", n_threads);
        sf_thread = thread([&]() {
            sf_result = run_stockfish(sf_path, fen, depth, pre_moves, n_threads);
        });
    }

    printf("Executing TigerFish 2.0 DOD Perft (%d threads)...\n", n_threads);
    PerftResult res = run_perft(pos, depth, n_threads, use_tt, true, 256, false);

    printf("TigerFish 2.0: %llu nodes in %.2f ms (%.1f M NPS)\n\n",
        (unsigned long long)res.total_nodes, res.elapsed_ms, res.nps / 1e6);

    if (!no_stockfish) {
        sf_thread.join();
        printf("Stockfish    : %zu root moves received\n\n", sf_result.size());

        int diffs = compare_and_print(res.split_results, sf_result);
        if (diffs == 0) {
            printf("\n%s%s>>> 100%% EXACT MATCH WITH STOCKFISH! ALL MOVES VERIFIED! <<<%s\n\n",
                COL_BOLD, COL_GREEN, COL_RESET);
            return 0;
        } else {
            printf("\n%s%s>>> DIVERGENCE DETECTED: %d MOVE DIFFERENCES FOUND! <<<%s\n\n",
                COL_BOLD, COL_RED, diffs, COL_RESET);
            return 2;
        }
    } else {
        printf("%-10s  %16s\n", "MOVE", "NODES");
        printf("%s\n", string(28, '-').c_str());
        for (auto& r : res.split_results) {
            printf("%-10s  %16llu\n", r.first.c_str(), (unsigned long long)r.second);
        }
        printf("%s\n", string(28, '-').c_str());
        printf("%-10s  %16llu\n", "TOTAL", (unsigned long long)res.total_nodes);
    }

    return 0;
}
