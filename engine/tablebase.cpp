#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — TABLEBASE DATABASE (newengine/tablebase.cpp)
// Syzygy Endgame / Midgame Tablebase probing interface & hook architecture.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "position.cpp"
#include "movegen.cpp"
#include <string>

namespace tigerfish {

// ── Tablebase WDL (Win/Draw/Loss) Constants ──────────────────────────────────
enum WDLResult : int {
    TB_LOSS         = -2,
    TB_BLESSED_LOSS = -1,
    TB_DRAW         =  0,
    TB_CURSED_WIN   =  1,
    TB_WIN          =  2,
    TB_NONE         =  99
};

// ── Function Pointer Hooks for Syzygy / Fathom Integration ──────────────────
using TBProbeWDLHook = int (*)(const Position& pos);
using TBProbeRootHook = bool (*)(const Position& pos, Move& best_move, int& score);

inline TBProbeWDLHook ACTIVE_TB_WDL_HOOK = nullptr;
inline TBProbeRootHook ACTIVE_TB_ROOT_HOOK = nullptr;
inline std::string TABLEBASE_PATH = "";
inline int TABLEBASE_PIECE_LIMIT = 5;

inline void init_tablebases(const std::string& path, int max_pieces = 5) {
    TABLEBASE_PATH = path;
    TABLEBASE_PIECE_LIMIT = max_pieces;
    // When external syzygy/fathom C library is linked, initialize it here
}

// ── Probe Tablebase WDL ──────────────────────────────────────────────────────
inline int probe_tablebase_wdl(const Position& pos) noexcept {
    // If external Syzygy library is linked, delegate to hook
    if (ACTIVE_TB_WDL_HOOK) {
        int piece_count = popcount(pos.occupancy());
        if (piece_count <= TABLEBASE_PIECE_LIMIT) {
            return ACTIVE_TB_WDL_HOOK(pos);
        }
    }

    // Built-in trivial tablebase rules:
    int piece_count = popcount(pos.occupancy());
    if (piece_count <= 2) {
        // King vs King is always draw
        return TB_DRAW;
    }

    if (is_insufficient_material(pos)) {
        return TB_DRAW;
    }

    return TB_NONE;
}

// ── Probe Tablebase at Search Root ───────────────────────────────────────────
inline bool probe_tablebase_root(const Position& pos, Move& best_move, int& score) noexcept {
    if (ACTIVE_TB_ROOT_HOOK) {
        int piece_count = popcount(pos.occupancy());
        if (piece_count <= TABLEBASE_PIECE_LIMIT) {
            return ACTIVE_TB_ROOT_HOOK(pos, best_move, score);
        }
    }

    int wdl = probe_tablebase_wdl(pos);
    if (wdl == TB_NONE) {
        return false;
    }

    if (wdl == TB_DRAW) {
        score = 0;
    } else if (wdl > 0) {
        score = SCORE_MATE - 100;
    } else {
        score = -SCORE_MATE + 100;
    }

    MoveList moves;
    generate_legal_moves(pos, moves);
    if (moves.empty()) {
        return false;
    }

    // Default to the first move in case child positions cannot be distinguished
    best_move = moves[0];
    int best_value = -999999;
    bool found_candidate = false;

    Position child_pos = pos;
    StateInfo root_st = *pos.state;
    child_pos.state = &root_st;

    for (int i = 0; i < moves.size(); ++i) {
        Move m = moves[i];
        StateInfo child_st;
        make_move(child_pos, m, child_st);

        int child_wdl = probe_tablebase_wdl(child_pos);
        undo_move(child_pos, m);

        int move_value;
        if (child_wdl != TB_NONE) {
            // child_wdl is evaluated from opponent's perspective; invert for current side
            move_value = -child_wdl;
        } else {
            // If child WDL is unprobed, assume it neutrally matches root expectation
            move_value = (wdl > 0) ? 1 : ((wdl < 0) ? -1 : 0);
        }

        // Tactical tie-breaker: prefer captures and promotions
        int tie_breaker = 0;
        if (m.is_capture()) tie_breaker += 2;
        if (m.is_promotion()) tie_breaker += 1;

        int total_score = (move_value * 10) + tie_breaker;

        if (!found_candidate || total_score > best_value) {
            best_value = total_score;
            best_move = m;
            found_candidate = true;
        }
    }

    return found_candidate;
}

} // namespace tigerfish
