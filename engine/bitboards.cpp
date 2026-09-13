#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — BITBOARD & ATTACK LOOKUPS (newengine/bitboards.cpp)
// Precomputed attack tables, ray masks, and dense Magic Bitboards.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"

namespace tigerfish {

// ── File and Rank Bitboards ──────────────────────────────────────────────────
constexpr Bitboard FILE_MASKS[8] = {
    0x0101010101010101ULL, 0x0202020202020202ULL,
    0x0404040404040404ULL, 0x0808080808080808ULL,
    0x1010101010101010ULL, 0x2020202020202020ULL,
    0x4040404040404040ULL, 0x8080808080808080ULL
};

constexpr Bitboard RANK_MASKS[8] = {
    0x00000000000000FFULL, 0x000000000000FF00ULL,
    0x0000000000FF0000ULL, 0x00000000FF000000ULL,
    0x000000FF00000000ULL, 0x0000FF0000000000ULL,
    0x00FF000000000000ULL, 0xFF00000000000000ULL
};

constexpr Bitboard NOT_FILE_A = ~FILE_MASKS[FILE_A];
constexpr Bitboard NOT_FILE_H = ~FILE_MASKS[FILE_H];

// ── Precomputed Non-Slider Attack Lookups ─────────────────────────────────────
inline Bitboard PAWN_ATTACKS[2][64];
inline Bitboard KNIGHT_ATTACKS[64];
inline Bitboard KING_ATTACKS[64];

// ── Precomputed Ray Lines & Between Masks ─────────────────────────────────────
inline Bitboard BETWEEN_BB[64][64];
inline Bitboard LINE_BB[64][64];
inline Bitboard DIAGONAL_MASKS[64];
inline Bitboard ORTHOGONAL_MASKS[64];

// ── Magic Bitboards Constants & Dense Storage ────────────────────────────────
inline const uint64_t ROOK_MAGICS[64] = {
    0x8a80104000800020ULL, 0x140002000100040ULL,  0x2801880a0017001ULL,
    0x100081001000420ULL,  0x200020010080420ULL,  0x3001c0002010008ULL,
    0x8480008002000100ULL, 0x2080088004402900ULL, 0x800098204000ULL,
    0x2024401000200040ULL, 0x100802000801000ULL,  0x120800800801000ULL,
    0x208808088000400ULL,  0x2802200800400ULL,     0x2200800100020080ULL,
    0x801000060821100ULL,  0x80044006422000ULL,    0x100808020004000ULL,
    0x12108a0010204200ULL, 0x140848010000802ULL,  0x481828014002800ULL,
    0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL,  0x2040002120081000ULL, 0x21200680100081ULL,
    0x20100080080080ULL,   0x2000a00200410ULL,     0x20080800400ULL,
    0x80088400100102ULL,   0x80004600042881ULL,    0x4040008040800020ULL,
    0x440003000200801ULL,  0x4200011004500ULL,     0x188020010100100ULL,
    0x14800401802800ULL,   0x2080040080800200ULL, 0x124080204001001ULL,
    0x200046502000484ULL,  0x480400080088020ULL,   0x1000422010034000ULL,
    0x30200100110040ULL,   0x100021010009ULL,      0x2002080100110004ULL,
    0x202008004008002ULL,  0x20020004010100ULL,    0x2048440040820001ULL,
    0x101002200408200ULL,  0x40802000401080ULL,    0x4008142004410100ULL,
    0x2060820c0120200ULL,  0x1001004080100ULL,     0x20c020080040080ULL,
    0x2935610830022400ULL, 0x44440041009200ULL,   0x280001040802101ULL,
    0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL,    0x12001008414402ULL,    0x2006104900a0804ULL,
    0x1004081002402ULL
};

inline const uint64_t BISHOP_MAGICS[64] = {
    0x40040844404084ULL,   0x2004208a004208ULL,   0x10190041080202ULL,
    0x108060845042010ULL,  0x581104180800210ULL,  0x2112080446200010ULL,
    0x1080820820060210ULL, 0x3c0808410220200ULL,  0x4050404440404ULL,
    0x21001420088ULL,      0x24d0080801082102ULL, 0x1020a0a020400ULL,
    0x40308200402ULL,      0x4011002100800ULL,     0x401484104104005ULL,
    0x801010402020200ULL,  0x400210c3880100ULL,   0x404022024108200ULL,
    0x810018200204102ULL,  0x4002801a02003ULL,    0x85040820080400ULL,
    0x810102c808880400ULL, 0xe900410884800ULL,    0x8002020480840102ULL,
    0x220200865090201ULL,  0x2010100a02021202ULL, 0x152048408022401ULL,
    0x20080002081110ULL,   0x4001001021004000ULL, 0x800040400a011002ULL,
    0xe4004081011002ULL,   0x1c004001012080ULL,   0x8004200962a00220ULL,
    0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
    0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL,
    0x42008c0340209202ULL, 0x209188240001000ULL,  0x400408a884001800ULL,
    0x110400a6080400ULL,   0x1840060a44020800ULL, 0x90080104000041ULL,
    0x201011000808101ULL,  0x1a2208080504f080ULL, 0x8012020600211212ULL,
    0x500861011240000ULL,  0x180806108200800ULL,  0x4000020e01040044ULL,
    0x300000261044000aULL, 0x802241102020002ULL,  0x20906061210001ULL,
    0x5a84841004010310ULL, 0x4010801011c04ULL,    0xa010109502200ULL,
    0x4a02012000ULL,       0x500201010098b028ULL, 0x8040002811040900ULL,
    0x28000010020204ULL,   0x6000020202d0240ULL,  0x8918844842082200ULL,
    0x4010011029020020ULL
};

inline int ROOK_SHIFT[64];
inline int BISHOP_SHIFT[64];
inline Bitboard ROOK_MASK[64];
inline Bitboard BISHOP_MASK[64];

// Dense flat attack buffers:
// Rook: 102,400 entries (800 KB). Bishop: 5,248 entries (41 KB). Total < 850 KB.
inline Bitboard ROOK_TABLE[102400];
inline Bitboard BISHOP_TABLE[5248];

inline Bitboard* ROOK_ATTACKS[64];
inline Bitboard* BISHOP_ATTACKS[64];

// ── Slider Attack Lookups ────────────────────────────────────────────────────
inline Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept {
#if defined(__BMI2__)
    return ROOK_ATTACKS[sq][_pext_u64(occupied, ROOK_MASK[sq])];
#else
    occupied &= ROOK_MASK[sq];
    return ROOK_ATTACKS[sq][(occupied * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]];
#endif
}

inline Bitboard bishop_attacks(Square sq, Bitboard occupied) noexcept {
#if defined(__BMI2__)
    return BISHOP_ATTACKS[sq][_pext_u64(occupied, BISHOP_MASK[sq])];
#else
    occupied &= BISHOP_MASK[sq];
    return BISHOP_ATTACKS[sq][(occupied * BISHOP_MAGICS[sq]) >> BISHOP_SHIFT[sq]];
#endif
}

inline Bitboard queen_attacks(Square sq, Bitboard occupied) noexcept {
    return rook_attacks(sq, occupied) | bishop_attacks(sq, occupied);
}

// ── Non-Slider Attack Helpers ────────────────────────────────────────────────
inline Bitboard pawn_attacks(Color c, Square sq) noexcept {
    return PAWN_ATTACKS[c][sq];
}

inline Bitboard knight_attacks(Square sq) noexcept {
    return KNIGHT_ATTACKS[sq];
}

inline Bitboard king_attacks(Square sq) noexcept {
    return KING_ATTACKS[sq];
}

inline Bitboard squares_between(Square sq1, Square sq2) noexcept {
    return BETWEEN_BB[sq1][sq2];
}

inline Bitboard line_through(Square sq1, Square sq2) noexcept {
    return LINE_BB[sq1][sq2];
}

// ── Initialization Internals ─────────────────────────────────────────────────
static Bitboard compute_rook_attacks_slow(Square sq, Bitboard blockers) noexcept {
    Bitboard attacks = 0;
    int r = square_rank(sq), f = square_file(sq);
    for (int i = r + 1; i < 8; i++) { attacks |= (1ULL << (i * 8 + f)); if (blockers & (1ULL << (i * 8 + f))) break; }
    for (int i = r - 1; i >= 0; i--) { attacks |= (1ULL << (i * 8 + f)); if (blockers & (1ULL << (i * 8 + f))) break; }
    for (int i = f + 1; i < 8; i++) { attacks |= (1ULL << (r * 8 + i)); if (blockers & (1ULL << (r * 8 + i))) break; }
    for (int i = f - 1; i >= 0; i--) { attacks |= (1ULL << (r * 8 + i)); if (blockers & (1ULL << (r * 8 + i))) break; }
    return attacks;
}

static Bitboard compute_bishop_attacks_slow(Square sq, Bitboard blockers) noexcept {
    Bitboard attacks = 0;
    int r = square_rank(sq), f = square_file(sq);
    for (int i = r + 1, j = f + 1; i < 8 && j < 8; i++, j++) { attacks |= (1ULL << (i * 8 + j)); if (blockers & (1ULL << (i * 8 + j))) break; }
    for (int i = r + 1, j = f - 1; i < 8 && j >= 0; i++, j--) { attacks |= (1ULL << (i * 8 + j)); if (blockers & (1ULL << (i * 8 + j))) break; }
    for (int i = r - 1, j = f + 1; i >= 0 && j < 8; i--, j++) { attacks |= (1ULL << (i * 8 + j)); if (blockers & (1ULL << (i * 8 + j))) break; }
    for (int i = r - 1, j = f - 1; i >= 0 && j >= 0; i--, j--) { attacks |= (1ULL << (i * 8 + j)); if (blockers & (1ULL << (i * 8 + j))) break; }
    return attacks;
}

inline void init_bitboards() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // 1. Pawn attacks
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard b = (1ULL << sq);
        // White attacks: NE and NW
        PAWN_ATTACKS[WHITE][sq] = ((b & NOT_FILE_A) << 7) | ((b & NOT_FILE_H) << 9);
        // Black attacks: SE and SW
        PAWN_ATTACKS[BLACK][sq] = ((b & NOT_FILE_A) >> 9) | ((b & NOT_FILE_H) >> 7);
    }

    // 2. Knight attacks
    for (int sq = 0; sq < 64; ++sq) {
        int r = square_rank(static_cast<Square>(sq));
        int f = square_file(static_cast<Square>(sq));
        Bitboard b = 0;
        int dr[8] = { -2, -2, -1, -1,  1,  1,  2,  2 };
        int df[8] = { -1,  1, -2,  2, -2,  2, -1,  1 };
        for (int i = 0; i < 8; ++i) {
            int nr = r + dr[i];
            int nf = f + df[i];
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                b |= (1ULL << (nr * 8 + nf));
            }
        }
        KNIGHT_ATTACKS[sq] = b;
    }

    // 3. King attacks
    for (int sq = 0; sq < 64; ++sq) {
        int r = square_rank(static_cast<Square>(sq));
        int f = square_file(static_cast<Square>(sq));
        Bitboard b = 0;
        for (int dr = -1; dr <= 1; ++dr) {
            for (int df = -1; df <= 1; ++df) {
                if (dr == 0 && df == 0) continue;
                int nr = r + dr;
                int nf = f + df;
                if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    b |= (1ULL << (nr * 8 + nf));
                }
            }
        }
        KING_ATTACKS[sq] = b;
    }

    // 4. Magic Bitboard Initialization
    Bitboard* rook_ptr = ROOK_TABLE;
    Bitboard* bishop_ptr = BISHOP_TABLE;

    for (int sq = 0; sq < 64; ++sq) {
        int r = sq / 8, f = sq % 8;

        Bitboard rmask = 0;
        for (int i = r + 1; i < 7; ++i) rmask |= (1ULL << (i * 8 + f));
        for (int i = r - 1; i > 0; --i) rmask |= (1ULL << (i * 8 + f));
        for (int i = f + 1; i < 7; ++i) rmask |= (1ULL << (r * 8 + i));
        for (int i = f - 1; i > 0; --i) rmask |= (1ULL << (r * 8 + i));
        ROOK_MASK[sq] = rmask;
        ROOK_SHIFT[sq] = 64 - popcount(rmask);

        Bitboard bmask = 0;
        for (int i = r + 1, j = f + 1; i < 7 && j < 7; ++i, ++j) bmask |= (1ULL << (i * 8 + j)); // NE
        for (int i = r + 1, j = f - 1; i < 7 && j > 0; ++i, --j) bmask |= (1ULL << (i * 8 + j)); // NW
        for (int i = r - 1, j = f + 1; i > 0 && j < 7; --i, ++j) bmask |= (1ULL << (i * 8 + j)); // SE
        for (int i = r - 1, j = f - 1; i > 0 && j > 0; --i, --j) bmask |= (1ULL << (i * 8 + j)); // SW
        BISHOP_MASK[sq] = bmask;
        BISHOP_SHIFT[sq] = 64 - popcount(bmask);

        ROOK_ATTACKS[sq] = rook_ptr;
        BISHOP_ATTACKS[sq] = bishop_ptr;

        // Carry-ripple enumeration for Rooks
        Bitboard rsub = 0;
        do {
#if defined(__BMI2__)
            int idx = static_cast<int>(_pext_u64(rsub, rmask));
#else
            int idx = static_cast<int>((rsub * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]);
#endif
            ROOK_ATTACKS[sq][idx] = compute_rook_attacks_slow(static_cast<Square>(sq), rsub);
            rsub = (rsub - 1) & rmask;
        } while (rsub != 0);

        // Carry-ripple enumeration for Bishops
        Bitboard bsub = 0;
        do {
#if defined(__BMI2__)
            int idx = static_cast<int>(_pext_u64(bsub, bmask));
#else
            int idx = static_cast<int>((bsub * BISHOP_MAGICS[sq]) >> BISHOP_SHIFT[sq]);
#endif
            BISHOP_ATTACKS[sq][idx] = compute_bishop_attacks_slow(static_cast<Square>(sq), bsub);
            bsub = (bsub - 1) & bmask;
        } while (bsub != 0);

        rook_ptr += (1ULL << popcount(rmask));
        bishop_ptr += (1ULL << popcount(bmask));
    }

    // 5. BETWEEN_BB and LINE_BB Tables
    for (int sq1 = 0; sq1 < 64; ++sq1) {
        for (int sq2 = 0; sq2 < 64; ++sq2) {
            BETWEEN_BB[sq1][sq2] = 0;
            LINE_BB[sq1][sq2] = 0;

            Square s1 = static_cast<Square>(sq1);
            Square s2 = static_cast<Square>(sq2);

            Bitboard b1 = 1ULL << sq1;
            Bitboard b2 = 1ULL << sq2;

            // Check if aligned on rook or bishop ray
            Bitboard rook_line = rook_attacks(s1, 0);
            Bitboard bishop_line = bishop_attacks(s1, 0);

            if (rook_line & b2) {
                LINE_BB[sq1][sq2] = (rook_attacks(s1, 0) & rook_attacks(s2, 0)) | b1 | b2;
                BETWEEN_BB[sq1][sq2] = rook_attacks(s1, b2) & rook_attacks(s2, b1);
            } else if (bishop_line & b2) {
                LINE_BB[sq1][sq2] = (bishop_attacks(s1, 0) & bishop_attacks(s2, 0)) | b1 | b2;
                BETWEEN_BB[sq1][sq2] = bishop_attacks(s1, b2) & bishop_attacks(s2, b1);
            }
        }
    }

    // 6. DIAGONAL_MASKS and ORTHOGONAL_MASKS Tables
    for (int sq = 0; sq < 64; ++sq) {
        Square s = static_cast<Square>(sq);
        DIAGONAL_MASKS[sq] = bishop_attacks(s, 0);
        ORTHOGONAL_MASKS[sq] = rook_attacks(s, 0);
    }
}

} // namespace tigerfish
