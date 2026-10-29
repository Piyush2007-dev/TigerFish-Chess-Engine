#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — LEGAL MOVE GENERATION (newengine/movegen.cpp)
// Strictly legal move generator directly generating legal moves without is_in_check.
// Zero dynamic heap allocations (stack-allocated MoveList).
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "bitboards.cpp"
#include "position.cpp"

namespace tigerfish {

// ── Stack-Allocated MoveList Struct ──────────────────────────────────────────
struct MoveList {
    Move moves[256];
    int count = 0;

    constexpr MoveList() noexcept = default;

    void push(Move m) noexcept {
        moves[count++] = m;
    }

    constexpr int size() const noexcept {
        return count;
    }

    constexpr bool empty() const noexcept {
        return count == 0;
    }

    constexpr void clear() noexcept {
        count = 0;
    }

    Move* begin() noexcept { return moves; }
    Move* end() noexcept { return moves + count; }
    const Move* begin() const noexcept { return moves; }
    const Move* end() const noexcept { return moves + count; }

    Move& operator[](int index) noexcept {
        return moves[index];
    }

    const Move& operator[](int index) const noexcept {
        return moves[index];
    }
};

// ── Pawn Move Generation Helpers ─────────────────────────────────────────────
template <Color Us, bool CapturesOnly>
inline void generate_pawn_moves(const Position& pos, MoveList& move_list, Bitboard target_mask) noexcept {
    constexpr Color Them = opponent_color(Us);
    constexpr Rank PromoRank = (Us == WHITE) ? RANK_8 : RANK_1;
    constexpr int Up = (Us == WHITE) ? 8 : -8;

    Bitboard pawns = pos.pieces[make_piece(Us, PT_PAWN)];
    Bitboard pinned = pos.state->pinned;
    Square ksq = pos.king_square(Us);
    if (ksq >= 64) return;
    Bitboard occ = pos.occupancy();
    Bitboard enemies = pos.colors[Them] & ~pos.pieces[make_piece(Them, PT_KING)];

    // 1. Single and Double Pushes (skip if CapturesOnly)
    if constexpr (!CapturesOnly) {
        Bitboard single_pushes = (Us == WHITE) ? ((pawns << 8) & ~occ) : ((pawns >> 8) & ~occ);
        Bitboard valid_singles = single_pushes & target_mask;

        // Non-promotions
        Bitboard normal_singles = valid_singles & ~RANK_MASKS[PromoRank];
        while (normal_singles) {
            Square to = pop_lsb(normal_singles);
            Square from = static_cast<Square>(to - Up);
            // If pinned, can only push if ray is aligned with king
            if (!(pinned & (1ULL << from)) || (line_through(ksq, from) & (1ULL << to))) {
                move_list.push(Move(from, to, MoveFlags::QUIET));
            }
        }

        // Promotions from single pushes
        Bitboard promo_singles = valid_singles & RANK_MASKS[PromoRank];
        while (promo_singles) {
            Square to = pop_lsb(promo_singles);
            Square from = static_cast<Square>(to - Up);
            if (!(pinned & (1ULL << from)) || (line_through(ksq, from) & (1ULL << to))) {
                move_list.push(Move(from, to, MoveFlags::PROMO_QUEEN));
                move_list.push(Move(from, to, MoveFlags::PROMO_ROOK));
                move_list.push(Move(from, to, MoveFlags::PROMO_BISHOP));
                move_list.push(Move(from, to, MoveFlags::PROMO_KNIGHT));
            }
        }

        // Double pushes
        Bitboard eligible_doubles = (Us == WHITE)
            ? (((single_pushes & RANK_MASKS[RANK_3]) << 8) & ~occ)
            : (((single_pushes & RANK_MASKS[RANK_6]) >> 8) & ~occ);
        Bitboard valid_doubles = eligible_doubles & target_mask;

        while (valid_doubles) {
            Square to = pop_lsb(valid_doubles);
            Square from = static_cast<Square>(to - 2 * Up);
            if (!(pinned & (1ULL << from)) || (line_through(ksq, from) & (1ULL << to))) {
                move_list.push(Move(from, to, MoveFlags::DOUBLE_PAWN_PUSH));
            }
        }
    }

    // 2. Diagonal Captures
    Bitboard capture_targets = target_mask & enemies;
    Bitboard pawns_copy = pawns;

    while (pawns_copy) {
        Square from = pop_lsb(pawns_copy);
        Bitboard attacks = pawn_attacks(Us, from) & capture_targets;

        // If pinned, attacks must be along the pin line
        if (pinned & (1ULL << from)) {
            attacks &= line_through(ksq, from);
        }

        while (attacks) {
            Square to = pop_lsb(attacks);
            if (square_rank(to) == PromoRank) {
                move_list.push(Move(from, to, MoveFlags::PROMO_CAP_QUEEN));
                move_list.push(Move(from, to, MoveFlags::PROMO_CAP_ROOK));
                move_list.push(Move(from, to, MoveFlags::PROMO_CAP_BISHOP));
                move_list.push(Move(from, to, MoveFlags::PROMO_CAP_KNIGHT));
            } else {
                move_list.push(Move(from, to, MoveFlags::CAPTURE));
            }
        }
    }

    // 3. En Passant Captures
    Square ep_sq = pos.state->en_passant_square;
    if (ep_sq != SQ_NONE) {
        Square cap_sq = make_square(square_file(ep_sq), (Us == WHITE) ? RANK_5 : RANK_4);
        // EP target is valid if ep_sq is in target_mask OR the captured pawn is the checker
        if ((target_mask & (1ULL << ep_sq)) || (target_mask & (1ULL << cap_sq))) {
            Bitboard ep_pawns = pawn_attacks(Them, ep_sq) & pawns;
            while (ep_pawns) {
                Square from = pop_lsb(ep_pawns);

                // Check pin alignment
                if ((pinned & (1ULL << from)) && !(line_through(ksq, from) & (1ULL << ep_sq))) {
                    continue; // Pinned along a different ray, cannot capture EP
                }

                // Check rare horizontal pin: both pawns disappear, exposing king to horizontal rook/queen
                Bitboard test_occ = (occ ^ (1ULL << from) ^ (1ULL << cap_sq)) | (1ULL << ep_sq);
                Bitboard enemy_sliders = pos.pieces[make_piece(Them, PT_ROOK)] | pos.pieces[make_piece(Them, PT_QUEEN)];
                if (!(rook_attacks(ksq, test_occ) & enemy_sliders)) {
                    move_list.push(Move(from, ep_sq, MoveFlags::EN_PASSANT));
                }
            }
        }
    }
}

// ── Piece Move Generation Helper ─────────────────────────────────────────────
template <PieceType Pt, bool CapturesOnly>
inline void generate_piece_moves(const Position& pos, MoveList& move_list, Bitboard target_mask) noexcept {
    Color us = pos.side_to_move;
    Square ksq = pos.king_square(us);
    if (ksq >= 64) return;
    Bitboard pieces = pos.pieces[make_piece(us, Pt)];
    Bitboard pinned = pos.state->pinned;
    Bitboard occ = pos.occupancy();

    while (pieces) {
        Square from = pop_lsb(pieces);
        Bitboard attacks = 0;

        if constexpr (Pt == PT_KNIGHT) {
            // Pinned knights cannot move anywhere
            if (pinned & (1ULL << from)) continue;
            attacks = knight_attacks(from);
        } else if constexpr (Pt == PT_BISHOP) {
            attacks = bishop_attacks(from, occ);
            if (pinned & (1ULL << from)) attacks &= line_through(ksq, from);
        } else if constexpr (Pt == PT_ROOK) {
            attacks = rook_attacks(from, occ);
            if (pinned & (1ULL << from)) attacks &= line_through(ksq, from);
        } else if constexpr (Pt == PT_QUEEN) {
            attacks = queen_attacks(from, occ);
            if (pinned & (1ULL << from)) attacks &= line_through(ksq, from);
        }

        Bitboard valid_moves = attacks & target_mask;
        while (valid_moves) {
            Square to = pop_lsb(valid_moves);
            uint16_t flag = CapturesOnly ? MoveFlags::CAPTURE :
                            ((pos.colors[opponent_color(us)] & (1ULL << to)) ? MoveFlags::CAPTURE : MoveFlags::QUIET);
            move_list.push(Move(from, to, flag));
        }
    }
}

// ── King Moves & Castling (Direct Strictly Legal) ────────────────────────────
template <bool CapturesOnly>
inline void generate_king_moves(const Position& pos, MoveList& move_list) noexcept {
    Color us = pos.side_to_move;
    Color them = opponent_color(us);
    Square ksq = pos.king_square(us);
    if (ksq >= 64) return;
    Bitboard occ = pos.occupancy();
    Bitboard occ_without_king = occ ^ (1ULL << ksq);

    Bitboard legal_targets = ~pos.colors[us] & ~pos.pieces[make_piece(them, PT_KING)];
    if constexpr (CapturesOnly) {
        legal_targets &= pos.colors[them];
    }

    Bitboard king_dests = king_attacks(ksq) & legal_targets;
    while (king_dests) {
        Square to = pop_lsb(king_dests);
        // Destination square must not be attacked by enemy
        if (!is_square_attacked(pos, to, occ_without_king, them)) {
            uint16_t flag = CapturesOnly ? MoveFlags::CAPTURE :
                            ((pos.colors[them] & (1ULL << to)) ? MoveFlags::CAPTURE : MoveFlags::QUIET);
            move_list.push(Move(ksq, to, flag));
        }
    }

    // Castling moves (only when NOT in check and NOT captures only)
    if constexpr (!CapturesOnly) {
        if (pos.state->checkers == 0) {
            uint8_t rights = pos.state->castling_rights;

            if (us == WHITE) {
                // White Kingside (E1 -> G1, Rook H1 -> F1)
                if ((rights & CASTLE_WHITE_OO) &&
                    !(occ & ((1ULL << F1) | (1ULL << G1))) &&
                    !is_square_attacked(pos, F1, occ, them) &&
                    !is_square_attacked(pos, G1, occ, them)) {
                    move_list.push(Move(E1, G1, MoveFlags::CASTLE_KING));
                }
                // White Queenside (E1 -> C1, Rook A1 -> D1)
                if ((rights & CASTLE_WHITE_OOO) &&
                    !(occ & ((1ULL << D1) | (1ULL << C1) | (1ULL << B1))) &&
                    !is_square_attacked(pos, D1, occ, them) &&
                    !is_square_attacked(pos, C1, occ, them)) {
                    move_list.push(Move(E1, C1, MoveFlags::CASTLE_QUEEN));
                }
            } else {
                // Black Kingside (E8 -> G8, Rook H8 -> F8)
                if ((rights & CASTLE_BLACK_OO) &&
                    !(occ & ((1ULL << F8) | (1ULL << G8))) &&
                    !is_square_attacked(pos, F8, occ, them) &&
                    !is_square_attacked(pos, G8, occ, them)) {
                    move_list.push(Move(E8, G8, MoveFlags::CASTLE_KING));
                }
                // Black Queenside (E8 -> C8, Rook A8 -> D8)
                if ((rights & CASTLE_BLACK_OOO) &&
                    !(occ & ((1ULL << D8) | (1ULL << C8) | (1ULL << B8))) &&
                    !is_square_attacked(pos, D8, occ, them) &&
                    !is_square_attacked(pos, C8, occ, them)) {
                    move_list.push(Move(E8, C8, MoveFlags::CASTLE_QUEEN));
                }
            }
        }
    }
}

// ── Master Legal Move Generator (Zero is_in_check calls) ─────────────────────
template <bool CapturesOnly>
inline void generate_moves_internal(const Position& pos, MoveList& move_list) noexcept {
    move_list.clear();
    // Lazy pin computation: only compute pins when moves are actually generated
    update_pins(const_cast<Position&>(pos));

    Color us = pos.side_to_move;
    Color them = opponent_color(us);
    Square ksq = pos.king_square(us);
    if (ksq >= 64) return;
    Bitboard checkers = pos.state->checkers;
    int check_count = popcount(checkers);
    Bitboard enemy_king = pos.pieces[make_piece(them, PT_KING)];

    // 1. King Moves are ALWAYS generated
    generate_king_moves<CapturesOnly>(pos, move_list);

    // 2. Double Check: ONLY King can move! Skip all other pieces.
    if (check_count >= 2) {
        return;
    }

    // 3. Single Check: Pieces can only capture the checker or block the check ray
    Bitboard target_mask;
    if (check_count == 1) {
        Square checker_sq = lsb(checkers);
        target_mask = squares_between(ksq, checker_sq) | (1ULL << checker_sq);
        if constexpr (CapturesOnly) {
            target_mask &= pos.colors[them];
        }
    } else {
        // No check: can move to any square not occupied by our pieces
        target_mask = ~pos.colors[us];
        if constexpr (CapturesOnly) {
            target_mask &= pos.colors[them];
        }
    }
    target_mask &= ~enemy_king;

    // 4. Generate Moves for All Other Pieces using target_mask
    if (us == WHITE) {
        generate_pawn_moves<WHITE, CapturesOnly>(pos, move_list, target_mask);
    } else {
        generate_pawn_moves<BLACK, CapturesOnly>(pos, move_list, target_mask);
    }

    generate_piece_moves<PT_KNIGHT, CapturesOnly>(pos, move_list, target_mask);
    generate_piece_moves<PT_BISHOP, CapturesOnly>(pos, move_list, target_mask);
    generate_piece_moves<PT_ROOK, CapturesOnly>(pos, move_list, target_mask);
    generate_piece_moves<PT_QUEEN, CapturesOnly>(pos, move_list, target_mask);
}

// ── Public Legal Move Generator Functions ────────────────────────────────────
inline void generate_legal_moves(const Position& pos, MoveList& move_list) noexcept {
    PROFILE_ZONE("GenLegalMoves");
    generate_moves_internal<false>(pos, move_list);
}

inline void generate_captures(const Position& pos, MoveList& move_list) noexcept {
    PROFILE_ZONE("GenCaptures");
    generate_moves_internal<true>(pos, move_list);
}

// ── Ultra-Fast Pure Bitboard Legal Move Counters (Depth-1 / Leaf Bypass) ──────
template <Color Us>
inline uint64_t count_pawn_moves(const Position& pos, Bitboard target_mask) noexcept {
    constexpr Color Them = opponent_color(Us);
    constexpr Rank PromoRank = (Us == WHITE) ? RANK_8 : RANK_1;
    constexpr int Up = (Us == WHITE) ? 8 : -8;

    Bitboard pawns = pos.pieces[make_piece(Us, PT_PAWN)];
    Bitboard pinned = pos.state->pinned;
    Square ksq = pos.king_square(Us);
    if (ksq >= 64) return 0;
    Bitboard occ = pos.occupancy();
    Bitboard enemies = pos.colors[Them] & ~pos.pieces[make_piece(Them, PT_KING)];

    uint64_t count = 0;

    // Single pushes
    Bitboard single_pushes = (Us == WHITE) ? ((pawns << 8) & ~occ) : ((pawns >> 8) & ~occ);
    Bitboard valid_singles = single_pushes & target_mask;

    Bitboard normal_singles = valid_singles & ~RANK_MASKS[PromoRank];
    while (normal_singles) {
        Square to = pop_lsb(normal_singles);
        Square from = static_cast<Square>(to - Up);
        if (!(pinned & (1ULL << from)) || (line_through(ksq, from) & (1ULL << to))) {
            count++;
        }
    }

    Bitboard promo_singles = valid_singles & RANK_MASKS[PromoRank];
    while (promo_singles) {
        Square to = pop_lsb(promo_singles);
        Square from = static_cast<Square>(to - Up);
        if (!(pinned & (1ULL << from)) || (line_through(ksq, from) & (1ULL << to))) {
            count += 4; // Q, R, B, N
        }
    }

    // Double pushes
    Bitboard eligible_doubles = (Us == WHITE)
        ? (((single_pushes & RANK_MASKS[RANK_3]) << 8) & ~occ)
        : (((single_pushes & RANK_MASKS[RANK_6]) >> 8) & ~occ);
    Bitboard valid_doubles = eligible_doubles & target_mask;
    while (valid_doubles) {
        Square to = pop_lsb(valid_doubles);
        Square from = static_cast<Square>(to - 2 * Up);
        if (!(pinned & (1ULL << from)) || (line_through(ksq, from) & (1ULL << to))) {
            count++;
        }
    }

    // Diagonal captures
    Bitboard capture_targets = target_mask & enemies;
    Bitboard pawns_copy = pawns;
    while (pawns_copy) {
        Square from = pop_lsb(pawns_copy);
        Bitboard attacks = pawn_attacks(Us, from) & capture_targets;
        if (pinned & (1ULL << from)) {
            attacks &= line_through(ksq, from);
        }
        Bitboard normal_caps = attacks & ~RANK_MASKS[PromoRank];
        count += popcount(normal_caps);
        Bitboard promo_caps = attacks & RANK_MASKS[PromoRank];
        count += 4 * popcount(promo_caps);
    }

    // En passant captures
    Square ep_sq = pos.state->en_passant_square;
    if (ep_sq != SQ_NONE) {
        Square cap_sq = make_square(square_file(ep_sq), (Us == WHITE) ? RANK_5 : RANK_4);
        if ((target_mask & (1ULL << ep_sq)) || (target_mask & (1ULL << cap_sq))) {
            Bitboard ep_pawns = pawn_attacks(Them, ep_sq) & pawns;
            while (ep_pawns) {
                Square from = pop_lsb(ep_pawns);
                if ((pinned & (1ULL << from)) && !(line_through(ksq, from) & (1ULL << ep_sq))) {
                    continue;
                }
                Bitboard test_occ = (occ ^ (1ULL << from) ^ (1ULL << cap_sq)) | (1ULL << ep_sq);
                Bitboard enemy_sliders = pos.pieces[make_piece(Them, PT_ROOK)] | pos.pieces[make_piece(Them, PT_QUEEN)];
                if (!(rook_attacks(ksq, test_occ) & enemy_sliders)) {
                    count++;
                }
            }
        }
    }

    return count;
}

template <PieceType Pt>
inline uint64_t count_piece_moves(const Position& pos, Bitboard target_mask) noexcept {
    Color us = pos.side_to_move;
    Square ksq = pos.king_square(us);
    if (ksq >= 64) return 0;
    Bitboard pieces = pos.pieces[make_piece(us, Pt)];
    Bitboard pinned = pos.state->pinned;
    Bitboard occ = pos.occupancy();
    uint64_t count = 0;

    while (pieces) {
        Square from = pop_lsb(pieces);
        Bitboard attacks = 0;

        if constexpr (Pt == PT_KNIGHT) {
            if (pinned & (1ULL << from)) continue;
            attacks = knight_attacks(from);
        } else if constexpr (Pt == PT_BISHOP) {
            attacks = bishop_attacks(from, occ);
            if (pinned & (1ULL << from)) attacks &= line_through(ksq, from);
        } else if constexpr (Pt == PT_ROOK) {
            attacks = rook_attacks(from, occ);
            if (pinned & (1ULL << from)) attacks &= line_through(ksq, from);
        } else if constexpr (Pt == PT_QUEEN) {
            attacks = queen_attacks(from, occ);
            if (pinned & (1ULL << from)) attacks &= line_through(ksq, from);
        }

        count += popcount(attacks & target_mask);
    }
    return count;
}

inline uint64_t count_king_moves(const Position& pos) noexcept {
    Color us = pos.side_to_move;
    Color them = opponent_color(us);
    Square ksq = pos.king_square(us);
    if (ksq >= 64) return 0;
    Bitboard occ = pos.occupancy();
    Bitboard occ_without_king = occ ^ (1ULL << ksq);
    Bitboard legal_targets = ~pos.colors[us] & ~pos.pieces[make_piece(them, PT_KING)];
    Bitboard king_dests = king_attacks(ksq) & legal_targets;
    uint64_t count = 0;

    while (king_dests) {
        Square to = pop_lsb(king_dests);
        if (!is_square_attacked(pos, to, occ_without_king, them)) {
            count++;
        }
    }

    // Castling moves (only when not in check)
    if (pos.state->checkers == 0) {
        uint8_t rights = pos.state->castling_rights;
        if (us == WHITE) {
            if ((rights & CASTLE_WHITE_OO) &&
                !(occ & ((1ULL << F1) | (1ULL << G1))) &&
                !is_square_attacked(pos, F1, occ, them) &&
                !is_square_attacked(pos, G1, occ, them)) {
                count++;
            }
            if ((rights & CASTLE_WHITE_OOO) &&
                !(occ & ((1ULL << D1) | (1ULL << C1) | (1ULL << B1))) &&
                !is_square_attacked(pos, D1, occ, them) &&
                !is_square_attacked(pos, C1, occ, them)) {
                count++;
            }
        } else {
            if ((rights & CASTLE_BLACK_OO) &&
                !(occ & ((1ULL << F8) | (1ULL << G8))) &&
                !is_square_attacked(pos, F8, occ, them) &&
                !is_square_attacked(pos, G8, occ, them)) {
                count++;
            }
            if ((rights & CASTLE_BLACK_OOO) &&
                !(occ & ((1ULL << D8) | (1ULL << C8) | (1ULL << B8))) &&
                !is_square_attacked(pos, D8, occ, them) &&
                !is_square_attacked(pos, C8, occ, them)) {
                count++;
            }
        }
    }
    return count;
}

inline uint64_t count_legal_moves(const Position& pos) noexcept {
    update_pins(const_cast<Position&>(pos));

    Color us = pos.side_to_move;
    Color them = opponent_color(us);
    Square ksq = pos.king_square(us);
    if (ksq >= 64) return 0;
    Bitboard checkers = pos.state->checkers;
    int check_count = popcount(checkers);
    Bitboard enemy_king = pos.pieces[make_piece(them, PT_KING)];

    // 1. King Moves
    uint64_t count = count_king_moves(pos);
    if (check_count >= 2) return count;

    // 2. Target Mask
    Bitboard target_mask;
    if (check_count == 1) {
        Square checker_sq = lsb(checkers);
        target_mask = squares_between(ksq, checker_sq) | (1ULL << checker_sq);
    } else {
        target_mask = ~pos.colors[us];
    }
    target_mask &= ~enemy_king;

    // 3. Pawns and Pieces
    if (us == WHITE) {
        count += count_pawn_moves<WHITE>(pos, target_mask);
    } else {
        count += count_pawn_moves<BLACK>(pos, target_mask);
    }

    count += count_piece_moves<PT_KNIGHT>(pos, target_mask);
    count += count_piece_moves<PT_BISHOP>(pos, target_mask);
    count += count_piece_moves<PT_ROOK>(pos, target_mask);
    count += count_piece_moves<PT_QUEEN>(pos, target_mask);

    return count;
}

// ── Perft Move Counter (Testing & Validation) ────────────────────────────────
inline uint64_t perft(Position& pos, int depth) noexcept {
    if (depth <= 0) return 1ULL;
    if (depth == 1) return count_legal_moves(pos);

    MoveList move_list;
    generate_legal_moves(pos, move_list);

    uint64_t nodes = 0;
    StateInfo state;
    for (int i = 0; i < move_list.size(); ++i) {
        make_move(pos, move_list[i], state);
        nodes += perft(pos, depth - 1);
        undo_move(pos, move_list[i]);
    }
    return nodes;
}

} // namespace tigerfish
