// match_runner.cpp — Parallel match: Enhanced vs Old engine
// 100 positions × 2 sides = 200 games, all 24 threads
// Engines handle legal moves via UCI. Game ends when engine returns 0000 or 200 moves.
// Compile: g++ -O3 -std=c++20 -Wl,--stack,67108864 -o match_runner.exe match_runner.cpp
// Run:     ./match_runner.exe <depth> <threads>
// Example: ./match_runner.exe 6 12

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>
#include <sstream>
#include <cstring>

using namespace std;

// ── Config ───────────────────────────────────────────────────────
static const int NUM_POSITIONS = 100;
static const int SEARCH_DEPTH  = 6;
static const int MOVETIME_MS   = 2000;
static const int MAX_MOVES     = 200;
static const int MAX_THREADS   = 24;

// ── Opening book (100 positions) ─────────────────────────────────
static const char* OPENINGS[][2] = {
    {"Starting Position",      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
    {"Sicilian",               "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
    {"French",                 "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
    {"Caro-Kann",              "rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
    {"King's Indian",          "rnbqkb1r/pppppp1p/5np1/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 3"},
    {"QGA",                    "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 2"},
    {"Italian",                "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 3"},
    {"Ruy Lopez",              "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 3"},
    {"English",                "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 1"},
    {"Scandinavian",           "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
    {"Pirc",                   "rnbqkb1r/ppp1pp1p/3p1np1/8/3PP3/2N2N2/PPP2PPP/R1BQKB1R w KQkq - 0 4"},
    {"Alekhine",               "rnbqkb1r/pppppp1p/5np1/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 3"},
    {"Nimzo-Indian",           "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4"},
    {"Grunfeld",               "rnbqkb1r/ppp1pp1p/3p1np1/8/2PP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 0 4"},
    {"Benoni",                 "rnbqkb1r/pp1p1ppp/4pn2/2p5/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4"},
    {"Dutch",                  "rnbqkbnr/ppppp1pp/8/5p2/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2"},
    {"Owen",                   "rnbqkbnr/pppp1ppp/8/4p3/1P6/P7/2PPPPPP/RNBQKBNR b KQkq - 0 2"},
    {"Center Game",            "rnbqkbnr/pppp1ppp/8/4p3/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Bishop Opening",         "rnbqkbnr/pppp1ppp/8/4p3/2B1P3/8/PPPP1PPP/RNBQK1NR b KQkq - 0 2"},
    {"Vienna",                 "rnbqkbnr/pppp1ppp/8/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 0 2"},
    {"King's Gambit",          "rnbqkbnr/pppp1ppp/8/4p3/4PP3/8/PPPP2PP/RNBQKBNR b KQkq - 0 2"},
    {"London",                 "rnbqkbnr/ppp1pppp/8/3p4/3P1B2/4P3/PPP2PPP/RN1QKBNR b KQkq - 0 2"},
    {"Torre",                  "rnbqkbnr/ppp1pppp/8/3p4/3P1B2/5N2/PPP1PPPP/RN1QKB1R b KQkq - 0 2"},
    {"Trompowsky",             "rnbqkbnr/pppppppp/8/8/3P1B2/8/PPP1PPPP/RN1QKBNR b KQkq - 0 2"},
    {"Reti",                   "rnbqkbnr/pppppppp/8/8/1P6/8/P1PPPPPP/RNBQKBNR b KQkq - 0 1"},
    {"Larsen",                 "rnbqkbnr/pppppppp/8/8/1P6/P7/2PPPPPP/RNBQKBNR b KQkq - 0 1"},
    {"Sokolsky",               "rnbqkbnr/pppppppp/8/8/P7/8/1PPPPPPP/RNBQKBNR b KQkq - 0 1"},
    {"Closed Sicilian",        "rnbqkbnr/pp1ppppp/8/2p5/4P3/3P4/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Four Knights",           "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 3"},
    {"Scotch",                 "r1bqkbnr/pppp1ppp/2n5/4p3/3PP3/5N2/PPP2PPP/R1BQKB1R b KQkq - 0 3"},
    {"Petrov",                 "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 2"},
    {"Philidor",               "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 3"},
    {"Two Knights",            "r1bqkbnr/pppp1ppp/2n5/4p3/3PP3/5N2/PPP2PPP/RNBQKB1R w KQkq - 0 3"},
    {"Modern",                 "rnbqkbnr/ppp1pp1p/3p4/1b2P3/8/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 3"},
    {"Old Indian",             "rnbqkb1r/pp1ppppp/5n2/2p5/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 3"},
    {"Catalan",                "rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4"},
    {"Queen's Indian",         "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3"},
    {"Bogo-Indian",            "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 0 3"},
    {"Benko Gambit",           "rnbqkb1r/pp1p1ppp/4pn2/2p5/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 0 4"},
    {"Semi-Slav",              "rnbqkb1r/pp1ppppp/2p2n2/8/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4"},
    {"Meran",                  "rnbqkb1r/pp1ppppp/2p2n2/8/2PP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 0 4"},
    {"Cambridge Springs",      "rnbqkb1r/pp1ppppp/5n2/2p5/2PP4/4P3/PP3PPP/RNBQKBNR w KQkq - 0 3"},
    {"Tarrasch",               "rnbqkbnr/pp1ppppp/8/2p5/3P4/4P3/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Chigorin",               "r1bqkbnr/pppppppp/2n5/3p4/3PP3/5N2/PPP2PPP/RNBQKB1R w KQkq - 0 3"},
    {"Albin",                  "rnbqkbnr/ppp1pppp/8/3p4/3P4/4P3/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Baltic",                 "rnbqkbnr/pppp1ppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 2"},
    {"Elephant Gambit",        "rnbqkbnr/pppp1ppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
    {"Englund",                "rnbqkbnr/pppppppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2"},
    {"Falkbeer",               "rnbqkbnr/pppp1ppp/8/4p3/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Latvian",                "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 2"},
    {"Sicilian Najdorf",       "rnbqkb1r/pp1ppppp/5n2/2p5/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 4"},
    {"Sicilian Dragon",        "rnbqkb1r/ppp1pp1p/3p1np1/8/3PP3/2N2N2/PPP2PPP/R1BQKB1R w KQkq - 0 4"},
    {"Sicilian Scheveningen",  "rnbqkb1r/pp1ppppp/5n2/2p5/3PP3/2N5/PPP2PPP/R1BQKBNR w KQkq - 0 4"},
    {"Sicilian Richter-Rauzer","rnbqkb1r/pp2pppp/3p1n2/2p5/3PP3/2N2N2/PPP2PPP/R1BQKB1R w KQkq - 0 5"},
    {"Sicilian Sveshnikov",    "r1bqkb1r/pp2pppp/2np1n2/2p5/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 5"},
    {"KID Classical",          "r1bqkb1r/pppppp1p/2n2np1/4P3/8/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 4"},
    {"KID Samisch",            "r1bqkb1r/pppppp1p/2n2np1/4P3/2PP4/2N5/PP3PPP/R1BQKBNR b KQkq - 0 4"},
    {"KID Four Pawns",         "r1bqkb1r/pppppp1p/2n2np1/4P3/2PPP3/2N5/PP3PPP/R1BQKBNR b KQkq - 0 4"},
    {"French Winawer",         "rnbqkbnr/ppp1pppp/8/3p4/3P1B2/4P3/PPP2PPP/RN1QKBNR b KQkq - 0 2"},
    {"French Tarrasch",        "rnbqkbnr/ppp1pppp/8/3p4/3P1B2/4P3/PPP2PPP/RN1QKBNR b KQkq - 0 2"},
    {"French Advance",         "rnbqkbnr/ppp1pppp/8/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"French Exchange",        "rnbqkbnr/ppp1pppp/8/3p4/3P4/4P3/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Caro Advance",           "rnbqkbnr/pp1ppppp/2p5/8/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Caro Exchange",          "rnbqkbnr/pp1ppppp/2p5/8/3P4/4P3/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Caro Panov",             "rnbqkbnr/pp1ppppp/2p5/8/2PP4/4P3/PP3PPP/RNBQKBNR w KQkq - 0 3"},
    {"QGD",                    "rnbqkbnr/ppp1pppp/8/3p4/2PP4/4P3/PP3PPP/RNBQKBNR w KQkq - 0 2"},
    {"QGA",                    "rnbqkbnr/ppp1pppp/8/8/2pP4/4P3/PP3PPP/RNBQKBNR w KQkq - 0 3"},
    {"Slav",                   "rnbqkbnr/pp1ppppp/2p5/8/2PP4/4P3/PP3PPP/RNBQKBNR w KQkq - 0 2"},
    {"Tarrasch QGD",           "rnbqkbnr/ppp1pppp/8/3p4/2PP4/4P3/PP3PPP/RNBQKBNR w KQkq - 0 2"},
    {"Vienna Gambit",          "rnbqkbnr/pppp1ppp/8/4p3/4PP3/2N5/PPPP2PP/R1BQKBNR b KQkq - 0 2"},
    {"Modern Benoni",          "rnbqkb1r/pp1p1ppp/4pn2/2p5/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4"},
    {"KIA",                    "rnbqkbnr/pppppppp/8/8/5N2/8/PPPPPPPP/RNBQKB1R b KQkq - 0 1"},
    {"Colle",                  "rnbqkbnr/pppppppp/8/8/3P4/4P3/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Stonewall",              "rnbqkbnr/pppppppp/8/8/3P4/4PP2/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Veresov",                "rnbqkbnr/pppppppp/8/8/3P1B2/5N2/PPP1PPPP/RN1QKB1R w KQkq - 0 2"},
    {"Blackmar-Diemer",        "rnbqkbnr/pppp1ppp/8/4p3/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Danish Gambit",          "rnbqkbnr/pppp1ppp/8/4p3/2PP3/8/PP2PPPP/RNBQKBNR w KQkq - 0 2"},
    {"Evans Gambit",           "r1bqkbnr/pppp1ppp/2n5/4p3/1BP1P3/5N2/PP1P1PPP/RNBQK2R b KQkq - 0 3"},
    {"Giuoco Piano",           "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 3"},
    {"Ponziani",               "rnbqkbnr/pppp1ppp/8/4p3/4P3/2P5/PP1P1PPP/RNBQKBNR w KQkq - 0 2"},
    {"Smith-Morra",            "rnbqkbnr/pp1ppppp/8/2p5/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Sicilian Kan",           "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 2"},
    {"Pirc Austrian",          "rnbqkb1r/ppp1pp1p/3p1np1/8/3PP3/2N1B3/PPP2PPP/R2QK1NR b KQkq - 0 4"},
    {"Benko Main",             "r1bqkb1r/pp2pppp/2np1n2/2p5/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 0 5"},
    {"English Syms",           "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 1"},
    {"Botvinnik",              "r1bqkbnr/pppppppp/2n5/8/2P5/4P3/PP1P1PPP/RNBQKBNR w KQkq - 0 2"},
    {"Nimzo-Larsen",           "rnbqkbnr/pppppppp/8/8/1P6/5N2/P1PPPPPP/RNBQKB1R b KQkq - 0 1"},
    {"King Fianchetto",        "rnbqkbnr/pppppppp/8/8/1P6/8/P1PPPPPP/RNBQKBNR b KQkq - 0 1"},
    {"Sicilian Taimanov",      "r1bqkbnr/pp1ppppp/2n5/2p5/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 4"},
    {"French Rubinstein",      "rnbqkbnr/ppp1pppp/8/3p4/3P4/4P3/PPP2PPP/RNBQKBNR w KQkq - 0 2"},
    {"Modern Defense",         "rnbqkbnr/ppp1pp1p/3p4/1b2P3/8/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 3"},
    {"Dutch Leningrad",        "rnbqkb1r/pppppp1p/5np1/4P3/3P1p2/5N2/PPP2PPP/RNBQKB1R w KQkq - 0 4"},
    {"Benko Accepted",         "r1bqkb1r/pp2pppp/2np1n2/p1p5/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 0 5"},
    {"Sicilian Accelerated Dragon","rnbqkb1r/pp1ppppp/5n2/2p5/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4"},
    {"French Advance Burn",    "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/4B3/PPP2PPP/RN1QKBNR w KQkq - 0 3"},
    {"King's Indian Averbakh","r1bq1rk1/pppnnppp/3p4/1b2P3/2PP4/2N2N2/PP3PPP/R1BQKB1R w KQ - 0 7"},
    {"Ruy Lopez Marshall",     "r1bq1rk1/ppp1ppbp/2np1np1/8/3PP3/1B2NN2/PPP2PPP/R1BQ1RK1 w - - 0 8"},
    {"Catalan Open",           "r1bqkb1r/pp1p1ppp/2n2n2/2pp4/2PP4/2N1PN2/PP3PPP/R1BQKB1R w KQkq - 0 5"},
    {"Sicilian Najdorf P.P.",  "r1bqkb1r/1p2pppp/p2p1n2/2p5/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 6"},
    {"Sicilian Mozzie",        "rnbqkb1r/pp1ppppp/5n2/2pP4/8/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 3"},
    {"French Burn",            "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/4B3/PPP2PPP/RN1QKBNR w KQkq - 0 3"},
    {"Queen's Gambit Declined Exchange","rnbqkbnr/ppp1pppp/8/3p4/2PP4/4P3/PP3PPP/RNBQKBNR w KQkq - 0 2"},
    {"King's Indian Petrosian","r1bq1rk1/1ppnppbp/p2p1np1/8/2PPP3/2N1BN2/PP2BPPP/R2Q1RK1 w - - 0 8"},
    {"Sicilian Taimanov Najdorf","r1bqkb1r/pp1ppppp/2n2n2/2p5/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 4"},
    {"Benoni Modern",          "rnbqkb1r/pp1p1ppp/4pn2/2p5/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4"},
    {"English Reversed Sicilian","rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 1"},
    {"French McCutcheon",      "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/5N2/PPP1BPPP/RNBQK2R w KQkq - 0 3"},
    {"Sicilian Kalashnikov",   "r1bqkbnr/pp1ppppp/2n5/2p5/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 4"},
    {"Nimzo-Indian Rubinstein","rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4"},
    {"QGD Tartakower",         "r1bq1rk1/pp1pbnpp/2p1pn2/8/2PP4/2N1PN2/PP2BPPP/R1BQ1RK1 w - - 0 8"},
    {"Grunfeld Exchange",      "r1bqkb1r/ppp1pp1p/2np1np1/8/2PP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 0 4"},
    {"Slav Meran",             "r1bqkb1r/pp2pppp/2np1n2/2p5/2PP4/2N1PN2/PP3PPP/R1BQKB1R w KQkq - 0 5"},
    {"Sicilian Sveshnikov Main","r1bqkb1r/pp2pppp/2np1n2/2p5/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 5"},
    {"King's Indian Bayonet",  "r1bq1rk1/pppnppbp/3p1np1/8/2PPPP2/2N2N2/PP4PP/R1BQKB1R w KQ - 0 7"},
    {"French Steinitz",        "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/3B4/PPP2PPP/RNBQK1NR w KQkq - 0 3"},
    {"Petrov Classical",       "r1bqkb1r/pppp1ppp/2n2n2/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 3"},
};

static const int NUM_OPENINGS = sizeof(OPENINGS) / sizeof(OPENINGS[0]);

// ── Mutex for sequential output ──────────────────────────────────
mutex g_print;

// ── UCI Engine Process ───────────────────────────────────────────
struct UciEngine {
    HANDLE h_process = NULL;
    HANDLE h_in_write = NULL;
    HANDLE h_out_read = NULL;
    string name;

    bool start(const string& exe, const string& n) {
        name = n;
        SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
        HANDLE h_in_read, h_out_write;
        if (!CreatePipe(&h_in_read, &h_in_write, &sa, 0)) return false;
        if (!CreatePipe(&h_out_read, &h_out_write, &sa, 0)) {
            CloseHandle(h_in_read); CloseHandle(h_in_write); return false;
        }
        SetHandleInformation(h_in_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(h_out_read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFO si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        si.hStdInput = h_in_read;
        si.hStdOutput = h_out_write;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.dwFlags = STARTF_USESTDHANDLES;

        char cmd[MAX_PATH];
        strncpy(cmd, exe.c_str(), MAX_PATH - 1);
        cmd[MAX_PATH - 1] = 0;

        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(h_in_read); CloseHandle(h_in_write);
            CloseHandle(h_out_read); CloseHandle(h_out_write);
            return false;
        }
        h_process = pi.hProcess;
        CloseHandle(pi.hThread);
        CloseHandle(h_in_read);
        CloseHandle(h_out_write);
        return true;
    }

    void send(const string& s) {
        if (!h_in_write) return;
        string data = s + "\n";
        DWORD w;
        WriteFile(h_in_write, data.c_str(), (DWORD)data.size(), &w, NULL);
    }

    // Read one line with timeout. Returns empty string on timeout.
    string readline(int timeout_ms) {
        if (!h_out_read) return "";
        string buf;
        auto start = chrono::steady_clock::now();
        while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count() < timeout_ms) {
            DWORD avail = 0;
            if (!PeekNamedPipe(h_out_read, NULL, 0, NULL, &avail, NULL) || avail == 0) {
                Sleep(5);
                continue;
            }
            char tmp[1024];
            DWORD read = 0;
            if (ReadFile(h_out_read, tmp, min(avail, (DWORD)sizeof(tmp) - 1), &read, NULL) && read > 0) {
                tmp[read] = 0;
                buf += tmp;
                size_t nl;
                while ((nl = buf.find('\n')) != string::npos) {
                    string line = buf.substr(0, nl);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    buf.erase(0, nl + 1);
                    if (!line.empty()) return line;
                }
            }
        }
        return "";
    }

    // Send command, read until we see expected prefix
    string send_read(const string& cmd, const string& prefix, int timeout_ms = 10000) {
        send(cmd);
        auto start = chrono::steady_clock::now();
        while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count() < timeout_ms) {
            string line = readline(500);
            if (line.empty()) continue;
            if (line.compare(0, prefix.size(), prefix) == 0) return line;
        }
        return "";
    }

    // Initialize UCI
    void init_uci() {
        send("uci");
        send_read("uci", "uciok");
    }

    // Get best move for a position (fen + move list)
    string get_bestmove(const string& fen, const vector<string>& moves, int depth, int ms) {
        send("ucinewgame");
        this_thread::sleep_for(chrono::milliseconds(10));
        send("isready");
        send_read("isready", "readyok", 3000);

        // Build position string
        string pos = "position fen " + fen;
        if (!moves.empty()) {
            pos += " moves";
            for (auto& m : moves) pos += " " + m;
        }
        send(pos);
        send("go depth " + to_string(depth) + " movetime " + to_string(ms));

        // Wait for bestmove
        auto start = chrono::steady_clock::now();
        int timeout = ms + 10000;
        while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count() < timeout) {
            string line = readline(500);
            if (line.empty()) continue;
            if (line.compare(0, 9, "bestmove ") == 0) {
                size_t sp = line.find(' ', 9);
                return (sp != string::npos) ? line.substr(9, sp - 9) : line.substr(9);
            }
        }
        return "";
    }

    void stop_engine() {
        send("quit");
        Sleep(300);
        if (h_process) {
            TerminateProcess(h_process, 0);
            WaitForSingleObject(h_process, 1000);
            CloseHandle(h_process);
            h_process = NULL;
        }
        if (h_in_write) { CloseHandle(h_in_write); h_in_write = NULL; }
        if (h_out_read) { CloseHandle(h_out_read); h_out_read = NULL; }
    }
};

// ── Result tracking ──────────────────────────────────────────────
struct Result {
    int id;
    string opening;
    string white_name;
    string black_name;
    string result;      // "1-0", "0-1", "1/2-1/2"
    string termination;
    int moves;
    int time_ms;
};

atomic<int> g_games_played{0};
atomic<int> g_enhanced_wins{0};
atomic<int> g_old_wins{0};
atomic<int> g_draws{0};
vector<Result> g_results;
mutex g_results_mtx;

// ── Game worker ──────────────────────────────────────────────────
void play_game(int id, const string& fen, const string& opening,
               const string& exe_w, const string& name_w,
               const string& exe_b, const string& name_b)
{
    auto t0 = chrono::steady_clock::now();

    UciEngine white, black;
    if (!white.start(exe_w, name_w) || !black.start(exe_b, name_b)) {
        lock_guard<mutex> lk(g_print);
        cout << "  [GAME " << id << "] ENGINE START FAILED" << endl;
        if (white.h_process) white.stop_engine();
        if (black.h_process) black.stop_engine();
        return;
    }
    white.init_uci();
    black.init_uci();

    vector<string> all_moves;
    int move_num = 0;
    string term = "max_moves";
    string result_str = "1/2-1/2";

    for (int half = 0; half < MAX_MOVES; half++) {
        string mv;
        if (half % 2 == 0) {
            mv = white.get_bestmove(fen, all_moves, SEARCH_DEPTH, MOVETIME_MS);
        } else {
            mv = black.get_bestmove(fen, all_moves, SEARCH_DEPTH, MOVETIME_MS);
        }

        if (mv.empty() || mv == "0000" || mv == "(none)") {
            // No legal moves — checkmate or stalemate
            // Determine: if the side to move is in check, it's checkmate
            // We can't easily detect check here, but "0000" from engine = no moves
            // In UCI, engines return "(none)" for no legal moves
            if (half % 2 == 0) {
                result_str = "0-1";  // White has no moves → black wins (or stalemate)
                term = "white_no_moves";
            } else {
                result_str = "1-0";  // Black has no moves → white wins (or stalemate)
                term = "black_no_moves";
            }
            break;
        }

        all_moves.push_back(mv);
        move_num = (int)all_moves.size();
    }

    if (move_num >= MAX_MOVES) {
        term = "max_moves";
        result_str = "1/2-1/2";
    }

    int elapsed = (int)chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count();
    int games = ++g_games_played;

    // Record result
    Result r;
    r.id = id;
    r.opening = opening;
    r.white_name = name_w;
    r.black_name = name_b;
    r.result = result_str;
    r.termination = term;
    r.moves = move_num;
    r.time_ms = elapsed;

    {
        lock_guard<mutex> lk(g_results_mtx);
        g_results.push_back(r);
    }

    // Count wins
    if (result_str == "1-0") {
        if (name_w == "Enhanced") ++g_enhanced_wins; else ++g_old_wins;
    } else if (result_str == "0-1") {
        if (name_b == "Enhanced") ++g_enhanced_wins; else ++g_old_wins;
    } else {
        ++g_draws;
    }

    // Print result immediately
    {
        lock_guard<mutex> lk(g_print);
        cout << "  [" << games << "/200] " << opening
             << " | " << name_w << "(W) vs " << name_b << "(B)"
             << " → " << result_str
             << " (" << term << ", " << move_num << " half-moves"
             << ", " << elapsed << "ms)" << endl;
    }

    white.stop_engine();
    black.stop_engine();
}

// ── Main ─────────────────────────────────────────────────────────
int main() {
    ios::sync_with_stdio(false);

    string exe_enhanced = "engine_enhanced.exe";
    string exe_old = "engine_old.exe";

    // Check args
    for (int i = 1; i < __argc; i++) {
        string a = __argv[i];
        if (a == "--enhanced" && i+1 < __argc) exe_enhanced = __argv[++i];
        if (a == "--old" && i+1 < __argc) exe_old = __argv[++i];
    }

    cout << "========================================" << endl;
    cout << " TigerFish Match: Enhanced vs Old       " << endl;
    cout << " Positions: " << NUM_POSITIONS << " (200 games)       " << endl;
    cout << " Depth: " << SEARCH_DEPTH << "  Time: " << MOVETIME_MS << "ms       " << endl;
    cout << " Threads: " << MAX_THREADS << "                          " << endl;
    cout << "========================================" << endl;
    cout << endl;

    // Shuffle openings
    vector<int> indices(NUM_OPENINGS);
    for (int i = 0; i < NUM_OPENINGS; i++) indices[i] = i;
    random_device rd;
    mt19937 g(rd());
    shuffle(indices.begin(), indices.end(), g);

    int num_pos = min(NUM_POSITIONS, NUM_OPENINGS);

    // Launch all games (2 per position) using thread pool
    vector<thread> pool;

    for (int i = 0; i < num_pos; i++) {
        int idx = indices[i];
        string fen = OPENINGS[idx][1];
        string name = OPENINGS[idx][0];

        // Game 1: Enhanced as White
        // Game 2: Old as White
        for (int side = 0; side < 2; side++) {
            int gid = i * 2 + side + 1;
            string w_exe = (side == 0) ? exe_enhanced : exe_old;
            string w_name = (side == 0) ? "Enhanced" : "Old";
            string b_exe = (side == 0) ? exe_old : exe_enhanced;
            string b_name = (side == 0) ? "Old" : "Enhanced";

            // Wait for a free thread slot
            while ((int)pool.size() >= MAX_THREADS) {
                for (auto it = pool.begin(); it != pool.end();) {
                    if (it->joinable()) { it->join(); it = pool.erase(it); }
                    else ++it;
                }
                if ((int)pool.size() >= MAX_THREADS) Sleep(50);
            }

            pool.emplace_back(play_game, gid, fen, name, w_exe, w_name, b_exe, b_name);
        }

        // Periodic progress
        if ((i + 1) % 10 == 0) {
            lock_guard<mutex> lk(g_print);
            cout << "--- " << (i+1) << "/" << num_pos << " positions launched ---" << endl;
        }
    }

    // Join remaining
    for (auto& t : pool) { if (t.joinable()) t.join(); }

    // Final summary
    cout << endl;
    cout << "========================================" << endl;
    cout << " FINAL RESULTS                         " << endl;
    cout << "========================================" << endl;
    cout << " Enhanced wins: " << g_enhanced_wins << endl;
    cout << " Old wins:      " << g_old_wins << endl;
    cout << " Draws:         " << g_draws << endl;
    cout << " Total games:   " << g_games_played << endl;

    if (g_enhanced_wins + g_old_wins > 0) {
        double ratio = (double)g_enhanced_wins / (g_enhanced_wins + g_old_wins) * 100.0;
        cout << " Enhanced win%: " << ratio << "%" << endl;
        cout << " Elo diff est:  " << (int)(-400.0 * log10((100.0 / ratio) - 1.0)) << endl;
    }
    cout << "========================================" << endl;

    return 0;
}
