#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — REINFORCEMENT LEARNING INTERFACE (newengine/rl_interface.cpp)
// Feature tensor extraction (14x8x8 planes), policy encoding, dataset exporter.
// Compatible with AlphaZero / Leela / PyTorch / ONNX training pipelines.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "position.cpp"
#include "movegen.cpp"

#include <vector>
#include <fstream>
#include <cstring>

namespace tigerfish {

// ── Tensor Dimensions ────────────────────────────────────────────────────────
constexpr int RL_NUM_PLANES  = 14;
constexpr int RL_PLANE_SIZE  = 64;
constexpr int RL_TENSOR_SIZE = RL_NUM_PLANES * RL_PLANE_SIZE; // 896 floats
constexpr int RL_ACTION_SPACE = 4096; // 64 from_sq * 64 to_sq

// ── Feature Plane Extraction (14 x 8 x 8) ────────────────────────────────────
// Planes 0..5:   Our pieces (P, N, B, R, Q, K)
// Planes 6..11:  Their pieces (P, N, B, R, Q, K)
// Plane 12:      En Passant target square
// Plane 13:      Castling rights & metadata
inline void export_position_tensor(const Position& pos, float* output_tensor) noexcept {
    std::memset(output_tensor, 0, RL_TENSOR_SIZE * sizeof(float));

    Color us = pos.side_to_move;
    Color them = opponent_color(us);

    // Planes 0..5: Our pieces
    for (int pt = 1; pt <= 6; ++pt) {
        Piece p = make_piece(us, static_cast<PieceType>(pt));
        Bitboard bb = pos.pieces[p];
        int plane_offset = (pt - 1) * 64;

        while (bb) {
            Square sq = pop_lsb(bb);
            // Flip board if Black to move so engine always learns canonical perspective
            Square canonical_sq = (us == WHITE) ? sq : mirror_square(sq);
            output_tensor[plane_offset + canonical_sq] = 1.0f;
        }
    }

    // Planes 6..11: Their pieces
    for (int pt = 1; pt <= 6; ++pt) {
        Piece p = make_piece(them, static_cast<PieceType>(pt));
        Bitboard bb = pos.pieces[p];
        int plane_offset = (6 + pt - 1) * 64;

        while (bb) {
            Square sq = pop_lsb(bb);
            Square canonical_sq = (us == WHITE) ? sq : mirror_square(sq);
            output_tensor[plane_offset + canonical_sq] = 1.0f;
        }
    }

    // Plane 12: En Passant square
    if (pos.state->en_passant_square != SQ_NONE) {
        Square canonical_ep = (us == WHITE) ? pos.state->en_passant_square : mirror_square(pos.state->en_passant_square);
        output_tensor[12 * 64 + canonical_ep] = 1.0f;
    }

    // Plane 13: Castling rights & turn flag
    uint8_t rights = pos.state->castling_rights;
    int offset13 = 13 * 64;
    if (us == WHITE) {
        if (rights & CASTLE_WHITE_OO)  output_tensor[offset13 + 0] = 1.0f;
        if (rights & CASTLE_WHITE_OOO) output_tensor[offset13 + 1] = 1.0f;
        if (rights & CASTLE_BLACK_OO)  output_tensor[offset13 + 2] = 1.0f;
        if (rights & CASTLE_BLACK_OOO) output_tensor[offset13 + 3] = 1.0f;
    } else {
        if (rights & CASTLE_BLACK_OO)  output_tensor[offset13 + 0] = 1.0f;
        if (rights & CASTLE_BLACK_OOO) output_tensor[offset13 + 1] = 1.0f;
        if (rights & CASTLE_WHITE_OO)  output_tensor[offset13 + 2] = 1.0f;
        if (rights & CASTLE_WHITE_OOO) output_tensor[offset13 + 3] = 1.0f;
    }
}

// ── Policy Action Encoding & Decoding ────────────────────────────────────────
inline int encode_action(Move m, Color perspective) noexcept {
    if (m.is_none()) return -1;
    Square from = (perspective == WHITE) ? m.from() : mirror_square(m.from());
    Square to   = (perspective == WHITE) ? m.to()   : mirror_square(m.to());
    return static_cast<int>(from) * 64 + static_cast<int>(to);
}

inline Move decode_action(int action_idx, Color perspective, const Position& pos) noexcept {
    if (action_idx < 0 || action_idx >= RL_ACTION_SPACE) return MOVE_NONE;
    Square raw_from = static_cast<Square>(action_idx / 64);
    Square raw_to   = static_cast<Square>(action_idx % 64);

    Square from = (perspective == WHITE) ? raw_from : mirror_square(raw_from);
    Square to   = (perspective == WHITE) ? raw_to   : mirror_square(raw_to);

    MoveList moves;
    generate_legal_moves(pos, moves);
    for (int i = 0; i < moves.size(); ++i) {
        if (moves[i].from() == from && moves[i].to() == to) {
            return moves[i];
        }
    }
    return MOVE_NONE;
}

// ── Self-Play Training Record Struct ─────────────────────────────────────────
#pragma pack(push, 1)
struct RLTrainingStep {
    float state_tensor[RL_TENSOR_SIZE];  // 896 input features
    uint16_t chosen_action;              // Policy target action
    float game_outcome;                  // +1.0 for Win, 0.0 for Draw, -1.0 for Loss
};
#pragma pack(pop)

// ── Self-Play Dataset Exporter ───────────────────────────────────────────────
inline bool append_training_step_to_file(const std::string& filename, const RLTrainingStep& step) {
    std::ofstream out(filename, std::ios::binary | std::ios::app);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(&step), sizeof(RLTrainingStep));
    return true;
}

} // namespace tigerfish
