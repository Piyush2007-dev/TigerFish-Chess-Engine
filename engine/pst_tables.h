#pragma once
#include <cstdint>

// ═══════════════════════════════════════════════════════════════════
// PIECE-SQUARE TABLES & FLATTENED ACCUMULATOR LOOKUPS
// Shared between board.cpp (incremental updates) and eval.cpp
// ═══════════════════════════════════════════════════════════════════

static inline constexpr int mirror_square(int sq) {
    return (7 - sq / 8) * 8 + sq % 8;
}

// ── 1. Middlegame PSTs ──────────────────────────────────────────

static inline constexpr int PAWN_PST_MG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0
};

static inline constexpr int KNIGHT_PST_MG[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

static inline constexpr int BISHOP_PST_MG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

static inline constexpr int ROOK_PST_MG[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

static inline constexpr int QUEEN_PST_MG[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

static inline constexpr int KING_PST_MG[64] = {
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30
};

// ── 1b. Endgame PSTs ────────────────────────────────────────────

static inline constexpr int PAWN_PST_EG[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10,-20,-20, 10, 10,  5,
      5, -5,-10,  0,  0,-10, -5,  5,
      0,  0,  0, 20, 20,  0,  0,  0,
      5,  5, 10, 25, 25, 10,  5,  5,
     10, 10, 20, 30, 30, 20, 10, 10,
     50, 50, 50, 50, 50, 50, 50, 50,
      0,  0,  0,  0,  0,  0,  0,  0
};

static inline constexpr int KNIGHT_PST_EG[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

static inline constexpr int BISHOP_PST_EG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

static inline constexpr int ROOK_PST_EG[64] = {
      0,  0,  0,  5,  5,  0,  0,  0,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      5, 10, 10, 10, 10, 10, 10,  5,
      0,  0,  0,  0,  0,  0,  0,  0
};

static inline constexpr int QUEEN_PST_EG[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

static inline constexpr int KING_PST_EG[64] = {
    -50,-30,-10, -5, -5,-10,-30,-50,
    -30,-10,  5, 10, 10,  5,-10,-30,
    -10,  5, 15, 20, 20, 15,  5,-10,
     -5, 10, 20, 30, 30, 20, 10, -5,
     -5, 10, 20, 30, 30, 20, 10, -5,
    -10,  5, 15, 20, 20, 15,  5,-10,
    -30,-10,  5, 10, 10,  5,-10,-30,
    -50,-30,-10, -5, -5,-10,-30,-50
};

// Pointer lookups (indexed by piece type 0-5 = P N B R Q K)
static inline const int (*MG_PST[6])[64] = {
    &PAWN_PST_MG, &KNIGHT_PST_MG, &BISHOP_PST_MG,
    &ROOK_PST_MG, &QUEEN_PST_MG, &KING_PST_MG
};
static inline const int (*EG_PST[6])[64] = {
    &PAWN_PST_EG, &KNIGHT_PST_EG, &BISHOP_PST_EG,
    &ROOK_PST_EG, &QUEEN_PST_EG, &KING_PST_EG
};

// ── 2. Flattened Signed Lookup Table for O(1) Incremental Updates ───
// Indexed by [piece 0..11][square 0..63]
// White pieces (0..5) are positive.
// Black pieces (6..11) are negative with mirror_square already applied!

struct FlatPstTable {
    int mg[12][64];
    int eg[12][64];

    constexpr FlatPstTable() : mg{}, eg{} {
        const int* mg_src[6] = { PAWN_PST_MG, KNIGHT_PST_MG, BISHOP_PST_MG, ROOK_PST_MG, QUEEN_PST_MG, KING_PST_MG };
        const int* eg_src[6] = { PAWN_PST_EG, KNIGHT_PST_EG, BISHOP_PST_EG, ROOK_PST_EG, QUEEN_PST_EG, KING_PST_EG };
        for (int p = 0; p < 12; ++p) {
            int type = p % 6;
            for (int sq = 0; sq < 64; ++sq) {
                int tsq = (p < 6) ? sq : mirror_square(sq);
                int mg_val = mg_src[type][tsq];
                int eg_val = eg_src[type][tsq];
                mg[p][sq] = (p < 6) ? mg_val : -mg_val;
                eg[p][sq] = (p < 6) ? eg_val : -eg_val;
            }
        }
    }
};

inline constexpr FlatPstTable FLAT_PST{};
#define PIECE_MG_PST FLAT_PST.mg
#define PIECE_EG_PST FLAT_PST.eg
