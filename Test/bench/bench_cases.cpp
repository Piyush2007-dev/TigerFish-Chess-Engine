// ═══ SHARED BENCHMARK TEST CASES ══════════════════════════════════
// 10 chess positions (startpos, openings, endgames, tactical) used by
// bench_new.cpp and bench_old.cpp. NOT compiled directly — included only.
// ════════════════════════════════════════════════════════════════════
struct TestCase {
    string name;
    string fen;
    int depth;
};

vector<TestCase> get_test_cases() {
    return {
        {"startpos_d6",  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6},
        {"sicilian_d5",  "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", 5},
        {"italian_d5",   "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3", 5},
        {"queen_gambit", "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", 5},
        {"midgame",      "r1bqkb1r/pppppppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 4", 6},
        {"rook_endgame", "8/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1", 7},
        {"mate_in_1",    "6k1/5ppp/8/8/8/8/8/1R4K1 w - - 0 1", 4},
        {"mate_in_2",    "2k5/8/1Q6/8/8/8/8/4K2R w K - 0 1", 6},
        {"complex_mid",  "r1bq1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7", 6},
        {"pawn_endgame", "8/4k3/8/4P3/8/8/4K3/8 w - - 0 1", 8},
    };
}

void run_benchmark(const string& label) {
    init_rays();
    init_magics();

    auto tests = get_test_cases();

    cout << "\n=== " << label << " ===" << endl;
    cout << left << setw(20) << "Test"
         << right << setw(8) << "Depth"
         << setw(12) << "Nodes"
         << setw(12) << "Time(ms)"
         << setw(14) << "NPS"
         << setw(12) << "Score"
         << setw(14) << "Best Move"
         << setw(10) << "DepthDone"
         << endl;
    cout << string(102, '-') << endl;

    uint64_t total_nodes = 0;
    double total_time = 0;
    int tests_completed = 0;

    for (auto& tc : tests) {
        Board board;
        board.set_fen(tc.fen);

        Engine engine;
        engine.configure_search(0, 10000, nullptr);

        auto start = chrono::high_resolution_clock::now();
        uint32_t mv = engine.best_move(board, tc.depth);
        auto end = chrono::high_resolution_clock::now();
        double ms = chrono::duration_cast<chrono::microseconds>(end - start).count() / 1000.0;

        int score = engine.root_scores.empty() ? 0 : engine.root_scores[0];
        uint64_t nodes = engine.nodes_searched;
        uint64_t nps = ms > 0 ? (uint64_t)(nodes * 1000.0 / ms) : nodes;

        total_nodes += nodes;
        total_time += ms;
        if (engine.completed_depth > 0) tests_completed++;

        cout << left << setw(20) << tc.name
             << right << setw(8) << tc.depth
             << setw(12) << nodes
             << setw(12) << fixed << setprecision(1) << ms
             << setw(14) << nps
             << setw(12) << score
             << setw(14) << (mv ? move_to_uci(mv) : "none")
             << setw(10) << engine.completed_depth
             << endl;
    }

    cout << string(102, '-') << endl;
    cout << left << setw(20) << "TOTAL"
         << right << setw(8) << ""
         << setw(12) << total_nodes
         << setw(12) << fixed << setprecision(1) << total_time
         << setw(14) << (total_time > 0 ? (uint64_t)(total_nodes * 1000.0 / total_time) : 0)
         << setw(12) << ""
         << setw(14) << ""
         << setw(10) << (to_string(tests_completed) + "/" + to_string(tests.size()))
         << endl;
}
