// ═══ NEW MATCH MAKER ══════════════════════════════════════════════
// Clean copy of Matchmaker_fixed.cpp with:
//   - Eval perspective fix (white-positive, not side-to-move)
//   - Clean output (no \r, no UCI artifacts)
//   - Proper per-engine node/time tracking
//   - Auto-calculated position count
//   - Deduplicated FENs
//
// Usage: ./NewMatchmaker.exe [depth] [threads]
//   depth   = search depth (default 5)
//   threads = parallel threads (default 24)
// ════════════════════════════════════════════════════════════════════

#include "../../engine/oldsearch.cpp"
#include "../../engine/search.cpp"
#include <mutex>
#include <thread>

// ── Test positions (deduplicated) ─────────────────────────────────
static const char* POSITIONS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkbnr/pppppppp/2n5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 2 2",
    "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 3",
    "r1bqkb1r/pppppppp/2n2n2/8/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "r2qkb1r/pp2pppp/2n2n2/2pp1b2/3P4/2N2NP1/PPP1PPBP/R1BQK2R w KQkq - 4 5",
    "r1bqkb1r/pppppppp/2n2n2/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq d3 0 3",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 1 3",
    "r1bqkb1r/pp1ppppp/2n2n2/2p5/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R b KQkq d3 0 4",
    "rnbq1k1r/pppp1ppp/5n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQ - 4 4",
    "rnbq1k1r/ppp1ppbp/3p1np1/8/2PPP3/2N2N2/PP2BPPP/RNBQK2R w KQ - 0 6",
    "r1bq1rk1/pppnnppp/3p4/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQ1RK1 w - - 0 7",
    "r1bq1rk1/pppnnppp/3p4/2b1p3/2BPP3/5N2/PPP2PPP/RNBQ1RK1 b - d3 0 7",
    "r2q1rk1/pppb1ppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R1BQ1RK1 w - - 0 8",
    "r2q1rk1/ppp2ppp/2npbn2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7",
    "r2q1rk1/pppbbppp/2n1pn2/3p4/2PP4/2N1PN1P/PP2BPP1/R1BQ1RK1 w - - 0 9",
    "r3kb1r/ppp1pppp/2n2n2/3p1q2/3P1Bb1/2N2N2/PPP2PPP/R2QKB1R w KQkq - 5 6",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r1b3kr/pppn1p1p/3p4/8/3BP3/5N2/PPP2PPP/R3K2R w KQ - 0 10",
    "r2qkb1r/pp2pppp/2n2n2/2pp1b2/3P1B2/2N2NP1/PPP1PPBP/R2QK2R w KQkq - 4 6",
    "r1bqkb1r/pp3ppp/2n1pn2/2pp4/3P4/3BPN2/PPP2PPP/RNBQK2R w KQkq d6 0 5",
    "r1bqkb1r/pp3ppp/2n1pn2/2pp4/2PP4/4PN2/PP2BPPP/RNBQK2R w KQkq d6 0 5",
    "rnbqkb1r/pp1ppppp/5n2/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "r1bqkb1r/pp1ppppp/2n2n2/2p5/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 3 3",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "r1bqkbnr/ppp1pppp/2n5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq d6 0 3",
    "r1bqkb1r/pppppppp/2n2n2/8/3P4/5N2/PPP1PPPP/RNBQKB1R w KQkq - 1 3",
    "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 4",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQkq - 0 4",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK1NR b KQkq - 3 3",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2BPP3/5N2/PPP1NPPP/RNBQK2R b KQkq d3 0 4",
    "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK1NR w KQ - 4 4",
    "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK1NR b KQ d3 0 4",
    "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/2BPP3/5N2/PPP1NPPP/RNBQK2R b KQ d3 0 4",
    "r1bq1rk1/pppp1ppp/2n2n2/2b1pP2/2B1P3/5N2/PPP2PPP/RNBQK2R b KQ - 0 5",
    "r1bq1rk1/ppp2ppp/2n2n2/2b1pp2/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ f6 0 5",
    "r1bq1rk1/ppp2ppp/2n2n2/2bpp3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/ppppnppp/4pn2/2b5/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/pppp1ppp/2n2n2/2b1p3/1bBPP3/5N2/PPP2PPP/RNBQK2R w KQ - 5 5",
    "r1bq1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/ppp2ppp/2n2n2/2bpp3/2B1P3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/pppn1ppp/4pn2/2b5/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/ppp1bppp/2np1n2/2b1p3/2B1P3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/pppn1ppp/2np4/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r2q1rk1/ppp2ppp/2npbn2/2b1p3/2B1P3/5N2/PPP2PPP/RNBQK2R w KQ - 0 6",
    "r2q1rk1/pppbbppp/2np1n2/4p3/2B1P3/5N2/PPP2PPP/RNBQK2R w KQ - 0 6",
    "r2q1rk1/pppbbppp/2np1n2/2P1p3/4P3/5N2/PPP2PPP/RNBQK2R w KQ - 0 6",
    "r2q1rk1/ppp1bppp/2np1n2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 6",
    "r2q1rk1/ppp1bppp/2np1n2/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R w KQ - 0 6",
    "r1bq1rk1/1pp2ppp/p1np1n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/1ppnbppp/p2p1n2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/1ppn1ppp/p2pbn2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/1pp2ppp/p1npbn2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/2p1bppp/p1np1n2/1p2p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/2pnbppp/p2p1n2/1p2p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r1bq1rk1/2pnbppp/p2ppn2/1p6/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 5",
    "r2q1rk1/bpp1bppp/p1np1n2/1p2p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 6",
    "r2q1rk1/bpp1bppp/p1npbn2/4p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 6",
    "r2q1rk1/bppn1ppp/p2pbn2/4p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQ - 0 6",
    "r1b2rk1/2pp1ppp/p1n5/1p2p3/PPbPP3/5N2/2P2PPP/R1BQ1RK1 w - - 0 10",
    "r1bq1rk1/2ppbppp/p4n2/1p2p3/PPbPP3/5N2/2P2PPP/R1BQ1RK1 w - - 0 10",
    "r1bq1rk1/2pp1ppp/p1n2n2/1pb1p3/PPbPP3/5N2/2P2PPP/R1BQ1RK1 w - - 0 10",
    "r1bq1rk1/2ppbppp/p4n2/1pbpp3/PP1PP3/5N2/2P2PPP/R1BQ1RK1 w - - 0 10",
    "r2q1rk1/1bp1bppp/p1np1n2/1p2p3/PP1PP3/5N2/2P2PPP/RNBQR1K1 w - - 0 10",
    "r2q1rk1/1bp1bppp/p1np1n2/1p2p3/PP1PP3/2N2N2/2P2PPP/R1BQR1K1 w - - 0 10",
    "r2q1rk1/1bp1bppp/p1np1n2/1p2p3/PP1PP3/3B1N2/2P2PPP/RN1QR1K1 w - - 0 10",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/PPBPP3/5N2/2P2PPP/RNBQ1RK1 b - a3 0 5",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/2PPP3/5N2/PPB2PPP/RNBQ1RK1 b - d3 0 5",
    "r2q1rk1/1bp1bppp/p1np1n2/1p2p3/2PPP3/1P3N2/PBP2PPP/RN1QR1K1 w - - 0 10",
    "r2q1rk1/1bp1bppp/p1np1n2/1p2p3/PP1PP3/5N2/1BP2PPP/RN1QR1K1 w - - 0 10",
    "r2q1rk1/1bp1bppp/p1np1n2/1p2p3/PP1PP3/5N2/1BP2PPP/RN1Q1RK1 w - - 0 10",
    "r2q1rk1/1bp1bppp/p2p1n2/1p1np3/PP1PP3/5N2/1BP2PPP/RN1QR1K1 w - - 0 10",
    "r2q1rk1/1bp1bppp/pn1p1n2/1p2p3/PP1PP3/5N2/1BP2PPP/RN1QR1K1 w - - 0 10",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/PPBPP3/2N2N2/2P2PPP/R1BQ1RK1 b - a3 0 5",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/2PPP3/2N2N2/PPB2PPP/R1BQ1RK1 b - d3 0 5",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/PP1PP3/2N2N2/2PB1PPP/R1BQ1RK1 b - d3 0 5",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/PP1PP3/2N2N2/1PB2PPP/R1BQ1RK1 b - d3 0 5",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/PP1PP3/2N1BN2/2P2PPP/R1BQ1RK1 b - d3 0 5",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/PP1PP3/2N2N2/2P1BPPP/R1BQ1RK1 b - d3 0 5",
    "r1bq1rk1/1pp1bppp/p1np1n2/2b1p3/PP1PP3/2N2N2/2P2PPP/R1BQB1RK1 b - d3 0 5",
};

static constexpr int NUM_POSITIONS = sizeof(POSITIONS) / sizeof(POSITIONS[0]);

// ── Mutex for sequential logging ─────────────────────────────────
static mutex log_mutex;

// ── Per-engine node/time tracking ────────────────────────────────
struct MatchGameResult {
    string result;           // "1-0", "0-1", "1/2-1/2"
    int half_moves;
    long long time_ms;
    uint64_t white_nodes;
    uint64_t black_nodes;
    long long white_time_ms;
    long long black_time_ms;
    string final_reason;
};

// ── Play a single game ───────────────────────────────────────────
template<typename WhiteEngine, typename BlackEngine>
MatchGameResult play_game(const char* fen, int search_depth, bool log_moves, const string& label) {
    Board board;
    board.set_fen(fen);

    WhiteEngine white_engine;
    BlackEngine black_engine;
    white_engine.tt.clear();
    black_engine.tt.clear();

    auto start = chrono::steady_clock::now();

    int half_moves = 0;
    uint64_t w_nodes = 0, b_nodes = 0;
    long long w_time = 0, b_time = 0;

    for (int move_num = 0; move_num < 200; move_num++) {
        bool white_to_move = (board.side_to_move == WHITE);

        MoveList moves;
        MoveGenerator gen;
        gen.generate_moves(board, moves);
        if (moves.size() == 0) break;

        auto move_start = chrono::steady_clock::now();
        uint32_t best = 0;

        if (white_to_move) {
            best = white_engine.best_move(board, search_depth);
        } else {
            best = black_engine.best_move(board, search_depth);
        }

        auto move_end = chrono::steady_clock::now();
        long long move_ms = chrono::duration_cast<chrono::milliseconds>(move_end - move_start).count();

        uint64_t move_nodes;
        if (white_to_move) {
            move_nodes = white_engine.nodes_searched;
            w_nodes += move_nodes;
            w_time += move_ms;
        } else {
            move_nodes = black_engine.nodes_searched;
            b_nodes += move_nodes;
            b_time += move_ms;
        }

        if (best == 0) break;

        string uci = move_to_uci(best);

        if (log_moves) {
            lock_guard<mutex> lock(log_mutex);
            cout << "  " << label << " "
                 << uci
                 << " [" << move_nodes << " nodes, " << move_ms << "ms]"
                 << endl;
        }

        board.make_move(best);
        half_moves++;

        if (board.is_repetition(3)) break;
        if (board.halfmove_clock >= 100) break;
        if (half_moves >= 150) break;
    }

    auto end = chrono::steady_clock::now();
    long long total_ms = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    // Determine result
    string result;
    string reason;

    MoveList final_moves;
    MoveGenerator final_gen;
    final_gen.generate_moves(board, final_moves);

    if (final_moves.size() == 0) {
        if (is_in_check(board)) {
            result = (board.side_to_move == WHITE) ? "0-1" : "1-0";
            reason = "checkmate";
        } else {
            result = "1/2-1/2";
            reason = "stalemate";
        }
    } else if (board.is_repetition(3)) {
        result = "1/2-1/2";
        reason = "3fold repetition";
    } else if (board.halfmove_clock >= 100) {
        result = "1/2-1/2";
        reason = "50-move rule";
    } else if (half_moves >= 150) {
        result = "1/2-1/2";
        reason = "move limit";
    } else {
        result = "1/2-1/2";
        reason = "unknown";
    }

    return {result, half_moves, total_ms, w_nodes, b_nodes, w_time, b_time, reason};
}

// ── Thread worker: play 2 games for 1 position ──────────────────
struct PositionResult {
    int pos_index;
    string fen;
    MatchGameResult game1;  // Enhanced=White
    MatchGameResult game2;  // Old=White
    double enhanced_score;
    double old_score;
};

void play_position(PositionResult& pr, int pos_index, const char* fen, int depth) {
    pr.pos_index = pos_index;
    pr.fen = fen;
    pr.enhanced_score = 0.0;
    pr.old_score = 0.0;

    // Game 1: Enhanced=White, Old=Black
    pr.game1 = play_game<Engine, OldEngine>(fen, depth, true,
        "Pos" + to_string(pos_index + 1) + "-G1(E=W)");

    if (pr.game1.result == "1-0") { pr.enhanced_score += 1.0; }
    else if (pr.game1.result == "0-1") { pr.old_score += 1.0; }
    else { pr.enhanced_score += 0.5; pr.old_score += 0.5; }

    // Game 2: Old=White, Enhanced=Black
    pr.game2 = play_game<OldEngine, Engine>(fen, depth, true,
        "Pos" + to_string(pos_index + 1) + "-G2(O=W)");

    if (pr.game2.result == "1-0") { pr.old_score += 1.0; }
    else if (pr.game2.result == "0-1") { pr.enhanced_score += 1.0; }
    else { pr.enhanced_score += 0.5; pr.old_score += 0.5; }
}

// ── Main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    int search_depth = 5;
    int num_threads = 24;

    if (argc > 1) search_depth = atoi(argv[1]);
    if (argc > 2) num_threads = atoi(argv[2]);

    int total_games = NUM_POSITIONS * 2;

    cout << "============================================" << endl;
    cout << "  ENHANCED vs OLD — MATCHMAKER" << endl;
    cout << "============================================" << endl;
    cout << "Positions : " << NUM_POSITIONS << endl;
    cout << "Games     : " << total_games << " (" << NUM_POSITIONS << " x 2 with side swap)" << endl;
    cout << "Depth     : " << search_depth << endl;
    cout << "Threads   : " << num_threads << endl;
    cout << "============================================" << endl;
    cout << endl;

    vector<PositionResult> results(NUM_POSITIONS);

    for (int batch = 0; batch < NUM_POSITIONS; batch += num_threads) {
        int batch_end = min(batch + num_threads, NUM_POSITIONS);
        vector<thread> threads;

        for (int i = batch; i < batch_end; i++) {
            threads.emplace_back(play_position, ref(results[i]), i, POSITIONS[i], search_depth);
        }

        for (auto& t : threads) t.join();

        // Print results for this batch
        for (int i = batch; i < batch_end; i++) {
            auto& r = results[i];
            lock_guard<mutex> lock(log_mutex);

            cout << "--- Position " << (i + 1) << "/" << NUM_POSITIONS << " ---" << endl;
            cout << "  FEN: " << r.fen << endl;
            cout << "  G1 (Enh=W): " << r.game1.result
                 << " | " << r.game1.half_moves << " half-moves"
                 << " | " << r.game1.time_ms << "ms"
                 << " | Enh:" << r.game1.white_nodes
                 << " Old:" << r.game1.black_nodes
                 << " | " << r.game1.final_reason << endl;
            cout << "  G2 (Old=W): " << r.game2.result
                 << " | " << r.game2.half_moves << " half-moves"
                 << " | " << r.game2.time_ms << "ms"
                 << " | Old:" << r.game2.white_nodes
                 << " Enh:" << r.game2.black_nodes
                 << " | " << r.game2.final_reason << endl;
            cout << "  Score: Enhanced=" << r.enhanced_score
                 << " Old=" << r.old_score << endl;
            cout << endl;
        }

        cout << ">>> Batch " << (batch / num_threads + 1) << " done ("
             << batch_end << "/" << NUM_POSITIONS << " positions)" << endl;
        cout << endl;
    }

    // ── Final summary ──────────────────────────────────────────────
    double enhanced_total = 0.0, old_total = 0.0;
    uint64_t enhanced_nodes = 0, old_nodes = 0;
    long long enhanced_time = 0, old_time = 0;
    int enhanced_wins = 0, old_wins = 0, draws = 0;

    for (auto& r : results) {
        enhanced_total += r.enhanced_score;
        old_total += r.old_score;

        enhanced_nodes += r.game1.white_nodes + r.game2.black_nodes;
        old_nodes      += r.game1.black_nodes + r.game2.white_nodes;
        enhanced_time  += r.game1.white_time_ms + r.game2.black_time_ms;
        old_time       += r.game1.black_time_ms + r.game2.white_time_ms;

        if (r.game1.result == "1-0") enhanced_wins++;
        else if (r.game1.result == "0-1") old_wins++;
        else draws++;

        if (r.game2.result == "1-0") old_wins++;
        else if (r.game2.result == "0-1") enhanced_wins++;
        else draws++;
    }

    cout << "============================================" << endl;
    cout << "           FINAL RESULTS" << endl;
    cout << "============================================" << endl;
    cout << "  Enhanced wins : " << enhanced_wins << endl;
    cout << "  Draws         : " << draws << endl;
    cout << "  Old wins      : " << old_wins << endl;
    cout << "  Score         : Enhanced " << enhanced_total
         << " - " << old_total << " Old" << endl;
    cout << "  (out of " << total_games << " games)" << endl;
    cout << endl;
    cout << "  Avg nodes/move: Enhanced=" << (total_games > 0 ? enhanced_nodes / total_games : 0)
         << "  Old=" << (total_games > 0 ? old_nodes / total_games : 0) << endl;
    cout << "  Avg time/move : Enhanced=" << (total_games > 0 ? enhanced_time / total_games : 0) << "ms"
         << "  Old=" << (total_games > 0 ? old_time / total_games : 0) << "ms" << endl;
    cout << "  Total nodes   : Enhanced=" << enhanced_nodes
         << "  Old=" << old_nodes << endl;
    cout << "============================================" << endl;

    return 0;
}
