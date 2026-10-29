#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// TIGERFISH NEWENGINE — SEARCH (newengine/search.cpp)
// PVS Alpha-Beta, Iterative Deepening, Aspiration Windows, Quiescence, NMP, RFP,
// LMR, Staged Pick-Best Move Ordering (TT -> MVV-LVA -> Killers -> History), Lazy SMP.
// ══════════════════════════════════════════════════════════════════════════════

#include "types.cpp"
#include "bitboards.cpp"
#include "position.cpp"
#include "movegen.cpp"
#include "tt.cpp"
#include "eval.cpp"

#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <bit>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tigerfish {

#ifdef _WIN32
inline uint64_t get_p_core_mask() noexcept {
    static uint64_t cached_mask = 0;
    static bool checked = false;
    if (checked) return cached_mask;
    checked = true;

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length == 0) return 0;

    std::vector<char> buffer(length);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length)) {
        return 0;
    }

    DWORD offset = 0;
    uint64_t p_mask = 0;
    bool has_hybrid = false;

    while (offset < length) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
        if (info->Relationship == RelationProcessorCore) {
            // EfficiencyClass > 0 indicates high-performance P-core on Intel hybrid CPUs
            if (info->Processor.EfficiencyClass > 0 || (info->Processor.Flags & LTP_PC_SMT)) {
                p_mask |= info->Processor.GroupMask[0].Mask;
            } else {
                has_hybrid = true;
            }
        }
        offset += info->Size;
    }

    cached_mask = has_hybrid ? p_mask : 0;
    return cached_mask;
}

inline void apply_thread_affinity(int thread_idx, int total_threads) noexcept {
    (void)thread_idx;
    uint64_t p_mask = get_p_core_mask();
    if (p_mask != 0 && total_threads <= std::popcount(p_mask)) {
        SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(p_mask));
    }
}
#endif

// ── Search Limits & Constraints POD ──────────────────────────────────────────
struct SearchLimits {
    int depth = 64;
    uint64_t max_nodes = 0;
    int time_limit_ms = 0;
    int inc_ms = 0;
    bool infinite = false;
    int thread_count = 1;
    bool root_parallel = false;
    std::atomic<bool>* external_stop = nullptr;
};

// ── Search Output & Stats POD ────────────────────────────────────────────────
struct SearchResult {
    Move best_move = MOVE_NONE;
    int score = 0;
    int depth = 0;
    int seldepth = 0;
    uint64_t nodes = 0;
    int time_ms = 0;
    uint64_t nps = 0;
    uint64_t tt_hits = 0;
    uint64_t tt_misses = 0;
    std::vector<Move> pv;
};

// ── Per-Thread Search Context (Zero Heap Allocations) ────────────────────────
struct SearchContext {
    uint64_t nodes = 0;
    int seldepth = 0;
    uint64_t tt_hits = 0;
    uint64_t tt_misses = 0;

    Move killer_moves[128][2]{};
    int history_table[2][64][64]{};
    int cont_history[15][64][64]{};

    Move pv_table[128][128]{};
    int pv_length[128]{};
    int eval_history[128]{};

    std::chrono::steady_clock::time_point start_time;
    int time_limit_ms = 0;
    uint64_t node_limit = 0;
    bool stopped = false;
    std::atomic<bool>* external_stop = nullptr;

    void reset() noexcept {
        nodes = 0;
        seldepth = 0;
        tt_hits = 0;
        tt_misses = 0;
        stopped = false;
        std::memset(killer_moves, 0, sizeof(killer_moves));
        std::memset(history_table, 0, sizeof(history_table));
        std::memset(cont_history, 0, sizeof(cont_history));
        std::memset(pv_table, 0, sizeof(pv_table));
        std::memset(pv_length, 0, sizeof(pv_length));
        std::memset(eval_history, 0, sizeof(eval_history));
    }

    bool should_stop() noexcept {
        if (stopped) return true;
        if (external_stop && external_stop->load(std::memory_order_relaxed)) {
            stopped = true;
            return true;
        }
        if (node_limit > 0 && nodes >= node_limit) {
            stopped = true;
            return true;
        }
        if (time_limit_ms > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
            if (elapsed >= time_limit_ms) {
                stopped = true;
                return true;
            }
        }
        return false;
    }
};

// ── Precomputed Logarithmic LMR Table ───────────────────────────────────────
static int LMR_TABLE[64][64];

inline void init_lmr_table() noexcept {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    for (int d = 0; d < 64; ++d) {
        for (int m = 0; m < 64; ++m) {
            if (d >= 2 && m >= 2) {
                LMR_TABLE[d][m] = static_cast<int>(0.75 + std::log(d) * std::log(m) / 2.25);
            } else {
                LMR_TABLE[d][m] = 0;
            }
        }
    }
}

// ── Move Scoring & Ordering Constants ───────────────────────────────────────
constexpr int SCORE_TT_MOVE = 2000000;
constexpr int SCORE_CAPTURE_BASE = 1000000;
constexpr int SCORE_KILLER_1 = 900000;
constexpr int SCORE_KILLER_2 = 800000;
constexpr int SCORE_BAD_CAPTURE = -50000;
constexpr int MAX_QSEARCH_DEPTH = 64;

// MVV-LVA: Most Valuable Victim - Least Valuable Attacker
constexpr int MVV_LVA[7][7] = {
    // Victim: None, P, N, B, R, Q, K
    { 0,   0,   0,   0,   0,   0, 0 },
    { 0, 105, 205, 305, 405, 505, 0 }, // Attacker: P
    { 0, 104, 204, 304, 404, 504, 0 }, // Attacker: N
    { 0, 103, 203, 303, 403, 503, 0 }, // Attacker: B
    { 0, 102, 202, 302, 402, 502, 0 }, // Attacker: R
    { 0, 101, 201, 301, 401, 501, 0 }, // Attacker: Q
    { 0, 100, 200, 300, 400, 500, 0 }  // Attacker: K
};

constexpr int SEE_PIECE_VALUES[8] = { 0, 100, 320, 330, 500, 900, 20000, 0 };

// ── Static Exchange Evaluation (SEE) ─────────────────────────────────────────
inline int see(const Position& pos, Move m) noexcept {
    PROFILE_ZONE("SEE");
    Square from = m.from();
    Square to = m.to();
    Piece moving_piece = pos.mailbox[from];
    Piece victim = m.is_en_passant() ? make_piece(opponent_color(pos.side_to_move), PT_PAWN) : pos.mailbox[to];

    PieceType victim_pt = piece_type(victim);
    PieceType moving_pt = piece_type(moving_piece);
    if (victim_pt == PT_NONE) return 0;

    int gain[32];
    int depth = 0;
    gain[0] = SEE_PIECE_VALUES[victim_pt];

    Bitboard occ = pos.occupancy() ^ (1ULL << from);
    Color side = opponent_color(pos.side_to_move);
    PieceType current_attacker_pt = moving_pt;

    while (depth < 31) {
        // Find least valuable attacker of `side` to square `to`
        Square att_sq = SQ_NONE;
        PieceType next_pt = PT_NONE;

        // 1. Pawn
        Bitboard pawns = pawn_attacks(opponent_color(side), to) & pos.pieces[make_piece(side, PT_PAWN)] & occ;
        if (pawns) {
            att_sq = lsb(pawns);
            next_pt = PT_PAWN;
        } else {
            // 2. Knight
            Bitboard knights = knight_attacks(to) & pos.pieces[make_piece(side, PT_KNIGHT)] & occ;
            if (knights) {
                att_sq = lsb(knights);
                next_pt = PT_KNIGHT;
            } else {
                // 3. Bishop
                Bitboard b_pieces = pos.pieces[make_piece(side, PT_BISHOP)] & occ;
                if (b_pieces) {
                    Bitboard bishops = bishop_attacks(to, occ) & b_pieces;
                    if (bishops) {
                        att_sq = lsb(bishops);
                        next_pt = PT_BISHOP;
                    }
                }
                if (att_sq == SQ_NONE) {
                    // 4. Rook
                    Bitboard r_pieces = pos.pieces[make_piece(side, PT_ROOK)] & occ;
                    if (r_pieces) {
                        Bitboard rooks = rook_attacks(to, occ) & r_pieces;
                        if (rooks) {
                            att_sq = lsb(rooks);
                            next_pt = PT_ROOK;
                        }
                    }
                }
                if (att_sq == SQ_NONE) {
                    // 5. Queen
                    Bitboard q_pieces = pos.pieces[make_piece(side, PT_QUEEN)] & occ;
                    if (q_pieces) {
                        Bitboard queens = (bishop_attacks(to, occ) | rook_attacks(to, occ)) & q_pieces;
                        if (queens) {
                            att_sq = lsb(queens);
                            next_pt = PT_QUEEN;
                        }
                    }
                }
                if (att_sq == SQ_NONE) {
                    // 6. King
                    Bitboard kings = king_attacks(to) & pos.pieces[make_piece(side, PT_KING)] & occ;
                    if (kings) {
                        att_sq = lsb(kings);
                        next_pt = PT_KING;
                    }
                }
            }
        }

        if (att_sq == SQ_NONE) break;

        depth++;
        gain[depth] = SEE_PIECE_VALUES[current_attacker_pt] - gain[depth - 1];
        if (std::max(-gain[depth - 1], gain[depth]) < 0) break;

        occ ^= (1ULL << att_sq);
        current_attacker_pt = next_pt;
        side = opponent_color(side);
    }

    // Negamax propagation
    while (--depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
    }

    return gain[0];
}


// ── Move Scoring and Ordering ────────────────────────────────────────────────
inline void update_history_score(int& val, int bonus) noexcept {
    val += bonus - val * std::abs(bonus) / 16384;
}

inline void update_quiet_history(SearchContext& ctx, const Position& pos, Move best_quiet,
                                 const Move* quiets_searched, int num_quiets, int depth) noexcept {
    int bonus = std::min(300, depth * depth);
    Color us = pos.side_to_move;

    Piece prev_pc = (pos.state && pos.state->previous) ? pos.state->moving_piece : NO_PIECE;
    Square prev_to = (pos.state && pos.state->previous) ? pos.state->move.to() : SQ_NONE;

    // 1. Reward the cutoff move
    update_history_score(ctx.history_table[us][best_quiet.from()][best_quiet.to()], bonus);
    if (prev_pc != NO_PIECE && prev_to != SQ_NONE && prev_pc < 15) {
        update_history_score(ctx.cont_history[prev_pc][prev_to][best_quiet.to()], bonus);
    }

    // 2. Penalize preceding quiet moves that failed low
    for (int i = 0; i < num_quiets; ++i) {
        Move q = quiets_searched[i];
        if (q == best_quiet) continue;
        update_history_score(ctx.history_table[us][q.from()][q.to()], -bonus);
        if (prev_pc != NO_PIECE && prev_to != SQ_NONE && prev_pc < 15) {
            update_history_score(ctx.cont_history[prev_pc][prev_to][q.to()], -bonus);
        }
    }
}

inline int score_move(const Position& pos, Move m, Move tt_move, int ply, const SearchContext& ctx) noexcept {
    if (m == tt_move) return SCORE_TT_MOVE;

    if (m.is_capture()) {
        Piece moving_piece = pos.mailbox[m.from()];
        PieceType moving_pt = piece_type(moving_piece);
        PieceType victim_pt = m.is_en_passant() ? PT_PAWN : piece_type(pos.mailbox[m.to()]);
        return SCORE_CAPTURE_BASE + MVV_LVA[moving_pt][victim_pt];
    }

    if (m == ctx.killer_moves[ply][0]) return SCORE_KILLER_1;
    if (m == ctx.killer_moves[ply][1]) return SCORE_KILLER_2;

    Color us = pos.side_to_move;
    int score = ctx.history_table[us][m.from()][m.to()];
    if (pos.state && pos.state->previous && pos.state->moving_piece != NO_PIECE) {
        Piece prev_pc = pos.state->moving_piece;
        Square prev_to = pos.state->move.to();
        if (prev_pc < 15 && prev_to != SQ_NONE) {
            score += ctx.cont_history[prev_pc][prev_to][m.to()];
        }
    }
    return score;
}

// ── Fast Move Pseudo-Legality Checker ────────────────────────────────────────
inline bool is_pseudo_legal(const Position& pos, Move m) noexcept {
    if (m.is_none()) return false;
    Square from = m.from();
    Square to = m.to();
    Piece pc = pos.mailbox[from];
    if (pc == NO_PIECE) return false;

    Color us = pos.side_to_move;
    if (piece_color(pc) != us) return false;

    Piece dest_pc = pos.mailbox[to];
    if (dest_pc != NO_PIECE && piece_color(dest_pc) == us) return false;

    PieceType pt = piece_type(pc);
    Bitboard occ = pos.occupancy();

    switch (pt) {
        case PT_PAWN: {
            Color them = opponent_color(us);
            int up = (us == WHITE) ? 8 : -8;
            Rank start_rank = (us == WHITE) ? RANK_2 : RANK_7;
            Rank promo_rank = (us == WHITE) ? RANK_8 : RANK_1;

            if (m.is_capture()) {
                if (m.is_en_passant()) {
                    return to == pos.state->en_passant_square &&
                           (pawn_attacks(us, from) & (1ULL << to));
                }
                if (dest_pc == NO_PIECE || piece_color(dest_pc) != them) return false;
                if (!(pawn_attacks(us, from) & (1ULL << to))) return false;
            } else {
                if (dest_pc != NO_PIECE) return false;
                if (to == from + up) {
                    // Single push
                } else if (square_rank(from) == start_rank && to == from + 2 * up) {
                    if (pos.mailbox[from + up] != NO_PIECE) return false;
                } else {
                    return false;
                }
            }
            if (square_rank(to) == promo_rank) {
                return m.is_promotion();
            }
            return !m.is_promotion();
        }
        case PT_KNIGHT:
            return (knight_attacks(from) & (1ULL << to)) != 0;
        case PT_BISHOP:
            return (bishop_attacks(from, occ) & (1ULL << to)) != 0;
        case PT_ROOK:
            return (rook_attacks(from, occ) & (1ULL << to)) != 0;
        case PT_QUEEN:
            return ((bishop_attacks(from, occ) | rook_attacks(from, occ)) & (1ULL << to)) != 0;
        case PT_KING: {
            if (m.is_castling()) return false;
            return (king_attacks(from) & (1ULL << to)) != 0;
        }
        default:
            return false;
    }
}

// ── Validated TT Move Guard (Protects against torn reads in multi-threading) ──
inline bool is_valid_tt_move(const Position& pos, Move m) noexcept {
    if (m.is_none()) return false;
    Square from = m.from();
    Square to = m.to();
    if (from >= 64 || to >= 64 || from == to) return false;
    Piece pc = pos.mailbox[from];
    if (pc == NO_PIECE || piece_color(pc) != pos.side_to_move) return false;

    // King move distance guard
    if (piece_type(pc) == PT_KING) {
        int df = std::abs(square_file(from) - square_file(to));
        int dr = std::abs(square_rank(from) - square_rank(to));
        if (df > 2 || dr > 1) return false;
    }
    return is_pseudo_legal(pos, m);
}

// ── Selection Sort (Pick Next Best Move on Demand) ───────────────────────────
inline void pick_next_best_move(MoveList& moves, int move_scores[], int current_idx) noexcept {
    int best_idx = current_idx;
    int best_score = move_scores[current_idx];
    const int sz = moves.size();

    for (int i = current_idx + 1; i < sz; ++i) {
        if (move_scores[i] > best_score) {
            best_score = move_scores[i];
            best_idx = i;
        }
    }

    if (best_idx != current_idx) {
        std::swap(moves[current_idx], moves[best_idx]);
        std::swap(move_scores[current_idx], move_scores[best_idx]);
    }
}

// ── Quiescence Search (Tactical Horizon Resolution) ──────────────────────────
inline int quiescence_search(Position& pos, int alpha, int beta, int ply, int qply,
                             SearchContext& ctx, TranspositionTable& tt) noexcept {
    PROFILE_ZONE("Quiescence");
    if (ctx.stopped) return 0;
    if ((ctx.nodes & 4095) == 0 && ctx.should_stop()) return 0;
    if (ply >= 120) return evaluate(pos);
    ctx.nodes++;
    if (ply > ctx.seldepth) ctx.seldepth = ply;
    ctx.pv_length[ply] = 0;

    // Check for draw (repetitions are impossible on pure capture sequences)
    if (ply > 0 && pos.state->halfmove_clock >= 100) return 0;

    int original_alpha = alpha;

    // Transposition Table Probe in Quiescence (only at qply == 0 to avoid cacheline thrashing)
    uint64_t key = pos.state->zobrist_key;
    TTEntry tt_entry;
    bool tt_hit = false;
    if (qply == 0) {
        prefetch_tt(tt, key);
        tt_hit = probe_tt(tt, key, tt_entry);
        if (tt_hit) {
            ctx.tt_hits++;
            int tt_score = score_from_tt(tt_entry.score, ply);
            if (tt_entry.bound() == BOUND_EXACT) return tt_score;
            if (tt_entry.bound() == BOUND_LOWER && tt_score >= beta) return tt_score;
            if (tt_entry.bound() == BOUND_UPPER && tt_score <= alpha) return tt_score;
        } else {
            ctx.tt_misses++;
        }
    }

    // Stand-Pat evaluation (reuse TT eval if available)
    int stand_pat = tt_hit ? tt_entry.eval : evaluate(pos);
    if (stand_pat >= beta) {
        if (qply == 0) store_tt(tt, key, MOVE_NONE, beta, stand_pat, 0, BOUND_LOWER, ply);
        return beta;
    }
    if (stand_pat > alpha) alpha = stand_pat;

    // Hard cutoff for quiescence depth: prevent unbounded capture explosion
    if (qply >= MAX_QSEARCH_DEPTH) return stand_pat;

    // Dynamic Delta Pruning: Determine the highest value piece the opponent has
    Color them = opponent_color(pos.side_to_move);
    int max_gain = 100;
    if (pos.pieces[make_piece(them, PT_QUEEN)]) {
        max_gain = 900;
    } else if (pos.pieces[make_piece(them, PT_ROOK)]) {
        max_gain = 500;
    } else if (pos.pieces[make_piece(them, PT_BISHOP)] || pos.pieces[make_piece(them, PT_KNIGHT)]) {
        max_gain = 330;
    }

    // Account for potential queen promotions
    Color us = pos.side_to_move;
    Bitboard promo_pawns = (us == WHITE) ? (pos.pieces[W_PAWN] & 0x00FF000000000000ULL)
                                         : (pos.pieces[B_PAWN] & 0x000000000000FF00ULL);
    if (promo_pawns) max_gain = std::max(max_gain, 900);

    // If even capturing the highest-value enemy piece (or promoting) cannot raise alpha, skip
    if (stand_pat < alpha - max_gain - 200) return alpha;

    // Generate Captures Only
    MoveList captures;
    generate_captures(pos, captures);
    if (captures.empty()) return alpha;

    int scores[256];
    for (int i = 0; i < captures.size(); ++i) {
        Move m = captures[i];
        PieceType att = piece_type(pos.mailbox[m.from()]);
        PieceType vic = m.is_en_passant() ? PT_PAWN : piece_type(pos.mailbox[m.to()]);
        if (stand_pat + SEE_PIECE_VALUES[vic] + 200 < alpha && !m.is_promotion()) {
            scores[i] = -1;
        } else {
            scores[i] = MVV_LVA[att][vic];
        }
    }

    Move best_move = MOVE_NONE;
    int best_score = stand_pat;
    StateInfo state;

    for (int i = 0; i < captures.size(); ++i) {
        pick_next_best_move(captures, scores, i);
        if (scores[i] < 0) break; // All remaining captures are delta-pruned
        Move m = captures[i];

        PieceType attacker_pt = piece_type(pos.mailbox[m.from()]);
        PieceType victim_pt = m.is_en_passant() ? PT_PAWN : piece_type(pos.mailbox[m.to()]);

        // Guarded SEE Pruning: prune losing captures (SEE < -50 or SEE < 0 when lower value victim)
        if (!m.is_promotion()) {
            if (see(pos, m) < -50) continue;
            if (SEE_PIECE_VALUES[victim_pt] < SEE_PIECE_VALUES[attacker_pt] && see(pos, m) < 0) continue;
        }

        make_move(pos, m, state);
        int score = -quiescence_search(pos, -beta, -alpha, ply + 1, qply + 1, ctx, tt);
        undo_move(pos, m);

        if (score > best_score) {
            best_score = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) break;
            }
        }
    }

    if (qply == 0) {
        uint8_t qbound = (best_score <= original_alpha) ? BOUND_UPPER :
                         (best_score >= beta) ? BOUND_LOWER : BOUND_EXACT;
        store_tt(tt, key, best_move, best_score, stand_pat, 0, qbound, ply);
    }
    return best_score;
}

// ── Principal Variation Search (PVS Alpha-Beta) ──────────────────────────────
inline int alpha_beta(Position& pos, int alpha, int beta, int depth, int ply,
                      SearchContext& ctx, TranspositionTable& tt, bool can_null = true,
                      Move excluded_move = MOVE_NONE) noexcept {
    PROFILE_ZONE("AlphaBeta");
    if (ctx.stopped) return 0;
    if ((ctx.nodes & 4095) == 0 && ctx.should_stop()) return 0;
    if (ply >= 120) return evaluate(pos);
    ctx.pv_length[ply] = 0;

    // 1. Quiescence Horizon
    if (depth <= 0) {
        return quiescence_search(pos, alpha, beta, ply, 0, ctx, tt);
    }

    ctx.nodes++;
    if (ply > ctx.seldepth) ctx.seldepth = ply;

    bool is_root = (ply == 0);
    bool in_chk = (pos.state->checkers != 0);
    bool pv_node = (beta - alpha > 1);
    int original_alpha = alpha;

    // 2. Draw Detection (Repetitions, 50-move rule)
    if (!is_root && is_draw(pos)) return 0;

    // 3. Mate Distance Pruning
    if (!is_root) {
        alpha = std::max(alpha, -SCORE_MATE + ply);
        beta  = std::min(beta,   SCORE_MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    // 4. Transposition Table Probe (bypassed at shallow leaf depth < 3 unless at root)
    uint64_t key = pos.state->zobrist_key;
    TTEntry tt_entry;
    bool tt_hit = false;
    Move tt_move = MOVE_NONE;
    int tt_score = 0;

    if (depth >= 3 || is_root) {
        prefetch_tt(tt, key);
        tt_hit = probe_tt(tt, key, tt_entry);
        if (tt_hit) {
            ctx.tt_hits++;
            tt_move = tt_entry.move;
            tt_score = score_from_tt(tt_entry.score, ply);

            if (!is_root && excluded_move.is_none() && tt_entry.depth >= depth) {
                if (tt_entry.bound() == BOUND_EXACT) return tt_score;
                if (tt_entry.bound() == BOUND_LOWER && tt_score >= beta) return tt_score;
                if (tt_entry.bound() == BOUND_UPPER && tt_score <= alpha) return tt_score;
            }
        } else {
            ctx.tt_misses++;
        }
    }

    // 5. Static Evaluation (reuse TT eval if available to skip evaluate(pos))
    int static_eval = 0;
    if (in_chk) {
        static_eval = -SCORE_MATE;
    } else if (tt_hit) {
        static_eval = tt_entry.eval;
    } else {
        static_eval = evaluate(pos);
    }

    if (ply < 128) ctx.eval_history[ply] = static_eval;
    bool improving = (ply >= 2 && static_eval >= ctx.eval_history[ply - 2]);

    // 6. Reverse Futility Pruning (Static Null Move Pruning, depth <= 8)
    if (!is_root && !in_chk && !pv_node && depth <= 8 && (beta < SCORE_MATE_BOUND) && excluded_move.is_none()) {
        int rfp_margin = (improving ? 65 : 85) * depth;
        if (static_eval - rfp_margin >= beta) {
            int rfp_score = (2 * beta + static_eval) / 3;
            store_tt(tt, key, MOVE_NONE, rfp_score, static_eval, depth, BOUND_LOWER, ply);
            return rfp_score;
        }
    }

    // 7. Dynamic Adaptive Null Move Pruning (NMP)
    // Avoid if in check, at root, in PV nodes, or in pure pawn endgames (zugzwang risk)
    if (can_null && !in_chk && !is_root && !pv_node && depth >= 3 && static_eval >= beta && (beta < SCORE_MATE_BOUND) && excluded_move.is_none()) {
        Bitboard non_pawns = pos.colors[pos.side_to_move] & ~pos.pieces[make_piece(pos.side_to_move, PT_PAWN)] & ~pos.pieces[make_piece(pos.side_to_move, PT_KING)];
        if (non_pawns) {
            PROFILE_ZONE("NullMovePruning");
            int reduction = 4 + depth / 4 + std::min(3, (static_eval - beta) / 200);
            StateInfo null_state;
            make_null_move(pos, null_state);
            int null_score = -alpha_beta(pos, -beta, -beta + 1, depth - 1 - reduction, ply + 1, ctx, tt, false);
            undo_null_move(pos);

            if (null_score >= beta) {
                int res_score = (null_score >= SCORE_MATE_BOUND) ? beta : null_score;
                store_tt(tt, key, MOVE_NONE, res_score, static_eval, depth - reduction, BOUND_LOWER, ply);
                return res_score;
            }
        }
    }

    // 8. ProbCut (Cut subtree if winning capture exceeds high beta threshold)
    if (can_null && !in_chk && !is_root && !pv_node && depth >= 5 && static_eval >= beta + 150 && (beta < SCORE_MATE_BOUND) && excluded_move.is_none()) {
        PROFILE_ZONE("ProbCut");
        int probcut_beta = beta + 200 - (improving ? 50 : 0);
        MoveList captures;
        generate_captures(pos, captures);
        for (int i = 0; i < captures.size(); ++i) {
            Move m = captures[i];
            if (see(pos, m) < 0) continue;
            StateInfo pc_state;
            make_move(pos, m, pc_state);
            int pc_val = -quiescence_search(pos, -probcut_beta, -probcut_beta + 1, ply + 1, 0, ctx, tt);
            if (pc_val >= probcut_beta) {
                pc_val = -alpha_beta(pos, -probcut_beta, -probcut_beta + 1, depth - 4, ply + 1, ctx, tt, false);
            }
            undo_move(pos, m);
            if (pc_val >= probcut_beta) {
                store_tt(tt, key, m, probcut_beta, static_eval, depth - 3, BOUND_LOWER, ply);
                return probcut_beta;
            }
        }
    }

    // 9. Razor Pruning (drop directly into quiescence when static eval is hopeless)
    if (!is_root && !in_chk && !pv_node && depth <= 2 && (static_eval + 300 * depth <= alpha)) {
        int rscore = quiescence_search(pos, alpha, beta, ply, 0, ctx, tt);
        if (rscore <= alpha) return rscore;
    }

    // 10. Check Extension
    int extension = 0;
    if (in_chk && ply < 100) {
        extension = 1;
    }

    // 11. Singular Extension (SE)
    if (!is_root && depth >= 8 && tt_hit && excluded_move.is_none() &&
        tt_entry.depth >= depth - 3 && (tt_entry.bound() & BOUND_LOWER) &&
        std::abs(tt_score) < SCORE_MATE_BOUND) {
        PROFILE_ZONE("SingularExtension");

        int singular_beta = tt_score - 2 * depth;
        int singular_depth = (depth - 1) / 2;

        int singular_score = alpha_beta(pos, singular_beta - 1, singular_beta, singular_depth, ply, ctx, tt, false, tt_move);

        if (singular_score < singular_beta) {
            extension = 1;
            if (!pv_node && singular_score < singular_beta - 40) {
                extension = 2; // Double extension for decisively singular line
            }
        } else if (singular_beta >= beta) {
            return singular_beta; // Multi-cut
        }
    }

    // 12. Internal Iterative Reduction (IIR)
    if (!pv_node && depth >= 4 && tt_move.is_none()) {
        depth--;
    }

    Move best_move = MOVE_NONE;
    int best_score = -SCORE_INFINITY;
    StateInfo state;
    int moves_searched = 0;
    bool tt_searched = false;

    // Fast-path: try TT move first without generating or scoring any other moves!
    if (is_valid_tt_move(pos, tt_move) && tt_move != excluded_move) {
        make_move(pos, tt_move, state);

        Color us = opponent_color(pos.side_to_move);
        Square ksq = pos.king_square(us);
        bool in_check = is_square_attacked(pos, ksq, pos.occupancy(), pos.side_to_move);

        if (!in_check) {
            moves_searched = 1;
            int next_depth = depth - 1 + extension;
            int score = -alpha_beta(pos, -beta, -alpha, next_depth, ply + 1, ctx, tt);
            undo_move(pos, tt_move);

            if (ctx.should_stop()) return 0;

            if (score > best_score) {
                best_score = score;
                best_move = tt_move;

                if (score > alpha) {
                    alpha = score;

                    // Immediate Beta Cutoff! ZERO other moves were generated or scored!
                    if (score >= beta) {
                        if (!tt_move.is_capture() && !tt_move.is_promotion()) {
                            if (ctx.killer_moves[ply][0] != tt_move) {
                                ctx.killer_moves[ply][1] = ctx.killer_moves[ply][0];
                                ctx.killer_moves[ply][0] = tt_move;
                            }
                            update_quiet_history(ctx, pos, tt_move, &tt_move, 1, depth);
                        }
                        if (excluded_move.is_none()) {
                            store_tt(tt, key, best_move, best_score, static_eval, depth, BOUND_LOWER, ply);
                        }
                        return best_score;
                    }

                    // Update PV table only for PV nodes
                    if (pv_node && ply < 127) {
                        ctx.pv_table[ply][0] = tt_move;
                        int copy_len = std::min(ctx.pv_length[ply + 1], 126);
                        for (int j = 0; j < copy_len; ++j) {
                            ctx.pv_table[ply][j + 1] = ctx.pv_table[ply + 1][j];
                        }
                        ctx.pv_length[ply] = copy_len + 1;
                    }
                }
            }
            tt_searched = true;
        } else {
            undo_move(pos, tt_move);
        }
    }

    // 13. Generate Legal Moves only if TT move did not cause immediate beta cutoff
    MoveList move_list;
    generate_legal_moves(pos, move_list);

    // Checkmate or Stalemate
    if (move_list.empty()) {
        if (in_chk) return -SCORE_MATE + ply; // Checkmate
        return 0;                             // Stalemate
    }

    // 14. Futility Pruning Flag (Depth 1-3)
    bool futility_pruning = false;
    if (!pv_node && !in_chk && depth <= 3 && std::abs(alpha) < SCORE_MATE_BOUND) {
        int fp_margin = 150 * depth;
        if (static_eval + fp_margin <= alpha) {
            futility_pruning = true;
        }
    }

    int move_scores[256];
    {
        PROFILE_ZONE("ScoreMoves");
        for (int i = 0; i < move_list.size(); ++i) {
            if (tt_searched && move_list[i] == tt_move) {
                move_scores[i] = -999999;
            } else {
                move_scores[i] = score_move(pos, move_list[i], MOVE_NONE, ply, ctx);
            }
        }
    }

    Move quiet_moves_searched[64];
    int quiet_count = 0;

    for (int i = 0; i < move_list.size(); ++i) {
        pick_next_best_move(move_list, move_scores, i);
        Move m = move_list[i];

        // Skip TT move if it was already searched, or skip excluded move
        if ((tt_searched && m == tt_move) || m == excluded_move) {
            continue;
        }
        bool is_quiet = !m.is_capture() && !m.is_promotion();

        if (is_quiet && quiet_count < 64) {
            quiet_moves_searched[quiet_count++] = m;
        }

        // Futility Pruning: skip quiet moves at low depth when far behind
        if (futility_pruning && moves_searched > 0 && is_quiet) {
            continue;
        }

        // Late Move Pruning (LMP): skip late quiet moves up to depth 4 (scaled by improving)
        if (!pv_node && !in_chk && depth <= 4 && is_quiet) {
            int lmp_threshold = (3 + depth * depth) / (2 - (improving ? 1 : 0));
            if (moves_searched > lmp_threshold) {
                continue;
            }
        }

        // History Pruning: skip quiet moves with very bad history at shallow depths
        if (!pv_node && !in_chk && depth <= 3 && is_quiet && moves_searched > 0) {
            Color us_side = pos.side_to_move;
            if (ctx.history_table[us_side][m.from()][m.to()] < -2048 * depth) {
                continue;
            }
        }

        // Quiet Negative SEE Pruning: prune quiet moves that hang pieces
        if (!pv_node && !in_chk && depth <= 4 && is_quiet && moves_searched > 0) {
            if (see(pos, m) < -25 * depth) {
                continue;
            }
        }

        // Capture Futility Pruning: prune captures that cannot realistically reach alpha
        if (!pv_node && !in_chk && depth <= 6 && m.is_capture() && moves_searched > 0 && !m.is_promotion()) {
            Piece victim = m.is_en_passant() ? make_piece(opponent_color(pos.side_to_move), PT_PAWN) : pos.mailbox[m.to()];
            if (static_eval + SEE_PIECE_VALUES[piece_type(victim)] + 200 + 100 * depth <= alpha) {
                continue;
            }
        }

        // SEE Pruning: prune losing captures at shallow depths (depth <= 4)
        if (!pv_node && depth <= 4 && m.is_capture() && moves_searched > 0 && !m.is_promotion()) {
            Piece moving_piece = pos.mailbox[m.from()];
            Piece victim = m.is_en_passant() ? make_piece(opponent_color(pos.side_to_move), PT_PAWN) : pos.mailbox[m.to()];
            if (SEE_PIECE_VALUES[piece_type(victim)] < SEE_PIECE_VALUES[piece_type(moving_piece)]) {
                if (see(pos, m) < -40 * depth) {
                    continue;
                }
            }
        }

        make_move(pos, m, state);
        moves_searched++;

        int score = 0;
        int next_depth = depth - 1 + (m == tt_move ? extension : 0);

        // Principal Variation Search (PVS) with Logarithmic LMR Table
        bool do_full_search = true;
        if (moves_searched >= (pv_node ? 5 : 3) && depth >= 3 && is_quiet && !in_chk) {
            int d_idx = std::min(depth, 63);
            int m_idx = std::min(moves_searched, 63);
            int reduction = LMR_TABLE[d_idx][m_idx];

            Color us = opponent_color(pos.side_to_move);
            int hist = ctx.history_table[us][m.from()][m.to()];
            if (hist > 2000) reduction--;
            else if (hist < -1000) reduction++;

            // Improving heuristic: reduce more when improving, less when not improving
            if (improving) reduction++;
            else reduction--;

            reduction = std::clamp(reduction, 1, std::max(1, next_depth - 1));

            {
                PROFILE_ZONE("LMR_ReducedSearch");
                score = -alpha_beta(pos, -alpha - 1, -alpha, next_depth - reduction, ply + 1, ctx, tt);
            }
            do_full_search = (score > alpha);
        }

        if (do_full_search) {
            if (moves_searched == 1 || !pv_node) {
                score = -alpha_beta(pos, -beta, -alpha, next_depth, ply + 1, ctx, tt);
            } else {
                score = -alpha_beta(pos, -alpha - 1, -alpha, next_depth, ply + 1, ctx, tt);
                if (score > alpha && score < beta) {
                    PROFILE_ZONE("PVS_FullResearch");
                    score = -alpha_beta(pos, -beta, -alpha, next_depth, ply + 1, ctx, tt);
                }
            }
        }

        undo_move(pos, m);

        if (ctx.should_stop()) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = m;

            if (score > alpha) {
                alpha = score;

                // Beta Cutoff (Fail-High)
                if (score >= beta) {
                    // Update Quiet Move Heuristics (Killers & History Gravity with Malus)
                    if (is_quiet) {
                        if (ctx.killer_moves[ply][0] != m) {
                            ctx.killer_moves[ply][1] = ctx.killer_moves[ply][0];
                            ctx.killer_moves[ply][0] = m;
                        }
                        update_quiet_history(ctx, pos, m, quiet_moves_searched, quiet_count, depth);
                    }
                    break;
                }

                // Update PV table only for PV nodes
                if (pv_node && ply < 127) {
                    ctx.pv_table[ply][0] = m;
                    int copy_len = std::min(ctx.pv_length[ply + 1], 126);
                    for (int j = 0; j < copy_len; ++j) {
                        ctx.pv_table[ply][j + 1] = ctx.pv_table[ply + 1][j];
                    }
                    ctx.pv_length[ply] = copy_len + 1;
                }
            }
        }
    }

    // Store position in Transposition Table with exact bound (skip if excluded_move)
    if (excluded_move.is_none()) {
        uint8_t bound = (best_score <= original_alpha) ? BOUND_UPPER :
                        (best_score >= beta) ? BOUND_LOWER : BOUND_EXACT;
        store_tt(tt, key, best_move, best_score, static_eval, depth, bound, ply);
    }
    return best_score;
}

// ── Persistent Condition-Variable Worker Pool for Lazy SMP ───────────────────
class WorkerPool {
public:
    struct Worker {
        std::thread handle;
        std::mutex mtx;
        std::condition_variable cv_start;
        std::condition_variable cv_done;
        bool has_work = false;
        bool terminate = false;
        bool is_busy = false;

        int thread_id = 0;
        int total_threads = 1;
        int max_depth = 0;
        int time_limit_ms = 0;
        std::atomic<bool>* helper_stop = nullptr;
        const std::atomic<bool>* external_stop = nullptr;
        Position root_pos;
        StateInfo root_state;
        TranspositionTable* tt = nullptr;
        std::chrono::steady_clock::time_point start_time;
        SearchContext ctx;
    };

    std::vector<std::unique_ptr<Worker>> workers;

    WorkerPool() = default;

    ~WorkerPool() {
        stop_and_join();
    }

    void stop_and_join() {
        for (auto& w : workers) {
            if (!w) continue;
            {
                std::lock_guard<std::mutex> lock(w->mtx);
                w->terminate = true;
                w->has_work = true;
            }
            w->cv_start.notify_one();
            if (w->handle.joinable()) {
                w->handle.join();
            }
        }
        workers.clear();
    }

    void resize(int target_workers) {
        if ((int)workers.size() == target_workers) return;
        stop_and_join();
        workers.reserve(target_workers);
        for (int i = 0; i < target_workers; ++i) {
            auto w = std::make_unique<Worker>();
            w->thread_id = i + 1;
            w->handle = std::thread([worker = w.get()]() {
                while (true) {
                    std::unique_lock<std::mutex> lock(worker->mtx);
                    worker->cv_start.wait(lock, [&]() { return worker->has_work; });
                    if (worker->terminate) break;

                    worker->has_work = false;
                    worker->is_busy = true;

                    Position thread_pos = worker->root_pos;
                    StateInfo thread_state = worker->root_state;
                    thread_state.previous = nullptr;
                    thread_pos.state = &thread_state;
                    TranspositionTable& tt = *worker->tt;
                    int t = worker->thread_id - 1;
                    int num_threads = worker->total_threads;
                    int max_depth = worker->max_depth;
                    std::atomic<bool>* helper_stop = worker->helper_stop;
                    const std::atomic<bool>* external_stop = worker->external_stop;
                    auto start_time = worker->start_time;
                    int time_limit = worker->time_limit_ms;

                    lock.unlock();

#ifdef _WIN32
                    apply_thread_affinity(t + 1, num_threads);
#endif

                    SearchContext& hctx = worker->ctx;
                    hctx.reset();
                    hctx.start_time = start_time;
                    hctx.time_limit_ms = time_limit;
                    hctx.external_stop = helper_stop;

                    int start_d = 1 + (t % 2);
                    int hbest_score = 0;

                    for (int d = start_d; d <= max_depth; ++d) {
                        if (helper_stop->load(std::memory_order_relaxed)) break;
                        if (external_stop && external_stop->load(std::memory_order_relaxed)) break;

                        int delta = 30;
                        int alpha = -SCORE_INFINITY;
                        int beta  =  SCORE_INFINITY;

                        if (d >= 4 && hbest_score != 0) {
                            alpha = std::max(-SCORE_INFINITY, hbest_score - delta);
                            beta  = std::min(SCORE_INFINITY,  hbest_score + delta);
                        }

                        int hscore = 0;
                        while (true) {
                            hscore = alpha_beta(thread_pos, alpha, beta, d, 0, hctx, tt);
                            if (hctx.should_stop() || helper_stop->load(std::memory_order_relaxed)) break;

                            if (hscore <= alpha) {
                                beta = (alpha + beta) / 2;
                                alpha = std::max(-SCORE_INFINITY, alpha - delta);
                                delta += delta / 2;
                            } else if (hscore >= beta) {
                                beta = std::min(SCORE_INFINITY, beta + delta);
                                delta += delta / 2;
                            } else {
                                break;
                            }

                            if (delta > 500) {
                                alpha = -SCORE_INFINITY;
                                beta  =  SCORE_INFINITY;
                            }
                        }

                        if (hctx.should_stop() || helper_stop->load(std::memory_order_relaxed)) break;
                        hbest_score = hscore;

                        if (d >= max_depth) break;
                    }

                    {
                        std::lock_guard<std::mutex> dlock(worker->mtx);
                        worker->is_busy = false;
                    }
                    worker->cv_done.notify_one();
                }
            });
            workers.push_back(std::move(w));
        }
    }

    void start_helpers(const Position& root_pos, const StateInfo& root_state, int num_threads,
                       int max_depth, const SearchLimits& limits, std::atomic<bool>* helper_stop,
                       TranspositionTable& tt, std::chrono::steady_clock::time_point start_time) {
        int needed = num_threads - 1;
        if (needed <= 0) return;
        if ((int)workers.size() != needed) {
            resize(needed);
        }

        for (int i = 0; i < needed; ++i) {
            auto& w = workers[i];
            {
                std::lock_guard<std::mutex> lock(w->mtx);
                w->total_threads = num_threads;
                w->max_depth = max_depth;
                w->time_limit_ms = limits.time_limit_ms;
                w->external_stop = limits.external_stop;
                w->helper_stop = helper_stop;
                w->root_pos = root_pos;
                w->root_state = root_state;
                w->tt = &tt;
                w->start_time = start_time;
                w->has_work = true;
            }
            w->cv_start.notify_one();
        }
    }

    void wait_helpers() {
        for (auto& w : workers) {
            std::unique_lock<std::mutex> lock(w->mtx);
            w->cv_done.wait(lock, [&]() { return !w->is_busy; });
        }
    }
};

static WorkerPool GLOBAL_WORKER_POOL;

// ── Root-Move Parallel Search / Young Brothers Wait Concept (YBWC) ───────────
inline SearchResult search_root_parallel(Position& pos, const SearchLimits& limits, TranspositionTable& tt) {
    init_king_shields();
    init_lmr_table();
    SearchResult result;
    SearchContext main_ctx;
    main_ctx.reset();
    main_ctx.start_time = std::chrono::steady_clock::now();
    main_ctx.time_limit_ms = limits.time_limit_ms;
    main_ctx.node_limit = limits.max_nodes;
    main_ctx.external_stop = limits.external_stop;

    tt.current_age++;

    Move best_move = MOVE_NONE;
    int best_score = 0;
    uint64_t total_nodes = 0;
    int target_depth = std::clamp(limits.depth, 1, 64);
    int num_threads = std::clamp(limits.thread_count, 1, 64);

    MoveList root_moves;
    generate_legal_moves(pos, root_moves);
    if (root_moves.empty()) return result;
    result.best_move = root_moves[0];

    // Iterative deepening up to target_depth
    for (int depth = 1; depth <= target_depth; ++depth) {
        if (main_ctx.should_stop()) break;

        if (depth < 4 || num_threads <= 1) {
            // Low depth: run on main thread
            int score = alpha_beta(pos, -SCORE_INFINITY, SCORE_INFINITY, depth, 0, main_ctx, tt);
            if (main_ctx.should_stop()) break;

            best_score = score;
            if (main_ctx.pv_length[0] > 0) best_move = main_ctx.pv_table[0][0];

            result.depth = depth;
            result.seldepth = main_ctx.seldepth;
            result.score = best_score;
            result.best_move = best_move;
            result.pv.clear();
            for (int i = 0; i < main_ctx.pv_length[0]; ++i) {
                result.pv.push_back(main_ctx.pv_table[0][i]);
            }
            if (is_mate_score(best_score)) break;
            continue;
        }

        // Depth >= 4: Root Move Parallelism (YBWC)
        // Sort root moves so best_move from previous iteration is first
        int move_scores[256];
        for (int i = 0; i < root_moves.size(); ++i) {
            if (root_moves[i] == best_move) move_scores[i] = 2000000;
            else move_scores[i] = score_move(pos, root_moves[i], MOVE_NONE, 0, main_ctx);
        }
        for (int i = 0; i < root_moves.size(); ++i) {
            pick_next_best_move(root_moves, move_scores, i);
        }

        // 1. Search oldest brother (PV move) with main thread to establish alpha
        StateInfo state0;
        make_move(pos, root_moves[0], state0);
        int alpha = -alpha_beta(pos, -SCORE_INFINITY, SCORE_INFINITY, depth - 1, 1, main_ctx, tt);
        undo_move(pos, root_moves[0]);

        if (main_ctx.should_stop()) break;

        best_score = alpha;
        best_move = root_moves[0];

        // 2. Search young brothers in parallel with zero-window scouting
        std::atomic<size_t> next_idx{1};
        std::mutex root_mutex;
        std::atomic<bool> worker_stop{false};
        std::atomic<int> shared_alpha{alpha};

        std::vector<std::thread> workers;
        std::vector<SearchContext> worker_contexts(num_threads);

        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back([&, t]() {
#ifdef _WIN32
                apply_thread_affinity(t, num_threads);
#endif
                SearchContext& wctx = worker_contexts[t];
                wctx.reset();
                wctx.start_time = main_ctx.start_time;
                wctx.time_limit_ms = limits.time_limit_ms;
                wctx.external_stop = limits.external_stop;

                Position wpos = pos;
                StateInfo wstate = *pos.state;
                wstate.previous = nullptr;
                wpos.state = &wstate;

                while (!worker_stop.load(std::memory_order_relaxed) && !wctx.should_stop()) {
                    size_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= root_moves.size()) break;

                    Move m = root_moves[idx];
                    StateInfo m_state;
                    make_move(wpos, m, m_state);

                    // Zero-window scout search
                    int cur_alpha = shared_alpha.load(std::memory_order_relaxed);
                    int score = -alpha_beta(wpos, -cur_alpha - 1, -cur_alpha, depth - 1, 1, wctx, tt);

                    if (score > cur_alpha && !wctx.should_stop()) {
                        // Re-search with full window upon fail-high
                        score = -alpha_beta(wpos, -SCORE_INFINITY, -cur_alpha, depth - 1, 1, wctx, tt);
                        if (score > cur_alpha) {
                            std::lock_guard<std::mutex> lock(root_mutex);
                            if (score > best_score) {
                                best_score = score;
                                best_move = m;
                                shared_alpha.store(score, std::memory_order_relaxed);
                            }
                        }
                    }
                    undo_move(wpos, m);
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        for (const auto& wctx : worker_contexts) {
            total_nodes += wctx.nodes;
        }

        if (main_ctx.should_stop()) break;

        result.depth = depth;
        result.seldepth = main_ctx.seldepth;
        result.score = best_score;
        result.best_move = best_move;
        result.pv.clear();
        result.pv.push_back(best_move);
        if (is_mate_score(best_score)) break;
    }

    total_nodes += main_ctx.nodes;
    result.nodes = total_nodes;
    result.best_move = best_move;
    result.score = best_score;
    result.depth = target_depth;

    auto now = std::chrono::steady_clock::now();
    result.time_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - main_ctx.start_time).count());
    result.nps = (result.time_ms > 0) ? (result.nodes * 1000 / result.time_ms) : result.nodes;
    return result;
}

// ── Iterative Deepening & Root Search ────────────────────────────────────────
inline SearchResult search_position(Position& pos, const SearchLimits& limits, TranspositionTable& tt) {
    PROFILE_ZONE("SearchPosition");
    if (limits.root_parallel && limits.thread_count > 1) {
        return search_root_parallel(pos, limits, tt);
    }

    init_king_shields();
    init_lmr_table();
    SearchResult result;
    SearchContext ctx;
    ctx.reset();
    ctx.start_time = std::chrono::steady_clock::now();
    ctx.time_limit_ms = limits.time_limit_ms;
    ctx.node_limit = limits.max_nodes;
    ctx.external_stop = limits.external_stop;

    tt.current_age++;

    int max_depth = std::clamp(limits.depth, 1, 64);
    int best_score = 0;
    Move best_move = MOVE_NONE;
    int num_threads = std::clamp(limits.thread_count, 1, 64);

    MoveList root_legal;
    generate_legal_moves(pos, root_legal);
    if (root_legal.empty()) return result;
    result.best_move = root_legal[0];

    std::atomic<bool> helper_stop{false};
    std::mutex result_mutex;

    Position root_pos = pos;
    StateInfo root_state = *pos.state;
    root_state.previous = nullptr;
    root_pos.state = &root_state;

    if (num_threads > 1) {
        GLOBAL_WORKER_POOL.start_helpers(root_pos, root_state, num_threads, max_depth, limits, &helper_stop, tt, ctx.start_time);
    }

#ifdef _WIN32
    apply_thread_affinity(0, num_threads);
#endif

    // Iterative Deepening Loop
    for (int current_depth = 1; current_depth <= max_depth; ++current_depth) {
        if (helper_stop.load(std::memory_order_relaxed)) break;

        PROFILE_FRAME("DepthIteration");
        PROFILE_ZONE("DepthIteration");

        // Aspiration Windows with Exponential Widening (depth >= 4)
        int delta = 25;
        int alpha = -SCORE_INFINITY;
        int beta  =  SCORE_INFINITY;

        if (current_depth >= 4 && best_score != 0) {
            alpha = std::max(-SCORE_INFINITY, best_score - delta);
            beta  = std::min(SCORE_INFINITY,  best_score + delta);
        }

        int score = 0;
        while (true) {
            {
                PROFILE_ZONE("AspirationSearch");
                score = alpha_beta(pos, alpha, beta, current_depth, 0, ctx, tt);
            }

            if (ctx.should_stop() || helper_stop.load(std::memory_order_relaxed)) break;

            // Aspiration window fail-low or fail-high with exponential widening
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(-SCORE_INFINITY, alpha - delta);
                delta += delta / 2;
            } else if (score >= beta) {
                beta = std::min(SCORE_INFINITY, beta + delta);
                delta += delta / 2;
            } else {
                break; // Score fell inside window
            }

            if (delta > 500) {
                alpha = -SCORE_INFINITY;
                beta  =  SCORE_INFINITY;
            }
        }

        if (ctx.should_stop() || helper_stop.load(std::memory_order_relaxed)) break;

        {
            std::lock_guard<std::mutex> lock(result_mutex);
            if (current_depth >= result.depth) {
                best_score = score;
                if (ctx.pv_length[0] > 0) {
                    best_move = ctx.pv_table[0][0];
                }

                result.depth = current_depth;
                result.seldepth = ctx.seldepth;
                result.score = best_score;
                result.best_move = best_move;

                result.pv.clear();
                for (int i = 0; i < ctx.pv_length[0]; ++i) {
                    result.pv.push_back(ctx.pv_table[0][i]);
                }
            }
        }

        PROFILE_PLOT("Search Depth", current_depth);
        PROFILE_PLOT("Best Score", score);

        // Stop early if checkmate is found
        if (is_mate_score(best_score)) break;
    }

    if (num_threads > 1) {
        helper_stop.store(true, std::memory_order_relaxed);
        GLOBAL_WORKER_POOL.wait_helpers();

        result.nodes = ctx.nodes;
        result.tt_hits = ctx.tt_hits;
        result.tt_misses = ctx.tt_misses;
        for (const auto& w : GLOBAL_WORKER_POOL.workers) {
            result.nodes += w->ctx.nodes;
            result.tt_hits += w->ctx.tt_hits;
            result.tt_misses += w->ctx.tt_misses;
        }
    } else {
        result.nodes = ctx.nodes;
        result.tt_hits = ctx.tt_hits;
        result.tt_misses = ctx.tt_misses;
    }

    tt.hits = result.tt_hits;
    tt.misses = result.tt_misses;

    auto now = std::chrono::steady_clock::now();
    result.time_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx.start_time).count());
    result.nps = (result.time_ms > 0) ? (result.nodes * 1000 / result.time_ms) : result.nodes;

    PROFILE_PLOT("Search Nodes", (int64_t)result.nodes);
    PROFILE_PLOT("NPS", (int64_t)result.nps);
    PROFILE_PLOT("TT Hits", (int64_t)result.tt_hits);
    PROFILE_PLOT("TT Misses", (int64_t)result.tt_misses);

    return result;
}

} // namespace tigerfish
