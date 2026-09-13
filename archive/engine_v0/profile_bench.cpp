// ═══════════════════════════════════════════════════════════════════
// TIGERFISH SEARCH PROFILER — engine/profile_bench.cpp
// Zero-modification profiler: microbenchmarks every engine component,
// runs full searches, and estimates the time breakdown.
//
// Compile: g++ -O3 -std=c++20 -o profile_bench.exe profile_bench.cpp
// Run:     ./profile_bench.exe
// ═══════════════════════════════════════════════════════════════════

#include "magic_lut.cpp"
#include "board.cpp"
#include "rules.cpp"
#include "eval.cpp"
#include "search.cpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>

using namespace std;
using namespace chrono;

// ═══════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════

static const int MICRO_ITERS   = 200000;
static const int WARMUP_ITERS  = 20000;
static const int BENCH_SAMPLES = 5;

// ═══════════════════════════════════════════════════════════════════
// TEST POSITIONS
// ═══════════════════════════════════════════════════════════════════

struct TestPos {
    const char* name;
    const char* fen;
};

static const TestPos POSITIONS[] = {
    {"Starting Position",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"Sicilian Defense",     "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
    {"Italian Game",         "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3"},
    {"Complex Middlegame",   "r1bq1rk1/ppp2ppp/2np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
    {"Queen's Gambit",       "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2"},
    {"Rook Endgame",         "8/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1"},
    {"Pawn Endgame",         "8/4k3/8/4P3/8/8/4K3/8 w - - 0 1"},
    {"Kiwipete",             "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
};
static const int NUM_POS = sizeof(POSITIONS) / sizeof(POSITIONS[0]);

// ═══════════════════════════════════════════════════════════════════
// MICROBENCHMARK HARNESS
// ═══════════════════════════════════════════════════════════════════

static volatile int g_sink;

struct BenchResult {
    double median_ns;
    double min_ns;
    double max_ns;
};

template<typename F>
BenchResult microbench(F&& func, int iters = MICRO_ITERS) {
    for (int i = 0; i < WARMUP_ITERS; i++) func();

    double times[BENCH_SAMPLES];
    for (int s = 0; s < BENCH_SAMPLES; s++) {
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < iters; i++) func();
        auto t1 = high_resolution_clock::now();
        times[s] = (double)duration_cast<nanoseconds>(t1 - t0).count() / iters;
    }
    sort(times, times + BENCH_SAMPLES);
    return { times[BENCH_SAMPLES / 2], times[0], times[BENCH_SAMPLES - 1] };
}

// ═══════════════════════════════════════════════════════════════════
// COMPONENT NAMES
// ═══════════════════════════════════════════════════════════════════

enum Component {
    C_EVALUATE = 0,
    C_IS_IN_CHECK,
    C_GENERATE_MOVES,
    C_GENERATE_CAPTURES,
    C_FIND_PINS,
    C_ORDER_MOVES,
    C_MAKE_UNMAKE,
    C_SEE_FULL,
    C_TT_PROBE,
    C_TT_STORE,
    C_SORT_MVV_LVA,
    C_SHOULD_STOP,
    C_COUNT
};

static const char* COMP_NAMES[C_COUNT] = {
    "evaluate()",
    "is_in_check()",
    "generate_moves()",
    "generate_captures()",
    "find_pins()",
    "order_moves()",
    "make+unmake pair",
    "see_full()",
    "TT.probe()",
    "TT.store()",
    "sort_mvv_lva()",
    "should_stop()"
};

struct PosProfile {
    double ns[C_COUNT] = {};
    int move_count = 0;
    int capture_count = 0;
};

// ═══════════════════════════════════════════════════════════════════
// BENCHMARK ALL COMPONENTS ON A SINGLE POSITION
// ═══════════════════════════════════════════════════════════════════

PosProfile benchmark_position(const char* fen) {
    PosProfile prof;
    Board board;
    board.set_fen(fen);

    MoveList legal_moves;
    MoveGenerator gen;
    gen.generate_moves(board, legal_moves);
    prof.move_count = legal_moves.size();

    MoveList cap_moves;
    MoveGenerator gen2;
    gen2.generate_captures(board, cap_moves);
    prof.capture_count = cap_moves.size();

    // 1. evaluate()
    {
        auto r = microbench([&]() { g_sink = evaluate(board); });
        prof.ns[C_EVALUATE] = r.median_ns;
    }

    // 2. is_in_check()
    {
        auto r = microbench([&]() { g_sink = is_in_check(board) ? 1 : 0; });
        prof.ns[C_IS_IN_CHECK] = r.median_ns;
    }

    // 3. generate_moves()
    {
        auto r = microbench([&]() {
            MoveList ml;
            MoveGenerator g;
            g.generate_moves(board, ml);
            g_sink = ml.size();
        });
        prof.ns[C_GENERATE_MOVES] = r.median_ns;
    }

    // 4. generate_captures()
    {
        auto r = microbench([&]() {
            MoveList ml;
            MoveGenerator g;
            g.generate_captures(board, ml);
            g_sink = ml.size();
        });
        prof.ns[C_GENERATE_CAPTURES] = r.median_ns;
    }

    // 5. find_pins()
    {
        auto r = microbench([&]() {
            PinInfo pins = find_pins(board);
            g_sink = (int)pins.pinned;
        });
        prof.ns[C_FIND_PINS] = r.median_ns;
    }

    // 6. order_moves()
    {
        Engine eng;
        eng.tt.initialize();
        MoveList baseline;
        MoveGenerator gx;
        gx.generate_moves(board, baseline);

        if (baseline.size() > 0) {
            auto r = microbench([&]() {
                MoveList copy;
                memcpy(&copy, &baseline, sizeof(MoveList));
                eng.order_moves(copy, 0, 0, board.side_to_move == WHITE ? 0 : 1, board);
                g_sink = copy.scores[0];
            });
            auto copy_overhead = microbench([&]() {
                MoveList copy;
                memcpy(&copy, &baseline, sizeof(MoveList));
                g_sink = copy.scores[0];
            });
            prof.ns[C_ORDER_MOVES] = max(1.0, r.median_ns - copy_overhead.median_ns);
        }
    }

    // 7. make_move + unmake_move pair
    if (legal_moves.size() > 0) {
        int num_test = min(5, legal_moves.size());
        double total_ns = 0;
        for (int m = 0; m < num_test; m++) {
            uint16_t move = legal_moves.move_list[m];
            auto r = microbench([&]() {
                board.make_move(move);
                board.unmake_move();
            });
            total_ns += r.median_ns;
        }
        prof.ns[C_MAKE_UNMAKE] = total_ns / num_test;
    }

    // 8. see_full()
    if (cap_moves.size() > 0) {
        int num_test = min(5, cap_moves.size());
        double total_ns = 0;
        for (int m = 0; m < num_test; m++) {
            uint16_t move = cap_moves.move_list[m];
            auto r = microbench([&]() {
                g_sink = see_full(board, move);
            });
            total_ns += r.median_ns;
        }
        prof.ns[C_SEE_FULL] = total_ns / num_test;
    }

    // 9. TT.probe()
    {
        TranspositionTable tt;
        tt.initialize();
        tt.store(board.zobrist_hash, 5, 42, TT_EXACT,
                 legal_moves.size() > 0 ? legal_moves.move_list[0] : (uint16_t)0, 0);
        int result;
        uint16_t tt_move;
        auto r = microbench([&]() {
            g_sink = tt.probe(board.zobrist_hash, 3, -100000, 100000, result, tt_move, 0) ? 1 : 0;
        });
        prof.ns[C_TT_PROBE] = r.median_ns;
    }

    // 10. TT.store()
    {
        TranspositionTable tt;
        tt.initialize();
        uint16_t move = legal_moves.size() > 0 ? legal_moves.move_list[0] : (uint16_t)0;
        int depth_cycle = 0;
        auto r = microbench([&]() {
            tt.store(board.zobrist_hash + (depth_cycle++ & 0xFF), 5, 42, TT_EXACT, move, 0);
        });
        prof.ns[C_TT_STORE] = r.median_ns;
    }

    // 11. sort_mvv_lva()
    if (cap_moves.size() > 0) {
        MoveList baseline_caps;
        memcpy(&baseline_caps, &cap_moves, sizeof(MoveList));
        auto r = microbench([&]() {
            MoveList copy;
            memcpy(&copy, &baseline_caps, sizeof(MoveList));
            copy.sort_mvv_lva();
            g_sink = copy.scores[0];
        });
        auto copy_overhead = microbench([&]() {
            MoveList copy;
            memcpy(&copy, &baseline_caps, sizeof(MoveList));
            g_sink = copy.scores[0];
        });
        prof.ns[C_SORT_MVV_LVA] = max(1.0, r.median_ns - copy_overhead.median_ns);
    }

    // 12. should_stop()
    {
        Engine eng;
        eng.nodes_searched = 12345;
        eng.search_stopped = false;
        eng.time_limit_ms = 0;
        eng.nodes_limit = 0;
        eng.external_stop_flag = nullptr;
        auto r = microbench([&]() {
            g_sink = eng.should_stop() ? 1 : 0;
        });
        prof.ns[C_SHOULD_STOP] = r.median_ns;
    }

    return prof;
}

// ═══════════════════════════════════════════════════════════════════
// FULL SEARCH TIMING
// ═══════════════════════════════════════════════════════════════════

struct SearchRun {
    const char* pos_name;
    int depth;
    uint64_t nodes;
    double time_ms;
    double nps;
    uint64_t tt_hits;
    uint64_t tt_misses;
    int completed_depth;
};

SearchRun run_search(const char* name, const char* fen, int depth) {
    Board board;
    board.set_fen(fen);

    Engine engine;
    engine.configure_search(0, 30000, nullptr);
    engine.tt.clear();

    auto t0 = high_resolution_clock::now();
    uint16_t best = engine.best_move(board, depth);
    auto t1 = high_resolution_clock::now();

    double ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    SearchRun r;
    r.pos_name = name;
    r.depth = depth;
    r.nodes = engine.nodes_searched;
    r.time_ms = ms;
    r.nps = ms > 0 ? engine.nodes_searched * 1000.0 / ms : 0;
    r.tt_hits = engine.tt.total_hits.load();
    r.tt_misses = engine.tt.total_misses.load();
    r.completed_depth = engine.completed_depth;
    return r;
}

// ═══════════════════════════════════════════════════════════════════
// ESTIMATED BREAKDOWN MODEL
// ═══════════════════════════════════════════════════════════════════

struct BreakdownEntry {
    const char* name;
    uint64_t est_calls;
    double ns_per_call;
    double total_ms;
    double pct;
};

vector<BreakdownEntry> estimate_breakdown(
    const SearchRun& run,
    const double avg_ns[C_COUNT],
    int avg_move_count)
{
    uint64_t N = run.nodes;
    uint64_t tt_probes = run.tt_hits + run.tt_misses;

    // TT probed only in search(), not quiescence()
    uint64_t S = tt_probes;
    uint64_t Q = (N > S) ? N - S : 0;
    uint64_t S_movegen = (S > run.tt_hits) ? S - run.tt_hits : S;

    double eff_branch = min((double)avg_move_count, 8.0);
    double q_branch = 2.5;
    uint64_t make_unmake_calls = (uint64_t)(S_movegen * eff_branch + Q * q_branch);
    uint64_t see_calls = (uint64_t)(S_movegen * 2.0 + Q * 0.5);

    vector<BreakdownEntry> bd;
    auto add = [&](const char* name, uint64_t calls, double ns) {
        double total = calls * ns / 1e6;
        bd.push_back({name, calls, ns, total, 0});
    };

    add("evaluate()",          N,                avg_ns[C_EVALUATE]);
    add("is_in_check()",       S,                avg_ns[C_IS_IN_CHECK]);
    add("generate_moves()",    S_movegen,         avg_ns[C_GENERATE_MOVES]);
    add("generate_captures()", Q,                avg_ns[C_GENERATE_CAPTURES]);
    add("order_moves()",       S_movegen,         avg_ns[C_ORDER_MOVES]);
    add("make+unmake pair",    make_unmake_calls, avg_ns[C_MAKE_UNMAKE]);
    add("see_full()",          see_calls,          avg_ns[C_SEE_FULL]);
    add("TT.probe()",         tt_probes,          avg_ns[C_TT_PROBE]);
    add("TT.store()",         S_movegen,          avg_ns[C_TT_STORE]);
    add("sort_mvv_lva()",     Q,                 avg_ns[C_SORT_MVV_LVA]);
    add("should_stop()",      N / 65536 + 1,     avg_ns[C_SHOULD_STOP]);

    for (auto& e : bd) {
        e.pct = run.time_ms > 0 ? 100.0 * e.total_ms / run.time_ms : 0;
    }

    sort(bd.begin(), bd.end(), [](const BreakdownEntry& a, const BreakdownEntry& b) {
        return a.total_ms > b.total_ms;
    });

    return bd;
}

// ═══════════════════════════════════════════════════════════════════
// PRINTING HELPERS
// ═══════════════════════════════════════════════════════════════════

void print_sep(int w = 90) { for (int i = 0; i < w; i++) cout << '-'; cout << '\n'; }
void print_dsep(int w = 90) { for (int i = 0; i < w; i++) cout << '='; cout << '\n'; }

// ═══════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════

int main() {
    init_rays();
    init_magics();

    cout << '\n';
    print_dsep();
    cout << "             TIGERFISH SEARCH PROFILER v1.0\n";
    cout << "         Per-Component Microbenchmarks + Breakdown\n";
    print_dsep();
    cout << '\n';

    // ══════════════════════════════════════════════════
    // PHASE 1: MICROBENCHMARKS
    // ══════════════════════════════════════════════════

    cout << "PHASE 1: Microbenchmarking " << C_COUNT << " components on "
         << NUM_POS << " positions (" << MICRO_ITERS << " iters each)...\n\n";

    PosProfile profiles[NUM_POS];
    int avg_move_count = 0;

    for (int p = 0; p < NUM_POS; p++) {
        cout << "  [" << (p+1) << "/" << NUM_POS << "] " << POSITIONS[p].name << "...";
        cout.flush();
        profiles[p] = benchmark_position(POSITIONS[p].fen);
        avg_move_count += profiles[p].move_count;
        cout << " done (" << profiles[p].move_count << " moves, "
             << profiles[p].capture_count << " captures)\n";
    }
    avg_move_count /= NUM_POS;

    double avg_ns[C_COUNT] = {};
    for (int c = 0; c < C_COUNT; c++) {
        double sum = 0; int count = 0;
        for (int p = 0; p < NUM_POS; p++) {
            if (profiles[p].ns[c] > 0) { sum += profiles[p].ns[c]; count++; }
        }
        avg_ns[c] = count > 0 ? sum / count : 0;
    }

    // ── Per-Call Cost Table ──
    cout << '\n';
    print_dsep();
    cout << "  SECTION 1: PER-CALL COST (nanoseconds)\n";
    print_dsep();
    cout << left << setw(24) << "Component"
         << right << setw(10) << "Avg ns"
         << setw(10) << "Min ns"
         << setw(10) << "Max ns" << '\n';
    print_sep(54);

    for (int c = 0; c < C_COUNT; c++) {
        double cmin = 1e18, cmax = 0;
        for (int p = 0; p < NUM_POS; p++) {
            if (profiles[p].ns[c] > 0) {
                cmin = min(cmin, profiles[p].ns[c]);
                cmax = max(cmax, profiles[p].ns[c]);
            }
        }
        if (cmin > 1e17) cmin = 0;
        cout << fixed << setprecision(1);
        cout << left << setw(24) << COMP_NAMES[c]
             << right << setw(9) << avg_ns[c] << " "
             << setw(9) << cmin << " "
             << setw(9) << cmax << '\n';
    }

    // ── Cost Ranking ──
    struct RankedComp { int idx; double ns; };
    vector<RankedComp> ranked;
    for (int c = 0; c < C_COUNT; c++) {
        if (avg_ns[c] > 0) ranked.push_back({c, avg_ns[c]});
    }
    sort(ranked.begin(), ranked.end(), [](const RankedComp& a, const RankedComp& b) {
        return a.ns > b.ns;
    });

    cout << '\n';
    print_dsep();
    cout << "  SECTION 2: COST RANKING (most expensive per call first)\n";
    print_dsep();

    for (int i = 0; i < (int)ranked.size(); i++) {
        auto& r = ranked[i];
        int bar_len = max(1, min(40, (int)(r.ns / ranked[0].ns * 40)));
        string bar(bar_len, '#');
        cout << "  " << setw(2) << (i+1) << ". "
             << left << setw(24) << COMP_NAMES[r.idx]
             << right << setw(8) << fixed << setprecision(1) << r.ns << " ns  "
             << bar << '\n';
    }

    // ══════════════════════════════════════════════════
    // PHASE 2: FULL SEARCH RUNS
    // ══════════════════════════════════════════════════

    cout << "\n\n";
    print_dsep();
    cout << "  SECTION 3: FULL SEARCH RUNS (single-threaded)\n";
    print_dsep();

    struct SPos { const char* name; const char* fen; };
    SPos search_positions[] = {
        {"Starting Pos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"Sicilian",     "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
        {"Complex Mid",  "r1bq1rk1/ppp2ppp/2np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
        {"Kiwipete",     "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
    };
    int num_spos = sizeof(search_positions) / sizeof(search_positions[0]);
    int search_depths[] = {8, 10, 12, 14};
    int num_depths = 4;

    cout << left << setw(15) << "Position"
         << right << setw(6) << "Depth"
         << setw(14) << "Nodes"
         << setw(11) << "Time(ms)"
         << setw(14) << "NPS"
         << setw(9) << "TT Hit%"
         << setw(10) << "TT Probe"
         << setw(10) << "ns/node" << '\n';
    print_sep();

    vector<SearchRun> depth12_runs;

    for (int p = 0; p < num_spos; p++) {
        for (int d = 0; d < num_depths; d++) {
            cout.flush();
            SearchRun r = run_search(search_positions[p].name,
                                     search_positions[p].fen,
                                     search_depths[d]);

            double tt_pct = (r.tt_hits + r.tt_misses > 0)
                ? 100.0 * r.tt_hits / (r.tt_hits + r.tt_misses) : 0;
            double ns_node = r.nodes > 0 ? r.time_ms * 1e6 / r.nodes : 0;

            cout << fixed << setprecision(1);
            cout << left << setw(15) << r.pos_name
                 << right << setw(6) << r.completed_depth
                 << setw(14) << r.nodes
                 << setw(11) << r.time_ms
                 << setw(14) << (uint64_t)r.nps
                 << setw(8) << tt_pct << "%"
                 << setw(10) << (r.tt_hits + r.tt_misses)
                 << setw(10) << ns_node << '\n';

            if (search_depths[d] == 12) depth12_runs.push_back(r);
        }
        print_sep();
    }

    // ══════════════════════════════════════════════════
    // PHASE 3: ESTIMATED TIME BREAKDOWN
    // ══════════════════════════════════════════════════

    cout << '\n';
    print_dsep();
    cout << "  SECTION 4: ESTIMATED SEARCH TIME BREAKDOWN (depth 12)\n";
    cout << "  (Microbenchmark costs x estimated call counts from TT stats)\n";
    print_dsep();

    for (auto& run : depth12_runs) {
        auto bd = estimate_breakdown(run, avg_ns, avg_move_count);
        double total_accounted = 0;
        for (auto& e : bd) total_accounted += e.total_ms;

        cout << "\n  >>> " << run.pos_name << " | depth " << run.completed_depth
             << " | " << run.nodes << " nodes | " << fixed << setprecision(1)
             << run.time_ms << " ms | " << (uint64_t)run.nps << " NPS\n\n";

        cout << "  " << left << setw(24) << "Component"
             << right << setw(12) << "Est.Calls"
             << setw(10) << "ns/call"
             << setw(11) << "Total(ms)"
             << setw(9) << "% Search"
             << "  Visual\n";
        cout << "  "; print_sep(76);

        for (auto& e : bd) {
            if (e.total_ms < 0.01) continue;
            int bar_len = max(0, min(30, (int)(e.pct / 2.0)));
            string bar(bar_len, '#');

            cout << "  " << left << setw(24) << e.name
                 << right << setw(12) << e.est_calls
                 << setw(10) << fixed << setprecision(1) << e.ns_per_call
                 << setw(11) << fixed << setprecision(2) << e.total_ms
                 << setw(8) << fixed << setprecision(1) << e.pct << "%"
                 << "  " << bar << '\n';
        }

        cout << "  "; print_sep(76);
        cout << "  " << left << setw(24) << "ACCOUNTED"
             << right << setw(12) << "" << setw(10) << ""
             << setw(11) << fixed << setprecision(2) << total_accounted
             << setw(8) << fixed << setprecision(1)
             << (run.time_ms > 0 ? 100.0 * total_accounted / run.time_ms : 0) << "%\n";
        cout << "  " << left << setw(24) << "ACTUAL SEARCH"
             << right << setw(12) << "" << setw(10) << ""
             << setw(11) << fixed << setprecision(2) << run.time_ms << " 100.0%\n";
        double overhead = run.time_ms - total_accounted;
        cout << "  " << left << setw(24) << "OVERHEAD/CONTROL FLOW"
             << right << setw(12) << "" << setw(10) << ""
             << setw(11) << fixed << setprecision(2) << overhead
             << setw(8) << fixed << setprecision(1)
             << (run.time_ms > 0 ? 100.0 * overhead / run.time_ms : 0) << "%\n";
    }

    // ══════════════════════════════════════════════════
    // PHASE 4: BOTTLENECK ANALYSIS
    // ══════════════════════════════════════════════════

    cout << '\n';
    print_dsep();
    cout << "  SECTION 5: BOTTLENECK ANALYSIS & OPTIMIZATION TARGETS\n";
    print_dsep();

    double avg_pct[C_COUNT] = {};
    int bd_count = 0;
    for (auto& run : depth12_runs) {
        auto bd = estimate_breakdown(run, avg_ns, avg_move_count);
        for (auto& e : bd) {
            for (int c = 0; c < C_COUNT; c++) {
                if (strcmp(e.name, COMP_NAMES[c]) == 0) avg_pct[c] += e.pct;
            }
        }
        bd_count++;
    }
    if (bd_count > 0) for (int c = 0; c < C_COUNT; c++) avg_pct[c] /= bd_count;

    struct Target { int idx; double pct; double ns; };
    vector<Target> targets;
    for (int c = 0; c < C_COUNT; c++) {
        if (avg_pct[c] > 0.1) targets.push_back({c, avg_pct[c], avg_ns[c]});
    }
    sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) {
        return a.pct > b.pct;
    });

    for (int i = 0; i < (int)targets.size(); i++) {
        auto& t = targets[i];
        cout << "\n  #" << (i+1) << " ";
        if (t.pct >= 20)     cout << "[CRITICAL] ";
        else if (t.pct >= 10) cout << "[HIGH]     ";
        else if (t.pct >= 5)  cout << "[MEDIUM]   ";
        else                   cout << "[LOW]      ";

        cout << COMP_NAMES[t.idx] << "\n";
        cout << "     Avg " << fixed << setprecision(1) << t.pct
             << "% of search time | " << t.ns << " ns/call\n";

        if (t.idx == C_EVALUATE) {
            cout << "     -> Fix #2: Incremental PST accumulators in Board.make_move()\n";
            cout << "        Eliminates 12-piece bitboard scan + inner loop = O(1) eval\n";
        } else if (t.idx == C_GENERATE_MOVES) {
            cout << "     -> Fix #4: Cache PinInfo per ply (find_pins = "
                 << fixed << setprecision(1) << avg_ns[C_FIND_PINS] << " ns of "
                 << avg_ns[C_GENERATE_MOVES] << " ns total)\n";
            cout << "     -> Fix #7: Remove wasted MVV-LVA scoring at end\n";
        } else if (t.idx == C_GENERATE_CAPTURES) {
            cout << "     -> Fix #1: Pass in_check from parent, skip attack-map rebuild\n";
        } else if (t.idx == C_ORDER_MOVES) {
            cout << "     -> Guarded SEE already applied. Consider: lazy sort (pick-best)\n";
        } else if (t.idx == C_MAKE_UNMAKE) {
            cout << "     -> Already minimal. Fix #2 adds small PST overhead per move,\n";
            cout << "        but net positive since evaluate() savings dominate.\n";
        } else if (t.idx == C_SEE_FULL) {
            cout << "     -> Already guarded. Consider: precomputed attacker tables\n";
        } else if (t.idx == C_IS_IN_CHECK) {
            cout << "     -> Already king-centric with short-circuit. Near optimal.\n";
        } else if (t.idx == C_TT_PROBE || t.idx == C_TT_STORE) {
            cout << "     -> Fix #3: Flat TT array, eliminate shared_ptr dereference\n";
        } else if (t.idx == C_FIND_PINS) {
            cout << "     -> Fix #4: Cache per ply to avoid recomputation\n";
        }
    }

    // ── Theoretical NPS if component was free ──
    cout << "\n\n";
    print_dsep();
    cout << "  SECTION 6: THEORETICAL NPS IF COMPONENT WAS FREE\n";
    print_dsep();

    if (!depth12_runs.empty()) {
        auto& ref = depth12_runs[0];
        double actual_ns_node = ref.nodes > 0 ? ref.time_ms * 1e6 / ref.nodes : 1;
        double actual_nps = ref.nps;

        cout << "\n  Baseline: " << fixed << setprecision(0) << actual_nps << " NPS ("
             << setprecision(1) << actual_ns_node << " ns/node)\n\n";

        cout << "  " << left << setw(24) << "If eliminated..."
             << right << setw(11) << "Saved ns"
             << setw(13) << "New ns/node"
             << setw(14) << "New NPS"
             << setw(10) << "Speedup" << '\n';
        cout << "  "; print_sep(72);

        auto bd = estimate_breakdown(ref, avg_ns, avg_move_count);
        for (auto& e : bd) {
            if (e.total_ms < 0.1) continue;
            double saved_ns = ref.nodes > 0 ? e.total_ms * 1e6 / ref.nodes : 0;
            double new_ns = max(1.0, actual_ns_node - saved_ns);
            double new_nps = 1e9 / new_ns;
            double speedup = new_nps / actual_nps;

            cout << "  " << left << setw(24) << e.name
                 << right << setw(11) << fixed << setprecision(1) << saved_ns
                 << setw(13) << fixed << setprecision(1) << new_ns
                 << setw(14) << (uint64_t)new_nps
                 << setw(9) << fixed << setprecision(2) << speedup << "x" << '\n';
        }
    }

    cout << "\n";
    print_dsep();
    cout << "  PROFILING COMPLETE\n";
    print_dsep();
    cout << '\n';

    return 0;
}
