#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — TYPES & PRIMITIVES (newengine/types.cpp)
// Pure C++20 Data-Oriented Design (DOD). Zero header files, zero OOP overhead.
// ══════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <array>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <bit>

#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <immintrin.h>
#endif

#include "profiler.h"

namespace tigerfish {

// ── Fundamental Bitboard Type ────────────────────────────────────────────────
using Bitboard = uint64_t;

constexpr Bitboard EMPTY_BB = 0ULL;
constexpr Bitboard ALL_SQUARES_BB = ~0ULL;

// ── Colors ───────────────────────────────────────────────────────────────────
enum Color : uint8_t {
    WHITE = 0,
    BLACK = 1,
    COLOR_NONE = 2
};

constexpr Color opponent_color(Color c) noexcept {
    return static_cast<Color>(c ^ 1);
}

// ── Piece Types ──────────────────────────────────────────────────────────────
enum PieceType : uint8_t {
    PT_NONE   = 0,
    PT_PAWN   = 1,
    PT_KNIGHT = 2,
    PT_BISHOP = 3,
    PT_ROOK   = 4,
    PT_QUEEN  = 5,
    PT_KING   = 6
};

// ── Pieces (White: 1..6, Black: 9..14, None: 0) ──────────────────────────────
// Bit 3 (value 8) indicates Black piece. Bits 0-2 indicate PieceType.
enum Piece : uint8_t {
    NO_PIECE = 0,
    
    W_PAWN   = 1,
    W_KNIGHT = 2,
    W_BISHOP = 3,
    W_ROOK   = 4,
    W_QUEEN  = 5,
    W_KING   = 6,

    B_PAWN   = 9,
    B_KNIGHT = 10,
    B_BISHOP = 11,
    B_ROOK   = 12,
    B_QUEEN  = 13,
    B_KING   = 14
};

constexpr Piece make_piece(Color color, PieceType pt) noexcept {
    return static_cast<Piece>((color == WHITE ? 0 : 8) | pt);
}

constexpr Color piece_color(Piece p) noexcept {
    return static_cast<Color>((p >> 3) & 1);
}

constexpr PieceType piece_type(Piece p) noexcept {
    return static_cast<PieceType>(p & 7);
}

// ── Board Squares (A1 = 0 ... H8 = 63) ───────────────────────────────────────
enum Square : uint8_t {
    A1 = 0,  B1 = 1,  C1 = 2,  D1 = 3,  E1 = 4,  F1 = 5,  G1 = 6,  H1 = 7,
    A2 = 8,  B2 = 9,  C2 = 10, D2 = 11, E2 = 12, F2 = 13, G2 = 14, H2 = 15,
    A3 = 16, B3 = 17, C3 = 18, D3 = 19, E3 = 20, F3 = 21, G3 = 22, H3 = 23,
    A4 = 24, B4 = 25, C4 = 26, D4 = 27, E4 = 28, F4 = 29, G4 = 30, H4 = 31,
    A5 = 32, B5 = 33, C5 = 34, D5 = 35, E5 = 36, F5 = 37, G5 = 38, H5 = 39,
    A6 = 40, B6 = 41, C6 = 42, D6 = 43, E6 = 44, F6 = 45, G6 = 46, H6 = 47,
    A7 = 48, B7 = 49, C7 = 50, D7 = 51, E7 = 52, F7 = 53, G7 = 54, H7 = 55,
    A8 = 56, B8 = 57, C8 = 58, D8 = 59, E8 = 60, F8 = 61, G8 = 62, H8 = 63,
    SQ_NONE = 64
};

enum File : uint8_t { FILE_A = 0, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H };
enum Rank : uint8_t { RANK_1 = 0, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8 };

constexpr Square make_square(File f, Rank r) noexcept {
    return static_cast<Square>((r << 3) | f);
}

constexpr File square_file(Square sq) noexcept {
    return static_cast<File>(sq & 7);
}

constexpr Rank square_rank(Square sq) noexcept {
    return static_cast<Rank>(sq >> 3);
}

inline std::string square_name(Square sq) {
    if (sq >= SQ_NONE) return "-";
    std::string name;
    name += static_cast<char>('a' + square_file(sq));
    name += static_cast<char>('1' + square_rank(sq));
    return name;
}

inline Square square_from_name(std::string_view str) {
    if (str.length() < 2) return SQ_NONE;
    int file = str[0] - 'a';
    int rank = str[1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return SQ_NONE;
    return make_square(static_cast<File>(file), static_cast<Rank>(rank));
}

// ── Castling Rights Bitfield ─────────────────────────────────────────────────
enum CastlingRights : uint8_t {
    CASTLE_NONE       = 0,
    CASTLE_WHITE_OO   = 1,   // White Kingside
    CASTLE_WHITE_OOO  = 2,   // White Queenside
    CASTLE_BLACK_OO   = 4,   // Black Kingside
    CASTLE_BLACK_OOO  = 8,   // Black Queenside
    CASTLE_WHITE_ALL  = CASTLE_WHITE_OO | CASTLE_WHITE_OOO,
    CASTLE_BLACK_ALL  = CASTLE_BLACK_OO | CASTLE_BLACK_OOO,
    CASTLE_ALL        = CASTLE_WHITE_ALL | CASTLE_BLACK_ALL
};

// ── Move Flags (4 bits in bits 12..15 of Move) ───────────────────────────────
namespace MoveFlags {
    constexpr uint16_t QUIET            = 0x0000;
    constexpr uint16_t DOUBLE_PAWN_PUSH = 0x1000;
    constexpr uint16_t CASTLE_KING      = 0x2000;
    constexpr uint16_t CASTLE_QUEEN     = 0x3000;
    constexpr uint16_t CAPTURE          = 0x4000;
    constexpr uint16_t EN_PASSANT       = 0x5000;
    constexpr uint16_t PROMO_KNIGHT     = 0x8000;
    constexpr uint16_t PROMO_BISHOP     = 0x9000;
    constexpr uint16_t PROMO_ROOK       = 0xA000;
    constexpr uint16_t PROMO_QUEEN      = 0xB000;
    constexpr uint16_t PROMO_CAP_KNIGHT = 0xC000;
    constexpr uint16_t PROMO_CAP_BISHOP = 0xD000;
    constexpr uint16_t PROMO_CAP_ROOK   = 0xE000;
    constexpr uint16_t PROMO_CAP_QUEEN  = 0xF000;
}

// ── Packed 16-Bit Move POD Struct ────────────────────────────────────────────
// Bits 0..5:  from square (0..63)
// Bits 6..11: to square   (0..63)
// Bits 12..15: move flag
struct Move {
    uint16_t raw_data = 0;

    constexpr Move() noexcept = default;
    constexpr explicit Move(uint16_t data) noexcept : raw_data(data) {}
    constexpr Move(Square from, Square to, uint16_t flag = MoveFlags::QUIET) noexcept
        : raw_data(static_cast<uint16_t>(from | (to << 6) | flag)) {}

    constexpr uint16_t raw() const noexcept {
        return raw_data;
    }

    constexpr Square from() const noexcept {
        return static_cast<Square>(raw_data & 0x3F);
    }

    constexpr Square to() const noexcept {
        return static_cast<Square>((raw_data >> 6) & 0x3F);
    }

    constexpr uint16_t flags() const noexcept {
        return raw_data & 0xF000;
    }

    constexpr bool is_none() const noexcept {
        return raw_data == 0;
    }

    constexpr bool is_capture() const noexcept {
        return (raw_data & 0x4000) != 0;
    }

    constexpr bool is_en_passant() const noexcept {
        return flags() == MoveFlags::EN_PASSANT;
    }

    constexpr bool is_castling() const noexcept {
        return flags() == MoveFlags::CASTLE_KING || flags() == MoveFlags::CASTLE_QUEEN;
    }

    constexpr bool is_promotion() const noexcept {
        return (raw_data & 0x8000) != 0;
    }

    constexpr PieceType promotion_type() const noexcept {
        switch (flags()) {
            case MoveFlags::PROMO_KNIGHT:
            case MoveFlags::PROMO_CAP_KNIGHT: return PT_KNIGHT;
            case MoveFlags::PROMO_BISHOP:
            case MoveFlags::PROMO_CAP_BISHOP: return PT_BISHOP;
            case MoveFlags::PROMO_ROOK:
            case MoveFlags::PROMO_CAP_ROOK:   return PT_ROOK;
            case MoveFlags::PROMO_QUEEN:
            case MoveFlags::PROMO_CAP_QUEEN:  return PT_QUEEN;
            default:                          return PT_NONE;
        }
    }

    std::string to_uci() const {
        if (is_none()) return "0000";
        std::string s = square_name(from()) + square_name(to());
        if (is_promotion()) {
            switch (promotion_type()) {
                case PT_KNIGHT: s += 'n'; break;
                case PT_BISHOP: s += 'b'; break;
                case PT_ROOK:   s += 'r'; break;
                case PT_QUEEN:  s += 'q'; break;
                default: break;
            }
        }
        return s;
    }

    constexpr bool operator==(const Move& other) const noexcept {
        return raw_data == other.raw_data;
    }

    constexpr bool operator!=(const Move& other) const noexcept {
        return raw_data != other.raw_data;
    }
};

constexpr Move MOVE_NONE = Move(0);

// ── Hardware Bitwise Intrinsics (1-Cycle Operations) ─────────────────────────
inline int popcount(Bitboard mask) noexcept {
#if defined(__POPCNT__)
    return static_cast<int>(_mm_popcnt_u64(mask));
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(mask);
#else
    return std::popcount(mask);
#endif
}

inline Square lsb(Bitboard mask) noexcept {
#if defined(__BMI__)
    return static_cast<Square>(_tzcnt_u64(mask));
#elif defined(__GNUC__) || defined(__clang__)
    return static_cast<Square>(__builtin_ctzll(mask));
#else
    return static_cast<Square>(std::countr_zero(mask));
#endif
}

inline Square msb(Bitboard mask) noexcept {
#if defined(__LZCNT__)
    return static_cast<Square>(63 - _lzcnt_u64(mask));
#elif defined(__GNUC__) || defined(__clang__)
    return static_cast<Square>(63 - __builtin_clzll(mask));
#else
    return static_cast<Square>(63 - std::countl_zero(mask));
#endif
}

inline Square pop_lsb(Bitboard& mask) noexcept {
    Square sq = lsb(mask);
#if defined(__BMI__)
    mask = _blsr_u64(mask);
#else
    mask &= mask - 1;
#endif
    return sq;
}

// ── Evaluation & Game Score Constants ────────────────────────────────────────
constexpr int SCORE_ZERO     = 0;
constexpr int SCORE_MATE     = 30000;
constexpr int SCORE_MATE_BOUND = 29000;
constexpr int SCORE_INFINITY = 32000;

constexpr bool is_mate_score(int score) noexcept {
    return score >= SCORE_MATE_BOUND || score <= -SCORE_MATE_BOUND;
}

constexpr int mate_in_plies(int score) noexcept {
    if (score >= SCORE_MATE_BOUND) return SCORE_MATE - score;
    if (score <= -SCORE_MATE_BOUND) return -SCORE_MATE - score;
    return 0;
}

// Piece basic values (MG and EG)
constexpr int PIECE_VALUES_MG[7] = { 0, 100, 320, 330, 500, 900, 20000 };
constexpr int PIECE_VALUES_EG[7] = { 0, 120, 310, 330, 510, 980, 20000 };

// Game phase weights (Total phase = 24)
constexpr int PHASE_WEIGHTS[7] = { 0, 0, 1, 1, 2, 4, 0 };
constexpr int MAX_PHASE = 24;

} // namespace tigerfish
