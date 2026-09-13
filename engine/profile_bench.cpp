// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — SEARCH PROFILER (newengine/profile_bench.cpp)
// Component microbenchmarking, search execution breakdown, and NPS profiling.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "bitboards.cpp"
#include "position.cpp"
#include "movegen.cpp"
#include "tt.cpp"
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

using namespace tigerfish;
using namespace std::chrono;

static const int MICRO_ITERS   = 200000;
static const int WARMUP_ITERS  = 20000;
static const int BENCH_SAMPLES = 5;

static volatile int g_sink = 0;

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
    {"Rook Endgame",         "4k3/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"},
    {"Pawn Endgame",         "8/4k3/8/4P3/8/8/4K3/8 w - - 0 1"},
    {"Kiwipete",             "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
};
static const int NUM_POS = sizeof(POSITIONS) / sizeof(POSITIONS[0]);

template<typename F>
double microbench(F&& func, int iters = MICRO_ITERS) {
    for (int i = 0; i < WARMUP_ITERS; i++) func();

    double times[BENCH_SAMPLES];
    for (int s = 0; s < BENCH_SAMPLES; s++) {
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < iters; i++) func();
        auto t1 = high_resolution_clock::now();
        times[s] = (double)duration_cast<nanoseconds>(t1 - t0).count() / iters;
    }
    std::sort(times, times + BENCH_SAMPLES);
    return times[BENCH_SAMPLES / 2]; // median
}

enum Component {
    C_EVALUATE = 0,
    C_GENERATE_LEGAL,
    C_GENERATE_CAPTURES,
    C_MAKE_UNDO,
    C_SEE,
    C_TT_PROBE,
    C_TT_STORE,
    C_COUNT
};

static const char* COMP_NAMES[C_COUNT] = {
    "evaluate()",
    "generate_legal_moves()",
    "generate_captures()",
    "make+undo pair",
    "see()",
    "probe_tt()",
    "store_tt()"
};

struct PosProfile {
    double ns[C_COUNT] = {};
    int move_count = 0;
    int capture_count = 0;
};

PosProfile benchmark_position(const char* fen, TranspositionTable& tt) {
    PosProfile prof;
    Position pos;
    StateInfo state;
    set_fen(pos, fen, state);

    MoveList moves;
    generate_legal_moves(pos, moves);
    prof.move_count = moves.size();

    MoveList caps;
    generate_captures(pos, caps);
    prof.capture_count = caps.size();

    // 1. evaluate()
    prof.ns[C_EVALUATE] = microbench([&]() { g_sink = evaluate(pos); });

    // 2. generate_legal_moves()
    prof.ns[C_GENERATE_LEGAL] = microbench([&]() {
        MoveList ml;
        generate_legal_moves(pos, ml);
        g_sink = ml.size();
    }, 100000);

    // 3. generate_captures()
    prof.ns[C_GENERATE_CAPTURES] = microbench([&]() {
        MoveList ml;
        generate_captures(pos, ml);
        g_sink = ml.size();
    }, 100000);

    // 4. make+undo pair
    if (!moves.empty()) {
        Move m = moves[0];
        prof.ns[C_MAKE_UNDO] = microbench([&]() {
            StateInfo st;
            make_move(pos, m, st);
            undo_move(pos, m);
        });
    }

    // 5. see()
    if (!caps.empty()) {
        Move m = caps[0];
        prof.ns[C_SEE] = microbench([&]() { g_sink = see(pos, m); });
    } else if (!moves.empty()) {
        Move m = moves[0];
        prof.ns[C_SEE] = microbench([&]() { g_sink = see(pos, m); });
    }

    // 6. TT probe & store
    TTEntry entry;
    uint64_t key = pos.state->zobrist_key;

    prof.ns[C_TT_STORE] = microbench([&]() {
        store_tt(tt, key, moves.empty() ? MOVE_NONE : moves[0], 25, 25, 6, BOUND_EXACT, 0);
    });

    prof.ns[C_TT_PROBE] = microbench([&]() {
        g_sink = probe_tt(tt, key, entry);
    });

    return prof;
}

int main() {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

    TranspositionTable tt;
    init_tt(tt, 64);

    std::cout << "\n";
    std::cout << "=========================================================================================\n";
    std::cout << "                 TIGERFISH NEWENGINE — SEARCH COMPONENT PROFILER                         \n";
    std::cout << "=========================================================================================\n\n" << std::flush;

    std::cout << "[1] MICROBENCHMARK RESULTS (NANOSECONDS PER INVOCATION)\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(24) << "Position"
              << std::right
              << std::setw(10) << "eval"
              << std::setw(12) << "legalgen"
              << std::setw(10) << "capgen"
              << std::setw(12) << "make/undo"
              << std::setw(9)  << "see"
              << std::setw(9)  << "tt_probe"
              << std::setw(9)  << "tt_store"
              << "\n";
    std::cout << "-----------------------------------------------------------------------------------------\n" << std::flush;

    double avg_ns[C_COUNT] = {};

    for (int i = 0; i < NUM_POS; i++) {
        PosProfile p = benchmark_position(POSITIONS[i].fen, tt);
        std::cout << std::left << std::setw(24) << POSITIONS[i].name << std::right << std::fixed << std::setprecision(1)
                  << std::setw(10) << p.ns[C_EVALUATE]
                  << std::setw(12) << p.ns[C_GENERATE_LEGAL]
                  << std::setw(10) << p.ns[C_GENERATE_CAPTURES]
                  << std::setw(12) << p.ns[C_MAKE_UNDO]
                  << std::setw(9)  << p.ns[C_SEE]
                  << std::setw(9)  << p.ns[C_TT_PROBE]
                  << std::setw(9)  << p.ns[C_TT_STORE]
                  << "\n" << std::flush;

        for (int c = 0; c < C_COUNT; c++) {
            avg_ns[c] += p.ns[c] / NUM_POS;
        }
    }

    std::cout << "-----------------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(24) << "AVERAGE" << std::right << std::fixed << std::setprecision(1)
              << std::setw(10) << avg_ns[C_EVALUATE]
              << std::setw(12) << avg_ns[C_GENERATE_LEGAL]
              << std::setw(10) << avg_ns[C_GENERATE_CAPTURES]
              << std::setw(12) << avg_ns[C_MAKE_UNDO]
              << std::setw(9)  << avg_ns[C_SEE]
              << std::setw(9)  << avg_ns[C_TT_PROBE]
              << std::setw(9)  << avg_ns[C_TT_STORE]
              << "\n\n";

    std::cout << "[2] FULL SEARCH BENCHMARK AT DEPTH 12\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(24) << "Position"
              << std::right
              << std::setw(10) << "BestMove"
              << std::setw(8)  << "Score"
              << std::setw(12) << "Nodes"
              << std::setw(10) << "Time(ms)"
              << std::setw(14) << "NPS"
              << std::setw(10) << "TT Hits"
              << "\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";

    clear_tt(tt);

    uint64_t total_nodes = 0;
    int total_time_ms = 0;
    uint64_t total_hits = 0;

    for (int i = 0; i < NUM_POS; i++) {
        clear_tt(tt);
        Position pos;
        StateInfo state;
        set_fen(pos, POSITIONS[i].fen, state);

        SearchLimits limits;
        limits.depth = 12;

        SearchResult res = search_position(pos, limits, tt);
        total_nodes += res.nodes;
        total_time_ms += res.time_ms;
        total_hits += tt.hits;

        std::cout << std::left << std::setw(24) << POSITIONS[i].name
                  << std::right
                  << std::setw(10) << res.best_move.to_uci()
                  << std::setw(8)  << res.score
                  << std::setw(12) << res.nodes
                  << std::setw(10) << res.time_ms
                  << std::setw(14) << res.nps
                  << std::setw(10) << tt.hits
                  << "\n" << std::flush;
    }

    double overall_nps = (total_time_ms > 0) ? (total_nodes * 1000.0 / total_time_ms) : 0;
    std::cout << "-----------------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(24) << "TOTAL / OVERALL"
              << std::right
              << std::setw(10) << "-"
              << std::setw(8)  << "-"
              << std::setw(12) << total_nodes
              << std::setw(10) << total_time_ms
              << std::setw(14) << (uint64_t)overall_nps
              << std::setw(10) << total_hits
              << "\n\n";

    std::cout << "[3] ESTIMATED EXECUTION TIME BREAKDOWN IN TYPICAL SEARCH\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";
    // Estimated relative weights based on node profile
    // 1 node = 1 TT probe, 0.6 evaluate, 0.4 legalgen, 0.8 capgen, 1.2 make/undo, 0.5 SEE, 0.4 TT store
    double w_eval    = 0.6 * avg_ns[C_EVALUATE];
    double w_gen     = 0.4 * avg_ns[C_GENERATE_LEGAL];
    double w_capgen  = 0.8 * avg_ns[C_GENERATE_CAPTURES];
    double w_make    = 1.2 * avg_ns[C_MAKE_UNDO];
    double w_see     = 0.5 * avg_ns[C_SEE];
    double w_probe   = 1.0 * avg_ns[C_TT_PROBE];
    double w_store   = 0.4 * avg_ns[C_TT_STORE];
    double w_total   = w_eval + w_gen + w_capgen + w_make + w_see + w_probe + w_store;

    auto print_bar = [](const char* name, double ns, double total) {
        double pct = (ns / total) * 100.0;
        int bars = (int)(pct / 2.5);
        std::string bar_str(bars, '#');
        std::cout << "  " << std::left << std::setw(24) << name
                  << std::right << std::fixed << std::setprecision(1)
                  << std::setw(7) << ns << " ns  ("
                  << std::setw(5) << pct << "%)  ["
                  << std::left << std::setw(40) << bar_str << "]\n";
    };

    print_bar("Evaluation (evaluate)", w_eval, w_total);
    print_bar("Move Generation (legal)", w_gen, w_total);
    print_bar("Capture Gen (qsearch)", w_capgen, w_total);
    print_bar("Make/Undo State Mutation", w_make, w_total);
    print_bar("SEE Tactical Filtering", w_see, w_total);
    print_bar("Transposition Probe", w_probe, w_total);
    print_bar("Transposition Store", w_store, w_total);

    std::cout << "-----------------------------------------------------------------------------------------\n";
    std::cout << "  Estimated Node Latency: " << std::fixed << std::setprecision(1) << w_total << " ns/node "
              << "(Theor. Peak NPS: " << (uint64_t)(1e9 / w_total) << ")\n";
    std::cout << "=========================================================================================\n\n";

    free_tt(tt);
    return 0;
}
