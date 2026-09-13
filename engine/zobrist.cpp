#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — ZOBRIST HASHING (newengine/zobrist.cpp)
// Deterministic 64-bit keys for position hashing, repetitions, and TT lookups.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"

namespace tigerfish {

inline uint64_t ZOBRIST_PIECES[15][64];
inline uint64_t ZOBRIST_PAWNS[15][64];
inline uint64_t ZOBRIST_CASTLING[16];
inline uint64_t ZOBRIST_EN_PASSANT[8];
inline uint64_t ZOBRIST_SIDE;

// Deterministic SplitMix64 pseudo-random generator
static uint64_t splitmix64(uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

inline void init_zobrist() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    uint64_t seed = 0x123456789ABCDEF0ULL;

    for (int p = 0; p < 15; ++p) {
        for (int sq = 0; sq < 64; ++sq) {
            ZOBRIST_PIECES[p][sq] = splitmix64(seed);
            if (p == W_PAWN || p == B_PAWN) {
                ZOBRIST_PAWNS[p][sq] = splitmix64(seed);
            } else {
                ZOBRIST_PAWNS[p][sq] = 0;
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        ZOBRIST_CASTLING[i] = splitmix64(seed);
    }

    for (int f = 0; f < 8; ++f) {
        ZOBRIST_EN_PASSANT[f] = splitmix64(seed);
    }

    ZOBRIST_SIDE = splitmix64(seed);
}

} // namespace tigerfish
