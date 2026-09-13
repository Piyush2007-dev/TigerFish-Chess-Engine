#pragma once
// ═══════════════════════════════════════════════════════════════════
// EVALUATION PIPELINE — engine/eval.cpp
// Replaces eval_lut.cpp. Single public function: int evaluate(Board&)
//
// Features (in evaluation order):
//   1. Tapered eval — MG/EG PSTs blended by game phase
//   2. Bishop pair bonus
//   3. Pawn structure — doubled, isolated, passed pawns
//   4. Mobility — count safe squares per piece type
//   5. King safety — pawn shield + attacker count (MG only)
// ═══════════════════════════════════════════════════════════════════
#include "rules.cpp"
// ── Constants ───────────────────────────────────────────────────
static const int MAX_PHASE = 24;

// Piece values (index: 0-5 white P N B R Q K, 6-11 black p n b r q k)
static inline constexpr int PIECE_VALUE[12] = {
    100, 320, 330, 500, 900, 20000,
    100, 320, 330, 500, 900, 20000
};

// Phase weights per piece type (pawn=0, knight=1, bishop=1, rook=2, queen=4)
static inline constexpr int PHASE_WEIGHT[12] = {
    0, 1, 1, 2, 4, 0,  // White P N B R Q K
    0, 1, 1, 2, 4, 0   // Black p n b r q k
};

// ── File Masks ──────────────────────────────────────────────────
static inline constexpr uint64_t FILE_A_BB = 0x0101010101010101ULL;
static inline constexpr uint64_t FILE_B_BB = 0x0202020202020202ULL;
static inline constexpr uint64_t FILE_G_BB = 0x4040404040404040ULL;
static inline constexpr uint64_t FILE_H_BB = 0x8080808080808080ULL;
static inline constexpr uint64_t ADJ_FILES[8] = {
    FILE_B_BB,                         // File A neighbors: B
    FILE_A_BB | FILE_B_BB,             // File B neighbors: A,C (but C isn't in mask, just A|B)
    FILE_A_BB | FILE_B_BB,             // File C neighbors: B,D
    FILE_B_BB,                         // File D neighbors: C,E
    FILE_G_BB,                         // File E neighbors: D,F
    FILE_G_BB | FILE_H_BB,             // File F neighbors: E,G
    FILE_G_BB | FILE_H_BB,             // File G neighbors: F,H
    FILE_G_BB                          // File H neighbors: G
};

// Simpler adjacent file mask: shift left/right
static inline uint64_t adjacent_files(uint64_t pawns) {
    return ((pawns & ~FILE_A_BB) >> 1) | ((pawns & ~FILE_H_BB) << 1);
}

// PST tables (PAWN_PST_MG, etc., and MG_PST/EG_PST) are defined in pst_tables.h


// ═══════════════════════════════════════════════════════════════════
// 2. BISHOP PAIR BONUS
// ═══════════════════════════════════════════════════════════════════

static inline constexpr int BISHOP_PAIR_MG = 40;
static inline constexpr int BISHOP_PAIR_EG = 50;

// ═══════════════════════════════════════════════════════════════════
// 3. PAWN STRUCTURE
// ═══════════════════════════════════════════════════════════════════

// Doubled pawn penalty (per doubled pawn beyond first)
static inline constexpr int DOUBLED_PAWN_PENALTY = -12;

// Isolated pawn penalty
static inline constexpr int ISOLATED_PAWN_PENALTY = -15;

// Passed pawn bonus by rank (index 0=r2 for white, 6=r7 for white)
// Scaled higher for endgame in the taper
static inline constexpr int PASSED_PAWN_MG[8] = { 0, 0, 0, 8, 15, 25, 40, 0 };
static inline constexpr int PASSED_PAWN_EG[8] = { 0, 0, 0, 15, 30, 55, 90, 0 };

// Connected passed pawn bonus
static inline constexpr int CONNECTED_PASSED_BONUS = 12;

// ═══════════════════════════════════════════════════════════════════
// 4. MOBILITY
// ═══════════════════════════════════════════════════════════════════

// Mobility bonuses per number of safe squares (indexed by count, clamped)
// MG values — mobility matters more in middlegame
static inline constexpr int MOBILITY_MG[9] = {
    -20, -8, 0, 4, 8, 12, 16, 20, 24
};

// EG values — slightly less important
static inline constexpr int MOBILITY_EG[9] = {
    -15, -5, 0, 3, 6, 9, 12, 15, 18
};

// ═══════════════════════════════════════════════════════════════════
// 5. KING SAFETY
// ═══════════════════════════════════════════════════════════════════

// Pawn shield: bonus for each friendly pawn in front of king
static inline constexpr int PAWN_SHIELD_BONUS = 8;

// Open file near king: penalty per open file adjacent to king
static inline constexpr int OPEN_FILE_NEAR_KING_PENALTY = -20;

// Attacker count penalty (indexed by count, quadratic scaling)
static inline constexpr int ATTACKER_PENALTY[9] = {
    0, 0, -10, -25, -50, -80, -120, -170, -230
};

// ═══════════════════════════════════════════════════════════════════
// HELPER: Mirror square for black pieces
// ═══════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════
// 1. TAPERED EVALUATION — Material + PST (O(1) Incremental)
// ═══════════════════════════════════════════════════════════════════

static inline void eval_tapered(const Board& board, int& mg, int& eg) {
    mg = board.material_score + board.mg_pst;
    eg = board.material_score + board.eg_pst;
}

// ═══════════════════════════════════════════════════════════════════
// 2. BISHOP PAIR
// ═══════════════════════════════════════════════════════════════════

static inline void eval_bishop_pair(const Board& board, int& mg, int& eg) {
    if (popcount64(board.bitboards[Piece::B]) >= 2) {
        mg += BISHOP_PAIR_MG;
        eg += BISHOP_PAIR_EG;
    }
    if (popcount64(board.bitboards[Piece::b]) >= 2) {
        mg -= BISHOP_PAIR_MG;
        eg -= BISHOP_PAIR_EG;
    }
}

// ═══════════════════════════════════════════════════════════════════
// 3. PAWN STRUCTURE
// ═══════════════════════════════════════════════════════════════════

static inline void eval_pawn_structure(const Board& board, int& mg, int& eg) {
    uint64_t white_pawns = board.bitboards[Piece::P];
    uint64_t black_pawns = board.bitboards[Piece::p];

    // ── Doubled pawns ──
    int w_file_cnt[8] = {};
    int b_file_cnt[8] = {};
    uint64_t wp_scan = white_pawns;
    uint64_t bp_scan = black_pawns;
    while (wp_scan) w_file_cnt[pop_lsb(wp_scan) & 7]++;
    while (bp_scan) b_file_cnt[pop_lsb(bp_scan) & 7]++;
    for (int file = 0; file < 8; file++) {
        if (w_file_cnt[file] > 1) {
            int extra = w_file_cnt[file] - 1;
            mg += DOUBLED_PAWN_PENALTY * extra;
            eg += DOUBLED_PAWN_PENALTY * extra;
        }
        if (b_file_cnt[file] > 1) {
            int extra = b_file_cnt[file] - 1;
            mg -= DOUBLED_PAWN_PENALTY * extra;
            eg -= DOUBLED_PAWN_PENALTY * extra;
        }
    }

    // ── Isolated pawns ──
    // Pawns with no friendly pawns on adjacent files
    uint64_t w_adj = adjacent_files(white_pawns);
    uint64_t b_adj = adjacent_files(black_pawns);
    int w_isolated = popcount64(white_pawns & ~w_adj);
    int b_isolated = popcount64(black_pawns & ~b_adj);
    mg += w_isolated * ISOLATED_PAWN_PENALTY;
    mg -= b_isolated * ISOLATED_PAWN_PENALTY;
    eg += w_isolated * ISOLATED_PAWN_PENALTY;
    eg -= b_isolated * ISOLATED_PAWN_PENALTY;

    // ── Passed pawns ──
    uint64_t w_passed = 0;
    uint64_t b_passed = 0;

    {
        uint64_t wp = white_pawns;
        while (wp) {
            int sq = pop_lsb(wp);
            int rank = sq / 8;
            int file = sq % 8;
            uint64_t file_mask = FILE_A_BB << file;
            uint64_t adj = adjacent_files(file_mask);
            uint64_t span = file_mask | adj;
            uint64_t ahead = rank >= 7 ? 0ULL : (~0ULL << ((rank + 1) * 8));

            if (!(black_pawns & span & ahead)) {
                w_passed |= (1ULL << sq);
                mg += PASSED_PAWN_MG[rank];
                eg += PASSED_PAWN_EG[rank];
            }
        }
    }
    {
        uint64_t bp = black_pawns;
        while (bp) {
            int sq = pop_lsb(bp);
            int rank = sq / 8;
            int file = sq % 8;
            uint64_t file_mask = FILE_A_BB << file;
            uint64_t adj = adjacent_files(file_mask);
            uint64_t span = file_mask | adj;
            uint64_t ahead = rank == 0 ? 0ULL : ((1ULL << (rank * 8)) - 1);

            if (!(white_pawns & span & ahead)) {
                b_passed |= (1ULL << sq);
                int r = 7 - rank;
                mg -= PASSED_PAWN_MG[r];
                eg -= PASSED_PAWN_EG[r];
            }
        }
    }

    // Connected passed pawns bonus
    uint64_t w_connected = (w_passed >> 1) & w_passed;  // adjacent passed pawns
    uint64_t b_connected = (b_passed >> 1) & b_passed;
    mg += popcount64(w_connected) * CONNECTED_PASSED_BONUS;
    mg -= popcount64(b_connected) * CONNECTED_PASSED_BONUS;
    eg += popcount64(w_connected) * CONNECTED_PASSED_BONUS * 2;
    eg -= popcount64(b_connected) * CONNECTED_PASSED_BONUS * 2;
}

// ═══════════════════════════════════════════════════════════════════
// 4. MOBILITY
// ═══════════════════════════════════════════════════════════════════

static inline void eval_mobility_and_king_safety(const Board& board, int& mg, int& eg) {
    uint64_t all_occ = board.occupancy[2];
    uint64_t white_occ = board.occupancy[WHITE];
    uint64_t black_occ = board.occupancy[BLACK];

    // Compute King zones
    int w_king_sq = lsb_index(board.bitboards[Piece::K]);
    uint64_t w_king_zone = 0;
    if (w_king_sq >= 0 && w_king_sq < 64) {
        uint64_t king_bb = 1ULL << w_king_sq;
        uint64_t king_east = (king_bb & ~FILE_H_BB) << 1;
        uint64_t king_west = (king_bb & ~FILE_A_BB) >> 1;
        uint64_t king_rank3 = king_bb | king_east | king_west;
        w_king_zone = (king_rank3 | (king_rank3 << 8) | (king_rank3 >> 8)) & ~king_bb;
    }

    int b_king_sq = lsb_index(board.bitboards[Piece::k]);
    uint64_t b_king_zone = 0;
    if (b_king_sq >= 0 && b_king_sq < 64) {
        uint64_t king_bb = 1ULL << b_king_sq;
        uint64_t king_east = (king_bb & ~FILE_H_BB) << 1;
        uint64_t king_west = (king_bb & ~FILE_A_BB) >> 1;
        uint64_t king_rank3 = king_bb | king_east | king_west;
        b_king_zone = (king_rank3 | (king_rank3 >> 8) | (king_rank3 << 8)) & ~king_bb;
    }

    int w_attackers = 0;
    int b_attackers = 0;

    // ── White pieces: mobility & attackers against Black king ──
    {
        // Knights
        uint64_t kn = board.bitboards[Piece::N];
        while (kn) {
            int sq = pop_lsb(kn);
            uint64_t attacks = KNIGHT_ATTACKS_TABLE[sq];
            if (attacks & b_king_zone) b_attackers++;
            int count = min(8, popcount64(attacks & ~white_occ));
            mg += MOBILITY_MG[count];
            eg += MOBILITY_EG[count];
        }

        // Bishops
        uint64_t bi = board.bitboards[Piece::B];
        while (bi) {
            int sq = pop_lsb(bi);
            uint64_t attacks = bishop_attacks(sq, all_occ);
            if (attacks & b_king_zone) b_attackers++;
            int count = min(8, popcount64(attacks & ~white_occ));
            mg += MOBILITY_MG[count];
            eg += MOBILITY_EG[count];
        }

        // Rooks
        uint64_t ro = board.bitboards[Piece::R];
        while (ro) {
            int sq = pop_lsb(ro);
            uint64_t attacks = rook_attacks(sq, all_occ);
            if (attacks & b_king_zone) b_attackers++;
            int count = min(8, popcount64(attacks & ~white_occ));
            mg += MOBILITY_MG[count];
            eg += MOBILITY_EG[count];
        }

        // Queen
        uint64_t qu = board.bitboards[Piece::Q];
        while (qu) {
            int sq = pop_lsb(qu);
            uint64_t attacks = queen_attacks(sq, all_occ);
            if (attacks & b_king_zone) b_attackers++;
            int count = min(8, popcount64(attacks & ~white_occ));
            mg += MOBILITY_MG[count];
            eg += MOBILITY_EG[count];
        }
    }

    // ── Black pieces: mobility & attackers against White king ──
    {
        // Knights
        uint64_t kn = board.bitboards[Piece::n];
        while (kn) {
            int sq = pop_lsb(kn);
            uint64_t attacks = KNIGHT_ATTACKS_TABLE[sq];
            if (attacks & w_king_zone) w_attackers++;
            int count = min(8, popcount64(attacks & ~black_occ));
            mg -= MOBILITY_MG[count];
            eg -= MOBILITY_EG[count];
        }

        // Bishops
        uint64_t bi = board.bitboards[Piece::b];
        while (bi) {
            int sq = pop_lsb(bi);
            uint64_t attacks = bishop_attacks(sq, all_occ);
            if (attacks & w_king_zone) w_attackers++;
            int count = min(8, popcount64(attacks & ~black_occ));
            mg -= MOBILITY_MG[count];
            eg -= MOBILITY_EG[count];
        }

        // Rooks
        uint64_t ro = board.bitboards[Piece::r];
        while (ro) {
            int sq = pop_lsb(ro);
            uint64_t attacks = rook_attacks(sq, all_occ);
            if (attacks & w_king_zone) w_attackers++;
            int count = min(8, popcount64(attacks & ~black_occ));
            mg -= MOBILITY_MG[count];
            eg -= MOBILITY_EG[count];
        }

        // Queen
        uint64_t qu = board.bitboards[Piece::q];
        while (qu) {
            int sq = pop_lsb(qu);
            uint64_t attacks = queen_attacks(sq, all_occ);
            if (attacks & w_king_zone) w_attackers++;
            int count = min(8, popcount64(attacks & ~black_occ));
            mg -= MOBILITY_MG[count];
            eg -= MOBILITY_EG[count];
        }
    }

    // ── White king safety (pawn shield + open files + enemy attackers) ──
    if (w_king_sq >= 0 && w_king_sq < 64) {
        int king_rank = w_king_sq / 8;
        int king_file = w_king_sq % 8;

        int shield_count = 0;
        for (int r = king_rank + 1; r <= min(7, king_rank + 2); r++) {
            for (int f = max(0, king_file - 1); f <= min(7, king_file + 1); f++) {
                if (board.piece_on[r * 8 + f] == Piece::P) shield_count++;
            }
        }
        mg += shield_count * PAWN_SHIELD_BONUS;

        for (int f = max(0, king_file - 1); f <= min(7, king_file + 1); f++) {
            uint64_t file_mask = FILE_A_BB << f;
            if (!(board.bitboards[Piece::P] & file_mask)) {
                mg += OPEN_FILE_NEAR_KING_PENALTY;
            }
        }

        mg += ATTACKER_PENALTY[min(8, w_attackers)];
    }

    // ── Black king safety (pawn shield + open files + friendly attackers) ──
    if (b_king_sq >= 0 && b_king_sq < 64) {
        int king_rank = b_king_sq / 8;
        int king_file = b_king_sq % 8;

        int shield_count = 0;
        for (int r = max(0, king_rank - 2); r <= king_rank - 1; r++) {
            for (int f = max(0, king_file - 1); f <= min(7, king_file + 1); f++) {
                if (board.piece_on[r * 8 + f] == Piece::p) shield_count++;
            }
        }
        mg -= shield_count * PAWN_SHIELD_BONUS;

        for (int f = max(0, king_file - 1); f <= min(7, king_file + 1); f++) {
            uint64_t file_mask = FILE_A_BB << f;
            if (!(board.bitboards[Piece::p] & file_mask)) {
                mg -= OPEN_FILE_NEAR_KING_PENALTY;
            }
        }

        mg -= ATTACKER_PENALTY[min(8, b_attackers)];
    }
}

// ═══════════════════════════════════════════════════════════════════
// SINGLE PUBLIC FUNCTION — called by search
// ═══════════════════════════════════════════════════════════════════

inline int evaluate(Board& board) {
    init_rules_luts();
    // 1. Compute game phase (0 = endgame, MAX_PHASE = middlegame)
    int phase = min(board.phase_score, MAX_PHASE);

    // 2. Accumulate scores in MG and EG
    int mg = 0, eg = 0;

    eval_tapered(board, mg, eg);           // Material + PST
    eval_bishop_pair(board, mg, eg);       // Bishop pair
    eval_pawn_structure(board, mg, eg);    // Doubled, isolated, passed pawns
    eval_mobility_and_king_safety(board, mg, eg); // Unified mobility and king safety

    // 3. Tapered blend
    int score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    // 4. Return from White's perspective (positive = White advantage)
    // MINIMAX expects fixed perspective: White maximizes, Black minimizes
    return score;
}

// ═════════════════════════════════════════════════════════════════════
// OLD PIECE-SQUARE TABLES (non-tapered, used by old_evaluate only)
// ═════════════════════════════════════════════════════════════════════

inline constexpr int PAWN_PST[64] = {
    0,0,0,0,0,0,0,0,
    5,10,10,-20,-20,10,10,5,
    5,-5,-10,0,0,-10,-5,5,
    0,0,0,20,20,0,0,0,
    5,5,10,25,25,10,5,5,
    10,10,20,30,30,20,10,10,
    50,50,50,50,50,50,50,50,
    0,0,0,0,0,0,0,0
};

inline constexpr int KNIGHT_PST[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,0,5,5,0,-20,-40,
    -30,5,10,15,15,10,5,-30,
    -30,0,15,20,20,15,0,-30,
    -30,5,15,20,20,15,5,-30,
    -30,0,10,15,15,10,0,-30,
    -40,-20,0,0,0,0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

inline constexpr int BISHOP_PST[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,5,0,0,0,0,5,-10,
    -10,10,10,10,10,10,10,-10,
    -10,0,10,10,10,10,0,-10,
    -10,5,5,10,10,5,5,-10,
    -10,0,5,10,10,5,0,-10,
    -10,0,0,0,0,0,0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

inline constexpr int ROOK_PST[64] = {
    0,0,0,5,5,0,0,0,
    -5,0,0,0,0,0,0,-5,
    -5,0,0,0,0,0,0,-5,
    -5,0,0,0,0,0,0,-5,
    -5,0,0,0,0,0,0,-5,
    -5,0,0,0,0,0,0,-5,
    5,10,10,10,10,10,10,5,
    0,0,0,0,0,0,0,0
};

inline constexpr int QUEEN_PST[64] = {
    -20,-10,-10,-5,-5,-10,-10,-20,
    -10,0,5,0,0,0,0,-10,
    -10,5,5,5,5,5,0,-10,
    0,0,5,5,5,5,0,-5,
    -5,0,5,5,5,5,0,-5,
    -10,0,5,5,5,5,0,-10,
    -10,0,0,0,0,0,0,-10,
    -20,-10,-10,-5,-5,-10,-10,-20
};

inline constexpr int KING_PST[64] = {
    20,30,10,0,0,10,30,20,
    20,20,0,0,0,0,20,20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30
};

// ═════════════════════════════════════════════════════════════════════
// OLD EVALUATOR - simple PST-only, no tapering (used by OldEngine)
// Uses the original non-tapered tables above, NOT the MG/EG tables.
// ═════════════════════════════════════════════════════════════════════

inline int old_evaluate(Board& board) {
    int total_eval = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece piece = board.piece_on[sq];
        if (piece == (Piece)0xF) continue;

        int piece_val = PIECE_VALUE[piece];
        int table_sq  = piece < 6 ? sq : mirror_square(sq);

        switch (piece) {
            case Piece::P: case Piece::p: piece_val += PAWN_PST[table_sq];   break;
            case Piece::N: case Piece::n: piece_val += KNIGHT_PST[table_sq]; break;
            case Piece::B: case Piece::b: piece_val += BISHOP_PST[table_sq]; break;
            case Piece::R: case Piece::r: piece_val += ROOK_PST[table_sq];   break;
            case Piece::Q: case Piece::q: piece_val += QUEEN_PST[table_sq];  break;
            case Piece::K: case Piece::k: piece_val += KING_PST[table_sq];   break;
            default: break;
        }
        total_eval += piece < 6 ? piece_val : -piece_val;
    }
    return total_eval;
}
