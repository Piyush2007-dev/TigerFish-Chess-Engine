// ═══ STANDALONE BENCHMARK ═══════════════════════════════════════
// Runs enhanced vs old engine on 10 positions, prints node/time/NPS.
// All engine files included locally (self-contained, no external deps).
// Compile: g++ -O3 -std=c++20 -o newbenchmark.exe newbenchmark.cpp
// Run:     ./newbenchmark.exe

#include "magic_lut.cpp"
#include "board.cpp"
#include "rules.cpp"
#include "eval.cpp"

#include "search.cpp"


#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>
#include <algorithm>

using namespace std;

// ============================================================
// CONFIG
// ============================================================

static constexpr int FIRST_DEPTH = 6;
static constexpr int LAST_DEPTH  = 7;

// Number of self-play games per depth.
// For serious results, increase to 50/100+.
static constexpr int GAMES_PER_DEPTH = 100;

// Maximum half-moves before declaring a draw.
static constexpr int MAX_HALF_MOVES = 150;

// ------------------------------------------------------------
// Test positions
// Same idea as your Matchmaker positions, but for NEW vs NEW.
// Add/remove positions as needed.
// ------------------------------------------------------------

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
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK1NR b KQkq - 0 4",
    "rnbq1k1r/pppp1ppp/5n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQ - 4 4",
    "rnbq1k1r/ppp1ppbp/3p1np1/8/2PPP3/2N2N2/PP2BPPP/RNBQK2R w KQ - 0 6",
    "r1bq1rk1/pppnnppp/3p4/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQ1RK1 w - - 0 7",
    "r1bq1rk1/pppnnppp/3p4/2b1p3/2BPP3/5N2/PPP2PPP/RNBQ1RK1 b - d3 0 7",
    "r2q1rk1/pppb1ppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R1BQ1RK1 w - - 0 8",
    "r2q1rk1/ppp2ppp/2npbn2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7",
    "r2q1rk1/pppbbppp/2n1pn2/3p4/2PP4/2N1PN1P/PP2BPP1/R1BQ1RK1 w - - 0 9",
    "r3kb1r/ppp1pppp/2n2n2/3p1q2/3P1Bb1/2N2N2/PPP2PPP/R2QKB1R w KQkq - 5 6",
    "r3kb1r/ppp1pppp/5n2/3p1q2/3PnBb1/2N2N2/PPP2PPP/R2QKB1R w KQkq - 7 7",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1b3kr/pppn1p1p/3p4/8/3BP3/5N2/PPP2PPP/R3K2R w KQ - 0 10",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQK2R w KQkq d3 0 4",
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
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2BPP3/5N2/PPPP1PPP/RNBQK1NR b KQkq - 0 4",
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
    "r1b2rk1/2ppbppp/p4n2/1p2p3/PPbPP3/5N2/2P2PPP/R1BQ1RK1 w - - 0 10",
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

static constexpr int NUM_POSITIONS =
    sizeof(POSITIONS) / sizeof(POSITIONS[0]);


// ============================================================
// STATISTICS
// ============================================================

struct SearchStats {
    uint64_t nodes = 0;
    uint64_t tt_hits = 0;
    uint64_t tt_misses = 0;

    long long time_ms = 0;

    int searches = 0;
    int depth_sum = 0;
    int max_completed_depth = 0;

    long long total_move_ms = 0;
    long long max_move_ms = 0;

    uint64_t max_nodes_move = 0;

    void add_search(const Engine& engine, long long move_ms) {

        nodes += engine.nodes_searched;

        tt_hits += engine.tt.total_hits;
        tt_misses += engine.tt.total_misses;

        time_ms += move_ms;

        searches++;
        depth_sum += engine.completed_depth;

        max_completed_depth =
            max(max_completed_depth, engine.completed_depth);

        total_move_ms += move_ms;
        max_move_ms = max(max_move_ms, move_ms);

        max_nodes_move =
            max(max_nodes_move, engine.nodes_searched);
    }
};


// ============================================================
// PERFT
// ============================================================
//
// This uses the SAME MoveGenerator + Board make/unmake logic,
// but completely bypasses search/evaluation.
//
// Therefore:
//
// perft = move generation / make_move / unmake_move test
//
// It does NOT measure evaluation quality.
// It does NOT measure alpha-beta search.
// ============================================================

uint64_t perft(Board& board, int depth) {

    if (depth == 0)
        return 1;

    MoveList moves;
    MoveGenerator generator;

    generator.generate_moves(board, moves);

    uint64_t nodes = 0;

    for (int i = 0; i < moves.size(); ++i) {

        board.make_move(moves.move_list[i]);

        nodes += perft(board, depth - 1);

        board.unmake_move();
    }

    return nodes;
}


// ============================================================
// SELF PLAY RESULT
// ============================================================

enum class Result {
    WHITE_WIN,
    BLACK_WIN,
    DRAW
};

struct GameStats {

    Result result = Result::DRAW;

    int half_moves = 0;

    uint32_t final_best_move = 0;

    SearchStats white;
    SearchStats black;

    string reason;

    long long game_time_ms = 0;

    int final_eval = 0;
};


// ============================================================
// PLAY ONE NEW vs NEW GAME
// ============================================================

GameStats play_game(
    const char* fen,
    int depth,
    bool verbose,
    int game_number)
{
    GameStats result;

    Board board;
    board.set_fen(fen);

    Engine white_engine;
    Engine black_engine;

    white_engine.tt.clear();
    black_engine.tt.clear();

    auto game_start = chrono::steady_clock::now();

    for (int ply = 0; ply < MAX_HALF_MOVES; ++ply) {

        result.half_moves = ply;

        bool white_to_move =
            (board.side_to_move == WHITE);

        // ----------------------------------------------------
        // Generate legal moves first.
        // This also tells us whether this is mate/stalemate.
        // ----------------------------------------------------

        MoveList legal_moves;
        MoveGenerator generator;

        generator.generate_moves(board, legal_moves);

        if (legal_moves.size() == 0) {

            if (is_in_check(board)) {

                if (board.side_to_move == WHITE)
                    result.result = Result::BLACK_WIN;
                else
                    result.result = Result::WHITE_WIN;

                result.reason = "checkmate";

            } else {

                result.result = Result::DRAW;
                result.reason = "stalemate";
            }

            break;
        }


        // ----------------------------------------------------
        // Search
        // ----------------------------------------------------

        auto move_start = chrono::steady_clock::now();

        uint32_t best_move = 0;

        if (white_to_move) {

            white_engine.tt.total_hits = 0;
            white_engine.tt.total_misses = 0;

            best_move =
                white_engine.best_move(board, depth);

        } else {

            black_engine.tt.total_hits = 0;
            black_engine.tt.total_misses = 0;

            best_move =
                black_engine.best_move(board, depth);
        }

        auto move_end = chrono::steady_clock::now();

        long long move_ms =
            chrono::duration_cast<chrono::milliseconds>(
                move_end - move_start
            ).count();


        // ----------------------------------------------------
        // Collect stats
        // ----------------------------------------------------

        if (white_to_move) {

            result.white.add_search(
                white_engine,
                move_ms
            );

        } else {

            result.black.add_search(
                black_engine,
                move_ms
            );
        }


        if (best_move == 0) {

            // Should not normally happen because legal moves
            // were already generated.
            result.result = Result::DRAW;
            result.reason = "no best move";
            break;
        }


        // ----------------------------------------------------
        // Optional move logging
        // ----------------------------------------------------

        if (verbose) {

            int current_depth =
                white_to_move
                    ? white_engine.completed_depth
                    : black_engine.completed_depth;

            uint64_t current_nodes =
                white_to_move
                    ? white_engine.nodes_searched
                    : black_engine.nodes_searched;

            cout
                << "Game " << game_number
                << " ply " << ply + 1
                << " "
                << (white_to_move ? "White" : "Black")
                << " "
                << move_to_uci(best_move)
                << " | depth=" << current_depth
                << " nodes=" << current_nodes
                << " time=" << move_ms << "ms"
                << '\n';
        }


        result.final_best_move = best_move;

        board.make_move(best_move);

        result.half_moves++;


        // ----------------------------------------------------
        // Draw rules
        // ----------------------------------------------------

        if (board.is_repetition(3)) {

            result.result = Result::DRAW;
            result.reason = "threefold repetition";
            break;
        }

        if (board.halfmove_clock >= 100) {

            result.result = Result::DRAW;
            result.reason = "50-move rule";
            break;
        }


        if (result.half_moves >= MAX_HALF_MOVES) {

            result.result = Result::DRAW;
            result.reason = "move limit";
            break;
        }
    }


    // --------------------------------------------------------
    // Final eval
    // --------------------------------------------------------

    result.final_eval = evaluate(board);

    auto game_end = chrono::steady_clock::now();

    result.game_time_ms =
        chrono::duration_cast<chrono::milliseconds>(
            game_end - game_start
        ).count();

    return result;
}


// ============================================================
// PRINT GAME RESULT
// ============================================================

void print_game_result(
    const GameStats& g,
    int depth,
    int game_number)
{
    string result;

    switch (g.result) {

        case Result::WHITE_WIN:
            result = "1-0";
            break;

        case Result::BLACK_WIN:
            result = "0-1";
            break;

        case Result::DRAW:
            result = "1/2-1/2";
            break;
    }

    uint64_t total_nodes =
        g.white.nodes + g.black.nodes;

    long long search_time =
        g.white.time_ms + g.black.time_ms;

    double nps = 0.0;

    if (search_time > 0) {
        nps =
            static_cast<double>(total_nodes) *
            1000.0 /
            static_cast<double>(search_time);
    }

    double avg_depth = 0.0;

    int searches =
        g.white.searches + g.black.searches;

    if (searches > 0) {

        avg_depth =
            static_cast<double>(
                g.white.depth_sum +
                g.black.depth_sum
            ) /
            searches;
    }

    uint64_t tt_hits =
        g.white.tt_hits +
        g.black.tt_hits;

    uint64_t tt_misses =
        g.white.tt_misses +
        g.black.tt_misses;

    double tt_hit_rate = 0.0;

    if (tt_hits + tt_misses > 0) {

        tt_hit_rate =
            100.0 *
            static_cast<double>(tt_hits) /
            static_cast<double>(tt_hits + tt_misses);
    }

    cout << fixed << setprecision(2);

    cout
        << "Game " << game_number
        << " | Depth " << depth
        << " | " << result
        << " | plies=" << g.half_moves
        << " | nodes=" << total_nodes
        << " | time=" << g.game_time_ms << "ms"
        << " | NPS=" << nps
        << " | avgDepth=" << avg_depth
        << " | TT hit=" << tt_hit_rate << "%"
        << " | reason=" << g.reason
        << '\n';
}


// ============================================================
// SELF-PLAY BENCHMARK
// ============================================================

struct DepthSummary {

    int depth = 0;

    int games = 0;

    int white_wins = 0;
    int black_wins = 0;
    int draws = 0;

    int total_plies = 0;

    uint64_t total_nodes = 0;
    uint64_t total_tt_hits = 0;
    uint64_t total_tt_misses = 0;

    long long total_time_ms = 0;

    long long max_game_time_ms = 0;
    uint64_t max_game_nodes = 0;

    int depth_sum = 0;
    int search_count = 0;
};


DepthSummary run_self_play_depth(int depth) {

    DepthSummary summary;
    summary.depth = depth;

    // Deterministic mapping of games to positions.
    for (int game = 0; game < GAMES_PER_DEPTH; ++game) {

        const char* fen =
            POSITIONS[game % NUM_POSITIONS];

        // Alternate colors naturally because the same starting
        // FEN is used and White is always the first side.
        //
        // To remove first-player bias, reverse the side-to-move
        // in your FEN set or add mirrored positions later.

        GameStats stats =
            play_game(
                fen,
                depth,
                false,
                game + 1
            );

        print_game_result(
            stats,
            depth,
            game + 1
        );

        summary.games++;

        if (stats.result == Result::WHITE_WIN)
            summary.white_wins++;
        else if (stats.result == Result::BLACK_WIN)
            summary.black_wins++;
        else
            summary.draws++;

        summary.total_plies += stats.half_moves;

        uint64_t game_nodes =
            stats.white.nodes +
            stats.black.nodes;

        long long game_time =
            stats.game_time_ms;

        summary.total_nodes += game_nodes;
        summary.total_time_ms += game_time;

        summary.total_tt_hits +=
            stats.white.tt_hits +
            stats.black.tt_hits;

        summary.total_tt_misses +=
            stats.white.tt_misses +
            stats.black.tt_misses;

        summary.max_game_nodes =
            max(summary.max_game_nodes, game_nodes);

        summary.max_game_time_ms =
            max(summary.max_game_time_ms, game_time);

        summary.depth_sum +=
            stats.white.depth_sum +
            stats.black.depth_sum;

        summary.search_count +=
            stats.white.searches +
            stats.black.searches;
    }

    return summary;
}


// ============================================================
// PRINT DEPTH SUMMARY
// ============================================================

void print_depth_summary(
    const DepthSummary& s)
{
    double score_pct = 0.0;

    // In self-play there is no "stronger side".
    // We report White score separately.
    //
    // White: 1 point win, 0.5 draw.
    double white_score =
        s.white_wins +
        0.5 * s.draws;

    if (s.games > 0) {

        white_score /=
            static_cast<double>(s.games * 1.0);

        score_pct =
            white_score * 100.0;
    }

    double nps = 0.0;

    if (s.total_time_ms > 0) {

        nps =
            static_cast<double>(s.total_nodes) *
            1000.0 /
            static_cast<double>(s.total_time_ms);
    }

    double avg_plies = 0.0;

    if (s.games > 0) {

        avg_plies =
            static_cast<double>(
                s.total_plies
            ) /
            s.games;
    }

    double avg_depth = 0.0;

    if (s.search_count > 0) {

        avg_depth =
            static_cast<double>(
                s.depth_sum
            ) /
            s.search_count;
    }

    double tt_hit_rate = 0.0;

    if (s.total_tt_hits + s.total_tt_misses > 0) {

        tt_hit_rate =
            100.0 *
            static_cast<double>(s.total_tt_hits) /
            static_cast<double>(
                s.total_tt_hits +
                s.total_tt_misses
            );
    }

    double avg_game_time = 0.0;

    if (s.games > 0) {

        avg_game_time =
            static_cast<double>(
                s.total_time_ms
            ) /
            s.games;
    }

    double avg_game_nodes = 0.0;

    if (s.games > 0) {

        avg_game_nodes =
            static_cast<double>(
                s.total_nodes
            ) /
            s.games;
    }

    cout << "\n";
    cout << "====================================================\n";
    cout << "NEW ENGINE SELF-PLAY - DEPTH "
         << s.depth << "\n";
    cout << "====================================================\n";

    cout << fixed << setprecision(2);

    cout << "Games              : " << s.games << '\n';
    cout << "White wins         : " << s.white_wins << '\n';
    cout << "Black wins         : " << s.black_wins << '\n';
    cout << "Draws              : " << s.draws << '\n';

    cout << "White score        : "
         << score_pct << "%\n";

    cout << "Average plies      : "
         << avg_plies << '\n';

    cout << "Total nodes        : "
         << s.total_nodes << '\n';

    cout << "Average nodes/game : "
         << avg_game_nodes << '\n';

    cout << "Total time         : "
         << s.total_time_ms << " ms\n";

    cout << "Average time/game  : "
         << avg_game_time << " ms\n";

    cout << "Average NPS        : "
         << nps << '\n';

    cout << "Average search depth:\n"
         << avg_depth << '\n';

    cout << "TT hits            : "
         << s.total_tt_hits << '\n';

    cout << "TT misses          : "
         << s.total_tt_misses << '\n';

    cout << "TT hit rate        : "
         << tt_hit_rate << "%\n";

    cout << "Max game nodes     : "
         << s.max_game_nodes << '\n';

    cout << "Max game time      : "
         << s.max_game_time_ms << " ms\n";

    cout << "====================================================\n";
}


// ============================================================
// PERFT BENCHMARK
// ============================================================

void run_perft() {

    const char* start_fen =
        "rnbqkbnr/pppppppp/8/8/8/8/"
        "PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    cout << "\n";
    cout << "====================================================\n";
    cout << "PERFT - NEW MOVE GENERATOR\n";
    cout << "====================================================\n";

    // Initialize required tables.
    init_rays();
    init_magics();

    for (int depth = FIRST_DEPTH;
         depth <= LAST_DEPTH;
         ++depth)
    {
        Board board;
        board.set_fen(start_fen);

        auto start =
            chrono::steady_clock::now();

        uint64_t nodes =
            perft(board, depth);

        auto end =
            chrono::steady_clock::now();

        long long ms =
            chrono::duration_cast<
                chrono::milliseconds>(
                    end - start
                ).count();

        double nps = 0.0;

        if (ms > 0) {

            nps =
                static_cast<double>(nodes) *
                1000.0 /
                static_cast<double>(ms);
        }

        cout << fixed << setprecision(2);

        cout
            << "Perft depth " << depth
            << " | nodes=" << nodes
            << " | time=" << ms << "ms"
            << " | NPS=" << nps
            << '\n';
    }

    cout << "====================================================\n";
}


// ============================================================
// MAIN
// ============================================================

int main(int argc, char** argv) {

    init_rays();
    init_magics();

    cout << "============================================\n";
    cout << "      NEW ENGINE BENCHMARK\n";
    cout << "============================================\n";

    cout << "Positions       : "
         << NUM_POSITIONS << '\n';

    cout << "Games / depth   : "
         << GAMES_PER_DEPTH << '\n';

    cout << "Depth range     : "
         << FIRST_DEPTH << "-"
         << LAST_DEPTH << '\n';

    cout << "Max game plies  : "
         << MAX_HALF_MOVES << '\n';

    // --------------------------------------------------------
    // Perft first
    // --------------------------------------------------------

    run_perft();

    // --------------------------------------------------------
    // Self play
    // --------------------------------------------------------

    cout << "\n\n";
    cout << "============================================\n";
    cout << "       NEW vs NEW SELF-PLAY\n";
    cout << "============================================\n";

    for (int depth = FIRST_DEPTH;
         depth <= LAST_DEPTH;
         ++depth)
    {
        DepthSummary summary =
            run_self_play_depth(depth);

        print_depth_summary(summary);
    }

    return 0;
}