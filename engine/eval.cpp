#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — EVALUATION (newengine/eval.cpp)
// Tapered Handcrafted Evaluation with O(1) Incremental Base & Pluggable RL Hook.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "bitboards.cpp"
#include "position.cpp"

namespace tigerfish {

// ── Evaluation Bonus & Penalty Constants ─────────────────────────────────────
constexpr int BISHOP_PAIR_BONUS_MG = 35;
constexpr int BISHOP_PAIR_BONUS_EG = 50;

constexpr int DOUBLED_PAWN_PENALTY_MG = 15;
constexpr int DOUBLED_PAWN_PENALTY_EG = 25;

constexpr int ISOLATED_PAWN_PENALTY_MG = 15;
constexpr int ISOLATED_PAWN_PENALTY_EG = 20;

constexpr int PASSED_PAWN_BONUS_MG[8] = { 0, 5, 10, 20, 35, 60, 100, 0 };
constexpr int PASSED_PAWN_BONUS_EG[8] = { 0, 10, 20, 35, 60, 100, 160, 0 };

constexpr int ROOK_OPEN_FILE_MG = 25;
constexpr int ROOK_OPEN_FILE_EG = 15;
constexpr int ROOK_SEMI_OPEN_FILE_MG = 12;
constexpr int ROOK_SEMI_OPEN_FILE_EG = 8;

// ── Pluggable Model Hook (for Reinforcement Learning / NNUE) ─────────────────
using EvalHookFunc = int (*)(const Position&);
inline EvalHookFunc ACTIVE_EVAL_HOOK = nullptr;

static Bitboard WHITE_PASSED_SPAN[64]{};
static Bitboard BLACK_PASSED_SPAN[64]{};
static Bitboard ADJACENT_FILES[8]{};

struct alignas(16) PawnEntry {
    uint64_t key{0};
    int16_t mg{0};
    int16_t eg{0};
};
static PawnEntry PAWN_TABLE[65536]{};

// ── Pawn Structure Evaluation ────────────────────────────────────────────────
inline void eval_pawns(const Position& pos, int& mg, int& eg) noexcept {
    uint64_t key = pos.state->pawn_zobrist_key;
    uint32_t idx = static_cast<uint32_t>(key & 65535);
    if (PAWN_TABLE[idx].key == key) {
        std::atomic_thread_fence(std::memory_order_acquire);
        mg += PAWN_TABLE[idx].mg;
        eg += PAWN_TABLE[idx].eg;
        return;
    }

    int p_mg = 0;
    int p_eg = 0;
    Bitboard w_pawns = pos.pieces[W_PAWN];
    Bitboard b_pawns = pos.pieces[B_PAWN];

    // File-by-file analysis
    for (int f = 0; f < 8; ++f) {
        Bitboard file_mask = FILE_MASKS[f];
        Bitboard adj_mask = ADJACENT_FILES[f];

        // White pawns on file
        Bitboard wp_file = w_pawns & file_mask;
        int wp_count = popcount(wp_file);
        if (wp_count > 1) {
            p_mg -= (wp_count - 1) * DOUBLED_PAWN_PENALTY_MG;
            p_eg -= (wp_count - 1) * DOUBLED_PAWN_PENALTY_EG;
        }
        if (wp_count > 0 && !(w_pawns & adj_mask)) {
            p_mg -= ISOLATED_PAWN_PENALTY_MG;
            p_eg -= ISOLATED_PAWN_PENALTY_EG;
        }

        // Black pawns on file
        Bitboard bp_file = b_pawns & file_mask;
        int bp_count = popcount(bp_file);
        if (bp_count > 1) {
            p_mg += (bp_count - 1) * DOUBLED_PAWN_PENALTY_MG;
            p_eg += (bp_count - 1) * DOUBLED_PAWN_PENALTY_EG;
        }
        if (bp_count > 0 && !(b_pawns & adj_mask)) {
            p_mg += ISOLATED_PAWN_PENALTY_MG;
            p_eg -= ISOLATED_PAWN_PENALTY_EG;
        }
    }

    // Passed pawns
    Bitboard wp_copy = w_pawns;
    while (wp_copy) {
        Square sq = pop_lsb(wp_copy);
        if (!(b_pawns & WHITE_PASSED_SPAN[sq])) {
            int r = square_rank(sq);
            p_mg += PASSED_PAWN_BONUS_MG[r];
            p_eg += PASSED_PAWN_BONUS_EG[r];
        }
    }

    Bitboard bp_copy = b_pawns;
    while (bp_copy) {
        Square sq = pop_lsb(bp_copy);
        if (!(w_pawns & BLACK_PASSED_SPAN[sq])) {
            int rank_from_black = 7 - square_rank(sq);
            p_mg -= PASSED_PAWN_BONUS_MG[rank_from_black];
            p_eg -= PASSED_PAWN_BONUS_EG[rank_from_black];
        }
    }

    PAWN_TABLE[idx].mg = static_cast<int16_t>(p_mg);
    PAWN_TABLE[idx].eg = static_cast<int16_t>(p_eg);
    std::atomic_thread_fence(std::memory_order_release);
    PAWN_TABLE[idx].key = key;
    mg += p_mg;
    eg += p_eg;
}


// ── Mobility & Pieces Evaluation ─────────────────────────────────────────────
inline void eval_pieces(const Position& pos, int& mg, int& eg) noexcept {
    Bitboard w_pawns = pos.pieces[W_PAWN];
    Bitboard b_pawns = pos.pieces[B_PAWN];

    // Bishop pair
    if (popcount(pos.pieces[W_BISHOP]) >= 2) {
        mg += BISHOP_PAIR_BONUS_MG;
        eg += BISHOP_PAIR_BONUS_EG;
    }
    if (popcount(pos.pieces[B_BISHOP]) >= 2) {
        mg -= BISHOP_PAIR_BONUS_MG;
        eg -= BISHOP_PAIR_BONUS_EG;
    }

    // Rook on open and semi-open files
    Bitboard w_rooks = pos.pieces[W_ROOK];
    while (w_rooks) {
        Square sq = pop_lsb(w_rooks);
        File f = square_file(sq);
        if (!(w_pawns & FILE_MASKS[f])) {
            if (!(b_pawns & FILE_MASKS[f])) {
                mg += ROOK_OPEN_FILE_MG;
                eg += ROOK_OPEN_FILE_EG;
            } else {
                mg += ROOK_SEMI_OPEN_FILE_MG;
                eg += ROOK_SEMI_OPEN_FILE_EG;
            }
        }
    }

    Bitboard b_rooks = pos.pieces[B_ROOK];
    while (b_rooks) {
        Square sq = pop_lsb(b_rooks);
        File f = square_file(sq);
        if (!(b_pawns & FILE_MASKS[f])) {
            if (!(w_pawns & FILE_MASKS[f])) {
                mg -= ROOK_OPEN_FILE_MG;
                eg -= ROOK_OPEN_FILE_EG;
            } else {
                mg -= ROOK_SEMI_OPEN_FILE_MG;
                eg -= ROOK_SEMI_OPEN_FILE_EG;
            }
        }
    }
}

// ── King Safety Evaluation (Middlegame Only) ─────────────────────────────────
static Bitboard KING_SHIELD_1[2][64]{};
static Bitboard KING_SHIELD_2[2][64]{};

inline void init_king_shields() noexcept {
    static bool init = false;
    if (init) return;
    init = true;

    for (int f = 0; f < 8; ++f) {
        ADJACENT_FILES[f] = (f > 0 ? FILE_MASKS[f - 1] : 0) | (f < 7 ? FILE_MASKS[f + 1] : 0);
    }

    for (int sq = 0; sq < 64; ++sq) {
        int r = square_rank(static_cast<Square>(sq));
        int f = square_file(static_cast<Square>(sq));

        Bitboard front_w = (r < 7) ? ~((1ULL << ((r + 1) * 8)) - 1) : 0ULL;
        WHITE_PASSED_SPAN[sq] = front_w & (FILE_MASKS[f] | ADJACENT_FILES[f]);

        Bitboard front_b = (r > 0) ? ((1ULL << (r * 8)) - 1) : 0ULL;
        BLACK_PASSED_SPAN[sq] = front_b & (FILE_MASKS[f] | ADJACENT_FILES[f]);
    }

    for (int sq = 0; sq < 64; ++sq) {
        int r = square_rank(static_cast<Square>(sq));
        int f = square_file(static_cast<Square>(sq));

        // White
        if (r + 1 < 8) {
            for (int df = -1; df <= 1; ++df) {
                int nf = f + df;
                if (nf >= 0 && nf < 8) KING_SHIELD_1[WHITE][sq] |= (1ULL << make_square(static_cast<File>(nf), static_cast<Rank>(r + 1)));
            }
        }
        if (r + 2 < 8) {
            for (int df = -1; df <= 1; ++df) {
                int nf = f + df;
                if (nf >= 0 && nf < 8) KING_SHIELD_2[WHITE][sq] |= (1ULL << make_square(static_cast<File>(nf), static_cast<Rank>(r + 2)));
            }
        }

        // Black
        if (r - 1 >= 0) {
            for (int df = -1; df <= 1; ++df) {
                int nf = f + df;
                if (nf >= 0 && nf < 8) KING_SHIELD_1[BLACK][sq] |= (1ULL << make_square(static_cast<File>(nf), static_cast<Rank>(r - 1)));
            }
        }
        if (r - 2 >= 0) {
            for (int df = -1; df <= 1; ++df) {
                int nf = f + df;
                if (nf >= 0 && nf < 8) KING_SHIELD_2[BLACK][sq] |= (1ULL << make_square(static_cast<File>(nf), static_cast<Rank>(r - 2)));
            }
        }
    }
}

constexpr int ATTACKER_PENALTY[9] = {
    0, 0, -10, -25, -50, -80, -120, -170, -230
};
constexpr int OPEN_FILE_NEAR_KING_PENALTY = -20;
constexpr int SEMI_OPEN_FILE_NEAR_KING_PENALTY = -10;

inline int eval_king_safety(const Position& pos, Color c) noexcept {
    Square ksq = pos.king_square(c);
    if (ksq >= 64) return 0;
    Bitboard friendly_pawns = pos.pieces[make_piece(c, PT_PAWN)];
    Bitboard enemy_pawns = pos.pieces[make_piece(opponent_color(c), PT_PAWN)];

    int score = 0;

    // Pawn shield bonus
    Bitboard s1 = friendly_pawns & KING_SHIELD_1[c][ksq];
    Bitboard s2 = friendly_pawns & KING_SHIELD_2[c][ksq];
    score += popcount(s1) * 15 + popcount(s2) * 8;

    // Open and semi-open files penalty near king
    int king_file = square_file(ksq);
    for (int f = std::max(0, king_file - 1); f <= std::min(7, king_file + 1); ++f) {
        if (!(friendly_pawns & FILE_MASKS[f])) {
            if (!(enemy_pawns & FILE_MASKS[f])) {
                score += OPEN_FILE_NEAR_KING_PENALTY;
            } else {
                score += SEMI_OPEN_FILE_NEAR_KING_PENALTY;
            }
        }
    }

    // Enemy piece attackers into king zone (king square + 8 surrounding squares)
    Color enemy = opponent_color(c);
    Bitboard king_zone = king_attacks(ksq) | (1ULL << ksq);
    Bitboard occ = pos.occupancy();
    int attackers = 0;

    Bitboard kn = pos.pieces[make_piece(enemy, PT_KNIGHT)];
    while (kn) {
        Square s = pop_lsb(kn);
        if (knight_attacks(s) & king_zone) attackers++;
    }

    Bitboard bi = pos.pieces[make_piece(enemy, PT_BISHOP)];
    while (bi) {
        Square s = pop_lsb(bi);
        if (bishop_attacks(s, occ) & king_zone) attackers++;
    }

    Bitboard ro = pos.pieces[make_piece(enemy, PT_ROOK)];
    while (ro) {
        Square s = pop_lsb(ro);
        if (rook_attacks(s, occ) & king_zone) attackers++;
    }

    Bitboard qu = pos.pieces[make_piece(enemy, PT_QUEEN)];
    while (qu) {
        Square s = pop_lsb(qu);
        if (queen_attacks(s, occ) & king_zone) attackers += 2;
    }

    score += ATTACKER_PENALTY[std::min(8, attackers)];
    return score;
}

inline void init_eval() noexcept {
    init_king_shields();
}

// ── Master Tapered Evaluation Function ───────────────────────────────────────
inline int evaluate(const Position& pos) noexcept {
    init_king_shields();

    // If an external Reinforcement Learning / neural hook is registered, use it
    if (ACTIVE_EVAL_HOOK) {
        return ACTIVE_EVAL_HOOK(pos);
    }

    // Incremental PST & Material base (O(1))
    int mg = pos.state->eval_mg;
    int eg = pos.state->eval_eg;

    // Pawn structure
    eval_pawns(pos, mg, eg);

    // Piece features (Bishop pair, open files)
    eval_pieces(pos, mg, eg);

    // King safety (Middlegame only, skip when endgame)
    if (pos.state->game_phase > 0) {
        mg += eval_king_safety(pos, WHITE);
        mg -= eval_king_safety(pos, BLACK);
    }

    // Tapered blending by game phase (0 = Endgame, 24 = Opening/Middlegame)
    int phase = std::clamp(pos.state->game_phase, 0, MAX_PHASE);
    int tapered_score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;

    // Return score from side to move's perspective
    return (pos.side_to_move == WHITE) ? tapered_score : -tapered_score;
}

} // namespace tigerfish
