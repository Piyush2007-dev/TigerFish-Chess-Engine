#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — POSITION STATE & BOARD MUTATION (newengine/position.cpp)
// Clean Data-Oriented Design (DOD). Zero heap allocations, incremental PST & Zobrist.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "bitboards.cpp"
#include "zobrist.cpp"

namespace tigerfish {

// ── Piece-Square Tables (White perspective, mirror for Black) ─────────────────
constexpr Square mirror_square(Square sq) noexcept {
    return static_cast<Square>((7 - square_rank(sq)) * 8 + square_file(sq));
}

constexpr int PST_PAWN_MG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0
};

constexpr int PST_PAWN_EG[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0
};

constexpr int PST_KNIGHT_MG[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

constexpr int PST_KNIGHT_EG[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

constexpr int PST_BISHOP_MG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

constexpr int PST_BISHOP_EG[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

constexpr int PST_ROOK_MG[64] = {
      0,  0,  0,  5,  5,  0,  0,  0,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      5, 10, 10, 10, 10, 10, 10,  5,
      0,  0,  0,  0,  0,  0,  0,  0
};

constexpr int PST_ROOK_EG[64] = {
      0,  0,  0,  5,  5,  0,  0,  0,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      5, 10, 10, 10, 10, 10, 10,  5,
      0,  0,  0,  0,  0,  0,  0,  0
};

constexpr int PST_QUEEN_MG[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

constexpr int PST_QUEEN_EG[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -10,  5,  5,  5,  5,  5,  0,-10,
      0,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
    -10,  0,  5,  5,  5,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

constexpr int PST_KING_MG[64] = {
     20, 30, 10,  0,  0, 10, 30, 20,
     20, 20,  0,  0,  0,  0, 20, 20,
    -10,-20,-20,-20,-20,-20,-20,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30
};

constexpr int PST_KING_EG[64] = {
    -50,-30,-10, -5, -5,-10,-30,-50,
    -30,-10,  5, 10, 10,  5,-10,-30,
    -10,  5, 15, 20, 20, 15,  5,-10,
     -5, 10, 20, 30, 30, 20, 10, -5,
     -5, 10, 20, 30, 30, 20, 10, -5,
    -10,  5, 15, 20, 20, 15,  5,-10,
    -30,-10,  5, 10, 10,  5,-10,-30,
    -50,-30,-10, -5, -5,-10,-30,-50
};

// Flattened signed PST table [Piece 0..14][Square 0..63]
// White is positive, Black is negative.
inline int PIECE_PST_MG[15][64];
inline int PIECE_PST_EG[15][64];

inline void init_pst_tables() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    const int* mg_src[7] = { nullptr, PST_PAWN_MG, PST_KNIGHT_MG, PST_BISHOP_MG, PST_ROOK_MG, PST_QUEEN_MG, PST_KING_MG };
    const int* eg_src[7] = { nullptr, PST_PAWN_EG, PST_KNIGHT_EG, PST_BISHOP_EG, PST_ROOK_EG, PST_QUEEN_EG, PST_KING_EG };

    for (int p = 0; p < 15; ++p) {
        Piece piece = static_cast<Piece>(p);
        PieceType pt = piece_type(piece);
        Color c = piece_color(piece);

        for (int sq = 0; sq < 64; ++sq) {
            Square s = static_cast<Square>(sq);
            if (pt < 1 || pt > 6) {
                PIECE_PST_MG[p][sq] = 0;
                PIECE_PST_EG[p][sq] = 0;
                continue;
            }

            Square table_sq = (c == WHITE) ? s : mirror_square(s);
            int mg = mg_src[pt][table_sq] + PIECE_VALUES_MG[pt];
            int eg = eg_src[pt][table_sq] + PIECE_VALUES_EG[pt];

            PIECE_PST_MG[p][sq] = (c == WHITE) ? mg : -mg;
            PIECE_PST_EG[p][sq] = (c == WHITE) ? eg : -eg;
        }
    }
}

// ── Reversible State Stack (Zero Allocation) ─────────────────────────────────
struct StateInfo {
    Piece captured_piece = NO_PIECE;
    uint8_t castling_rights = CASTLE_NONE;
    Square en_passant_square = SQ_NONE;
    uint8_t halfmove_clock = 0;
    uint64_t zobrist_key = 0;
    uint64_t pawn_zobrist_key = 0;
    int eval_mg = 0;
    int eval_eg = 0;
    int game_phase = 0;

    Bitboard checkers = 0;
    Bitboard pinned = 0;

    StateInfo* previous = nullptr;
    Move move = MOVE_NONE;
    Piece moving_piece = NO_PIECE;
};

// ── Position POD Struct ──────────────────────────────────────────────────────
struct Position {
    Bitboard pieces[15];     // Bitboard per Piece enum
    Bitboard colors[2];      // White and Black occupancy
    Piece mailbox[64];       // Instantaneous O(1) piece on square
    Color side_to_move = WHITE;
    int game_ply = 0;
    StateInfo* state = nullptr;

    Bitboard occupancy() const noexcept {
        return colors[WHITE] | colors[BLACK];
    }

    Square king_square(Color c) const noexcept {
        Piece king = (c == WHITE) ? W_KING : B_KING;
        Bitboard b = pieces[king];
        return b ? lsb(b) : SQ_NONE;
    }
};

// ── Piece Placement Helpers (with Incremental State Updates) ─────────────────
inline void put_piece(Position& pos, Piece p, Square sq) noexcept {
    Bitboard mask = 1ULL << sq;
    Color c = piece_color(p);

    pos.pieces[p] |= mask;
    pos.colors[c] |= mask;
    pos.mailbox[sq] = p;

    // Incremental Zobrist
    pos.state->zobrist_key ^= ZOBRIST_PIECES[p][sq];
    if (piece_type(p) == PT_PAWN) {
        pos.state->pawn_zobrist_key ^= ZOBRIST_PAWNS[p][sq];
    }

    // Incremental Evaluation
    pos.state->eval_mg += PIECE_PST_MG[p][sq];
    pos.state->eval_eg += PIECE_PST_EG[p][sq];
    pos.state->game_phase += PHASE_WEIGHTS[piece_type(p)];
}

inline void remove_piece(Position& pos, Square sq) noexcept {
    Piece p = pos.mailbox[sq];
    Bitboard mask = ~(1ULL << sq);
    Color c = piece_color(p);

    pos.pieces[p] &= mask;
    pos.colors[c] &= mask;
    pos.mailbox[sq] = NO_PIECE;

    // Incremental Zobrist
    pos.state->zobrist_key ^= ZOBRIST_PIECES[p][sq];
    if (piece_type(p) == PT_PAWN) {
        pos.state->pawn_zobrist_key ^= ZOBRIST_PAWNS[p][sq];
    }

    // Incremental Evaluation
    pos.state->eval_mg -= PIECE_PST_MG[p][sq];
    pos.state->eval_eg -= PIECE_PST_EG[p][sq];
    pos.state->game_phase -= PHASE_WEIGHTS[piece_type(p)];
}

inline void move_piece(Position& pos, Square from, Square to) noexcept {
    Piece p = pos.mailbox[from];
    Color c = piece_color(p);
    Bitboard from_to = (1ULL << from) | (1ULL << to);

    pos.pieces[p] ^= from_to;
    pos.colors[c] ^= from_to;
    pos.mailbox[from] = NO_PIECE;
    pos.mailbox[to] = p;

    // Incremental Zobrist
    pos.state->zobrist_key ^= (ZOBRIST_PIECES[p][from] ^ ZOBRIST_PIECES[p][to]);
    if (piece_type(p) == PT_PAWN) {
        pos.state->pawn_zobrist_key ^= (ZOBRIST_PAWNS[p][from] ^ ZOBRIST_PAWNS[p][to]);
    }

    // Incremental Evaluation
    pos.state->eval_mg += (PIECE_PST_MG[p][to] - PIECE_PST_MG[p][from]);
    pos.state->eval_eg += (PIECE_PST_EG[p][to] - PIECE_PST_EG[p][from]);
}

// ── Checkers and Pin Detection (from King Perspective) ───────────────────────
inline Bitboard attackers_to(const Position& pos, Square sq, Bitboard occupied, Color by_color) noexcept {
    if (sq >= 64) return 0;
    Bitboard pawns = pos.pieces[make_piece(by_color, PT_PAWN)];
    Bitboard knights = pos.pieces[make_piece(by_color, PT_KNIGHT)];
    Bitboard bishops_queens = (pos.pieces[make_piece(by_color, PT_BISHOP)] | pos.pieces[make_piece(by_color, PT_QUEEN)]) & DIAGONAL_MASKS[sq];
    Bitboard rooks_queens   = (pos.pieces[make_piece(by_color, PT_ROOK)]   | pos.pieces[make_piece(by_color, PT_QUEEN)]) & ORTHOGONAL_MASKS[sq];
    Bitboard kings          = pos.pieces[make_piece(by_color, PT_KING)];

    Bitboard b_att = bishops_queens ? (bishop_attacks(sq, occupied) & bishops_queens) : 0;
    Bitboard r_att = rooks_queens   ? (rook_attacks(sq, occupied) & rooks_queens) : 0;

    return (pawn_attacks(opponent_color(by_color), sq) & pawns)
         | (knight_attacks(sq) & knights)
         | b_att
         | r_att
         | (king_attacks(sq) & kings);
}

inline Bitboard attackers_to(const Position& pos, Square sq, Bitboard occupied) noexcept {
    if (sq >= 64) return 0;
    return attackers_to(pos, sq, occupied, WHITE) | attackers_to(pos, sq, occupied, BLACK);
}

// ── Fast Early-Exit Square Attacked Predicate ────────────────────────────────
inline bool is_square_attacked(const Position& pos, Square sq, Bitboard occupied, Color by_color) noexcept {
    if (sq >= 64) return false;
    // 1. Enemy Pawns (1 cycle)
    Bitboard pawns = pos.pieces[make_piece(by_color, PT_PAWN)];
    if (pawn_attacks(opponent_color(by_color), sq) & pawns) return true;

    // 2. Enemy Knights (1 cycle)
    Bitboard knights = pos.pieces[make_piece(by_color, PT_KNIGHT)];
    if (knight_attacks(sq) & knights) return true;

    // 3. Enemy King (1 cycle)
    Bitboard kings = pos.pieces[make_piece(by_color, PT_KING)];
    if (king_attacks(sq) & kings) return true;

    // 4. Enemy Bishops / Queens (1-cycle diagonal mask pre-filter)
    Bitboard bishops_queens = (pos.pieces[make_piece(by_color, PT_BISHOP)] | pos.pieces[make_piece(by_color, PT_QUEEN)]) & DIAGONAL_MASKS[sq];
    if (bishops_queens && (bishop_attacks(sq, occupied) & bishops_queens)) return true;

    // 5. Enemy Rooks / Queens (1-cycle orthogonal mask pre-filter)
    Bitboard rooks_queens = (pos.pieces[make_piece(by_color, PT_ROOK)] | pos.pieces[make_piece(by_color, PT_QUEEN)]) & ORTHOGONAL_MASKS[sq];
    if (rooks_queens && (rook_attacks(sq, occupied) & rooks_queens)) return true;

    return false;
}

inline void update_checkers(Position& pos) noexcept {
    Color us = pos.side_to_move;
    Color them = opponent_color(us);
    Square ksq = pos.king_square(us);
    if (ksq >= 64) {
        pos.state->checkers = 0;
        return;
    }
    Bitboard occ = pos.occupancy();

    Bitboard their_pawns = pos.pieces[make_piece(them, PT_PAWN)];
    Bitboard their_knights = pos.pieces[make_piece(them, PT_KNIGHT)];
    Bitboard their_bishops_queens = (pos.pieces[make_piece(them, PT_BISHOP)] | pos.pieces[make_piece(them, PT_QUEEN)]) & DIAGONAL_MASKS[ksq];
    Bitboard their_rooks_queens = (pos.pieces[make_piece(them, PT_ROOK)] | pos.pieces[make_piece(them, PT_QUEEN)]) & ORTHOGONAL_MASKS[ksq];

    pos.state->checkers = (pawn_attacks(us, ksq) & their_pawns)
                        | (knight_attacks(ksq) & their_knights);
    if (their_bishops_queens) pos.state->checkers |= (bishop_attacks(ksq, occ) & their_bishops_queens);
    if (their_rooks_queens)   pos.state->checkers |= (rook_attacks(ksq, occ) & their_rooks_queens);
}

inline void update_pins(Position& pos) noexcept {
    Color us = pos.side_to_move;
    Color them = opponent_color(us);
    Square ksq = pos.king_square(us);
    if (ksq >= 64) {
        pos.state->pinned = 0;
        return;
    }

    Bitboard candidate_pinners = ((pos.pieces[make_piece(them, PT_BISHOP)] | pos.pieces[make_piece(them, PT_QUEEN)]) & DIAGONAL_MASKS[ksq])
                               | ((pos.pieces[make_piece(them, PT_ROOK)] | pos.pieces[make_piece(them, PT_QUEEN)]) & ORTHOGONAL_MASKS[ksq]);

    if (!candidate_pinners) {
        pos.state->pinned = 0;
        return;
    }

    Bitboard occ = pos.occupancy();
    Bitboard pinned = 0;
    while (candidate_pinners) {
        Square pinner_sq = pop_lsb(candidate_pinners);
        Bitboard between = squares_between(ksq, pinner_sq) & occ;
        if (popcount(between) == 1 && (between & pos.colors[us])) {
            pinned |= between;
        }
    }
    pos.state->pinned = pinned;
}

inline void update_checkers_and_pins(Position& pos) noexcept {
    update_checkers(pos);
    update_pins(pos);
}

// ── Make Move ────────────────────────────────────────────────────────────────
inline void make_move(Position& pos, Move move, StateInfo& new_state) noexcept {
    // Copy forward persistent state fields
    new_state.castling_rights = pos.state->castling_rights;
    new_state.en_passant_square = SQ_NONE;
    new_state.halfmove_clock = pos.state->halfmove_clock + 1;
    new_state.zobrist_key = pos.state->zobrist_key ^ ZOBRIST_SIDE;
    new_state.pawn_zobrist_key = pos.state->pawn_zobrist_key;
    new_state.eval_mg = pos.state->eval_mg;
    new_state.eval_eg = pos.state->eval_eg;
    new_state.game_phase = pos.state->game_phase;
    new_state.previous = pos.state;

    // Remove previous en passant hash if it existed
    if (pos.state->en_passant_square != SQ_NONE) {
        new_state.zobrist_key ^= ZOBRIST_EN_PASSANT[square_file(pos.state->en_passant_square)];
    }

    // Link state
    pos.state = &new_state;

    Square from = move.from();
    Square to = move.to();
    uint16_t flags = move.flags();
    Color us = pos.side_to_move;
    Color them = opponent_color(us);
    Piece moving_piece = pos.mailbox[from];
    PieceType moving_pt = piece_type(moving_piece);

    new_state.move = move;
    new_state.moving_piece = moving_piece;

    // Reset halfmove clock on pawn move
    if (moving_pt == PT_PAWN) {
        new_state.halfmove_clock = 0;
    }

    // Handle Normal Capture or Promotion Capture
    if (move.is_capture() && !move.is_en_passant()) {
        Piece captured = pos.mailbox[to];
        new_state.captured_piece = captured;
        new_state.halfmove_clock = 0;
        remove_piece(pos, to);
    } else {
        new_state.captured_piece = NO_PIECE;
    }

    // Move Piece
    move_piece(pos, from, to);

    // Special Move Handling
    switch (flags) {
        case MoveFlags::DOUBLE_PAWN_PUSH: {
            Square ep_sq = static_cast<Square>((from + to) / 2);
            // Only set ep square if enemy pawn can actually capture it
            if (pawn_attacks(us, ep_sq) & pos.pieces[make_piece(them, PT_PAWN)]) {
                new_state.en_passant_square = ep_sq;
                new_state.zobrist_key ^= ZOBRIST_EN_PASSANT[square_file(ep_sq)];
            }
            break;
        }
        case MoveFlags::EN_PASSANT: {
            Square cap_sq = make_square(square_file(to), square_rank(from));
            Piece captured = pos.mailbox[cap_sq];
            new_state.captured_piece = captured;
            remove_piece(pos, cap_sq);
            break;
        }
        case MoveFlags::CASTLE_KING: {
            Square r_from = (us == WHITE) ? H1 : H8;
            Square r_to   = (us == WHITE) ? F1 : F8;
            move_piece(pos, r_from, r_to);
            break;
        }
        case MoveFlags::CASTLE_QUEEN: {
            Square r_from = (us == WHITE) ? A1 : A8;
            Square r_to   = (us == WHITE) ? D1 : D8;
            move_piece(pos, r_from, r_to);
            break;
        }
        case MoveFlags::PROMO_QUEEN:
        case MoveFlags::PROMO_CAP_QUEEN:
            remove_piece(pos, to);
            put_piece(pos, make_piece(us, PT_QUEEN), to);
            break;
        case MoveFlags::PROMO_ROOK:
        case MoveFlags::PROMO_CAP_ROOK:
            remove_piece(pos, to);
            put_piece(pos, make_piece(us, PT_ROOK), to);
            break;
        case MoveFlags::PROMO_BISHOP:
        case MoveFlags::PROMO_CAP_BISHOP:
            remove_piece(pos, to);
            put_piece(pos, make_piece(us, PT_BISHOP), to);
            break;
        case MoveFlags::PROMO_KNIGHT:
        case MoveFlags::PROMO_CAP_KNIGHT:
            remove_piece(pos, to);
            put_piece(pos, make_piece(us, PT_KNIGHT), to);
            break;
        default:
            break;
    }

    // Update Castling Rights (if King or Rook moved, or Rook captured)
    if (new_state.castling_rights != CASTLE_NONE) {
        uint8_t old_rights = new_state.castling_rights;

        if (moving_pt == PT_KING) {
            new_state.castling_rights &= (us == WHITE ? ~CASTLE_WHITE_ALL : ~CASTLE_BLACK_ALL);
        } else if (moving_pt == PT_ROOK) {
            if (from == A1) new_state.castling_rights &= ~CASTLE_WHITE_OOO;
            else if (from == H1) new_state.castling_rights &= ~CASTLE_WHITE_OO;
            else if (from == A8) new_state.castling_rights &= ~CASTLE_BLACK_OOO;
            else if (from == H8) new_state.castling_rights &= ~CASTLE_BLACK_OO;
        }

        if (to == A1) new_state.castling_rights &= ~CASTLE_WHITE_OOO;
        else if (to == H1) new_state.castling_rights &= ~CASTLE_WHITE_OO;
        else if (to == A8) new_state.castling_rights &= ~CASTLE_BLACK_OOO;
        else if (to == H8) new_state.castling_rights &= ~CASTLE_BLACK_OO;

        if (new_state.castling_rights != old_rights) {
            new_state.zobrist_key ^= ZOBRIST_CASTLING[old_rights] ^ ZOBRIST_CASTLING[new_state.castling_rights];
        }
    }

    // Switch Side to Move
    pos.side_to_move = them;
    pos.game_ply++;

    // Compute Direct Checkers for New Side (pins are computed lazily during move generation)
    update_checkers(pos);
}

// ── Undo Move ────────────────────────────────────────────────────────────────
inline void undo_move(Position& pos, Move move) noexcept {
    Square from = move.from();
    Square to = move.to();
    uint16_t flags = move.flags();

    Color them = pos.side_to_move;
    Color us = opponent_color(them);
    pos.side_to_move = us;
    pos.game_ply--;

    // Revert Promotion
    if (move.is_promotion()) {
        remove_piece(pos, to);
        put_piece(pos, make_piece(us, PT_PAWN), to);
    }

    // Move Piece Back
    move_piece(pos, to, from);

    // Restore Captured Piece
    if (pos.state->captured_piece != NO_PIECE) {
        if (move.is_en_passant()) {
            Square cap_sq = make_square(square_file(to), square_rank(from));
            put_piece(pos, pos.state->captured_piece, cap_sq);
        } else {
            put_piece(pos, pos.state->captured_piece, to);
        }
    }

    // Restore Castling Rook
    if (flags == MoveFlags::CASTLE_KING) {
        Square r_from = (us == WHITE) ? H1 : H8;
        Square r_to   = (us == WHITE) ? F1 : F8;
        move_piece(pos, r_to, r_from);
    } else if (flags == MoveFlags::CASTLE_QUEEN) {
        Square r_from = (us == WHITE) ? A1 : A8;
        Square r_to   = (us == WHITE) ? D1 : D8;
        move_piece(pos, r_to, r_from);
    }

    // Unlink State
    pos.state = pos.state->previous;
}

// ── Null Move (for Null Move Pruning) ────────────────────────────────────────
inline void make_null_move(Position& pos, StateInfo& new_state) noexcept {
    new_state.castling_rights = pos.state->castling_rights;
    new_state.en_passant_square = SQ_NONE;
    new_state.halfmove_clock = pos.state->halfmove_clock + 1;
    new_state.zobrist_key = pos.state->zobrist_key ^ ZOBRIST_SIDE;
    new_state.pawn_zobrist_key = pos.state->pawn_zobrist_key;
    new_state.eval_mg = pos.state->eval_mg;
    new_state.eval_eg = pos.state->eval_eg;
    new_state.game_phase = pos.state->game_phase;
    new_state.captured_piece = NO_PIECE;
    new_state.checkers = 0;
    new_state.pinned = 0;
    new_state.previous = pos.state;

    if (pos.state->en_passant_square != SQ_NONE) {
        new_state.zobrist_key ^= ZOBRIST_EN_PASSANT[square_file(pos.state->en_passant_square)];
    }

    pos.state = &new_state;
    pos.side_to_move = opponent_color(pos.side_to_move);
    pos.game_ply++;
}

inline void undo_null_move(Position& pos) noexcept {
    pos.side_to_move = opponent_color(pos.side_to_move);
    pos.game_ply--;
    pos.state = pos.state->previous;
}

// ── Draw Conditions ──────────────────────────────────────────────────────────
inline bool is_repetition(const Position& pos, int count = 2) noexcept {
    int repetitions = 1;
    if (!pos.state || !pos.state->previous) return false;
    StateInfo* st = pos.state->previous;
    int limit = pos.state->halfmove_clock;

    while (st && limit >= 2) {
        st = st->previous;
        if (!st) break;
        if (st->zobrist_key == pos.state->zobrist_key) {
            repetitions++;
            if (repetitions >= count) return true;
        }
        if (!st->previous) break;
        st = st->previous;
        limit -= 2;
    }
    return false;
}

inline bool is_fifty_moves(const Position& pos) noexcept {
    return pos.state->halfmove_clock >= 100;
}

inline bool is_insufficient_material(const Position& pos) noexcept {
    // If pawns, rooks, or queens exist, material is sufficient to mate
    if (pos.pieces[W_PAWN] | pos.pieces[B_PAWN] |
        pos.pieces[W_ROOK] | pos.pieces[B_ROOK] |
        pos.pieces[W_QUEEN] | pos.pieces[B_QUEEN]) {
        return false;
    }

    int w_knights = popcount(pos.pieces[W_KNIGHT]);
    int b_knights = popcount(pos.pieces[B_KNIGHT]);
    int w_bishops = popcount(pos.pieces[W_BISHOP]);
    int b_bishops = popcount(pos.pieces[B_BISHOP]);

    // KK (King vs King)
    if ((w_knights + b_knights + w_bishops + b_bishops) == 0) return true;

    // KB vs K or KN vs K
    if ((w_knights + w_bishops <= 1) && (b_knights + b_bishops == 0)) return true;
    if ((b_knights + b_bishops <= 1) && (w_knights + w_bishops == 0)) return true;

    // KB vs KB with same-colored bishops
    if (w_knights == 0 && b_knights == 0 && w_bishops == 1 && b_bishops == 1) {
        Bitboard light_squares = 0x55AA55AA55AA55AAULL;
        bool w_light = (pos.pieces[W_BISHOP] & light_squares) != 0;
        bool b_light = (pos.pieces[B_BISHOP] & light_squares) != 0;
        if (w_light == b_light) return true;
    }

    return false;
}

inline bool is_draw(const Position& pos) noexcept {
    if (pos.state->halfmove_clock >= 100) return true;
    if (pos.state->halfmove_clock >= 4 && is_repetition(pos, 2)) return true;
    if (pos.state->game_phase <= 4 && is_insufficient_material(pos)) return true;
    return false;
}

// ── FEN Parser and Generator ─────────────────────────────────────────────────
inline void clear_position(Position& pos) noexcept {
    for (int p = 0; p < 15; ++p) pos.pieces[p] = 0;
    pos.colors[WHITE] = 0;
    pos.colors[BLACK] = 0;
    for (int sq = 0; sq < 64; ++sq) pos.mailbox[sq] = NO_PIECE;
    pos.side_to_move = WHITE;
    pos.game_ply = 0;
}

inline bool set_fen(Position& pos, std::string_view fen, StateInfo& root_state) {
    init_bitboards();
    init_zobrist();
    init_pst_tables();

    clear_position(pos);
    pos.state = &root_state;
    root_state = StateInfo{};

    std::istringstream ss{std::string(fen)};
    std::string board_str, turn_str, castling_str, ep_str;
    int halfmove = 0, fullmove = 1;

    if (!(ss >> board_str >> turn_str >> castling_str >> ep_str)) {
        return false;
    }
    ss >> halfmove >> fullmove;

    // 1. Parse Board
    int rank = 7, file = 0;
    for (char c : board_str) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += (c - '0');
        } else {
            Square sq = make_square(static_cast<File>(file), static_cast<Rank>(rank));
            Piece piece = NO_PIECE;
            switch (c) {
                case 'P': piece = W_PAWN; break;
                case 'N': piece = W_KNIGHT; break;
                case 'B': piece = W_BISHOP; break;
                case 'R': piece = W_ROOK; break;
                case 'Q': piece = W_QUEEN; break;
                case 'K': piece = W_KING; break;
                case 'p': piece = B_PAWN; break;
                case 'n': piece = B_KNIGHT; break;
                case 'b': piece = B_BISHOP; break;
                case 'r': piece = B_ROOK; break;
                case 'q': piece = B_QUEEN; break;
                case 'k': piece = B_KING; break;
                default: return false;
            }
            put_piece(pos, piece, sq);
            file++;
        }
    }

    // 2. Parse Side to Move
    pos.side_to_move = (turn_str == "b") ? BLACK : WHITE;
    if (pos.side_to_move == BLACK) {
        root_state.zobrist_key ^= ZOBRIST_SIDE;
    }

    // 3. Parse Castling Rights
    root_state.castling_rights = CASTLE_NONE;
    for (char c : castling_str) {
        if (c == 'K') root_state.castling_rights |= CASTLE_WHITE_OO;
        else if (c == 'Q') root_state.castling_rights |= CASTLE_WHITE_OOO;
        else if (c == 'k') root_state.castling_rights |= CASTLE_BLACK_OO;
        else if (c == 'q') root_state.castling_rights |= CASTLE_BLACK_OOO;
    }
    root_state.zobrist_key ^= ZOBRIST_CASTLING[root_state.castling_rights];

    // 4. Parse En Passant
    if (ep_str != "-") {
        root_state.en_passant_square = square_from_name(ep_str);
        if (root_state.en_passant_square != SQ_NONE) {
            root_state.zobrist_key ^= ZOBRIST_EN_PASSANT[square_file(root_state.en_passant_square)];
        }
    } else {
        root_state.en_passant_square = SQ_NONE;
    }

    // 5. Halfmove and Fullmove
    root_state.halfmove_clock = static_cast<uint8_t>(halfmove);
    pos.game_ply = (fullmove - 1) * 2 + (pos.side_to_move == BLACK ? 1 : 0);

    // Initial check and pin detection
    update_checkers_and_pins(pos);
    return true;
}

inline std::string get_fen(const Position& pos) {
    std::string fen;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            Square sq = make_square(static_cast<File>(f), static_cast<Rank>(r));
            Piece p = pos.mailbox[sq];
            if (p == NO_PIECE) {
                empty++;
            } else {
                if (empty > 0) {
                    fen += std::to_string(empty);
                    empty = 0;
                }
                char c = '?';
                switch (p) {
                    case W_PAWN: c = 'P'; break;
                    case W_KNIGHT: c = 'N'; break;
                    case W_BISHOP: c = 'B'; break;
                    case W_ROOK: c = 'R'; break;
                    case W_QUEEN: c = 'Q'; break;
                    case W_KING: c = 'K'; break;
                    case B_PAWN: c = 'p'; break;
                    case B_KNIGHT: c = 'n'; break;
                    case B_BISHOP: c = 'b'; break;
                    case B_ROOK: c = 'r'; break;
                    case B_QUEEN: c = 'q'; break;
                    case B_KING: c = 'k'; break;
                    default: break;
                }
                fen += c;
            }
        }
        if (empty > 0) fen += std::to_string(empty);
        if (r > 0) fen += '/';
    }

    fen += (pos.side_to_move == WHITE ? " w " : " b ");

    std::string castling;
    if (pos.state->castling_rights & CASTLE_WHITE_OO) castling += 'K';
    if (pos.state->castling_rights & CASTLE_WHITE_OOO) castling += 'Q';
    if (pos.state->castling_rights & CASTLE_BLACK_OO) castling += 'k';
    if (pos.state->castling_rights & CASTLE_BLACK_OOO) castling += 'q';
    fen += castling.empty() ? "-" : castling;

    fen += ' ';
    fen += square_name(pos.state->en_passant_square);

    fen += ' ' + std::to_string(pos.state->halfmove_clock);
    int fullmove = (pos.game_ply / 2) + 1;
    fen += ' ' + std::to_string(fullmove);

    return fen;
}

} // namespace tigerfish
