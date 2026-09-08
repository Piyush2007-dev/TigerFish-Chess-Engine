#pragma once
#include "eval.cpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetActiveProcessorCount(unsigned short GroupNumber);
#endif

using namespace std;

static const int MATE_SCORE = 100000;
static const int MATE_THRESHOLD = 90000;
static const int MAX_SEARCH_PLY = 128;
static const int MAX_QSEARCH_DEPTH = 8;
static const int MAX_HISTORY_SCORE = 16384;

inline bool is_mate_score(int score) {
    return abs(score) >= MATE_THRESHOLD;
}

inline int mate_in_plies(int score) {
    return score > 0 ? MATE_SCORE - score : -(MATE_SCORE + score);
}

inline int score_to_transposition_table(int score, int ply) {
    if (score >= MATE_THRESHOLD) return score + ply;
    if (score <= -MATE_THRESHOLD) return score - ply;
    return score;
}

inline int score_from_transposition_table(int score, int ply) {
    if (score >= MATE_THRESHOLD) return score - ply;
    if (score <= -MATE_THRESHOLD) return score + ply;
    return score;
}

enum TranspositionFlag : uint8_t {
    TT_EXACT = 0,
    TT_ALPHA = 1,
    TT_BETA = 2
};

struct TranspositionEntry {
    atomic<uint64_t> key{0};
    atomic<int32_t> evaluation{0};
    atomic<uint32_t> meta{0};
    atomic<uint16_t> best_move{0};
};

static constexpr size_t TT_SIZE = 2097152;
static TranspositionEntry TT_GLOBAL[TT_SIZE];

class TranspositionTable {
public:
    static constexpr size_t TABLE_SIZE = TT_SIZE;
    atomic<uint64_t> total_hits{0};
    atomic<uint64_t> total_misses{0};
    uint32_t current_age{0}; // generation counter, wraps at 128

    TranspositionTable() = default;
    ~TranspositionTable() = default;

    TranspositionTable(const TranspositionTable& other)
        : total_hits(other.total_hits.load()),
          total_misses(other.total_misses.load()),
          current_age(other.current_age) {}

    TranspositionTable& operator=(const TranspositionTable& other) {
        if (this != &other) {
            total_hits.store(other.total_hits.load());
            total_misses.store(other.total_misses.load());
            current_age = other.current_age;
        }
        return *this;
    }

    TranspositionTable(TranspositionTable&& other) noexcept
        : total_hits(other.total_hits.load()),
          total_misses(other.total_misses.load()),
          current_age(other.current_age) {}

    TranspositionTable& operator=(TranspositionTable&& other) noexcept {
        if (this != &other) {
            total_hits.store(other.total_hits.load());
            total_misses.store(other.total_misses.load());
            current_age = other.current_age;
        }
        return *this;
    }

    void initialize() {
        // Flat array is static global and always initialized
    }

    void clear() {
        memset(reinterpret_cast<void*>(TT_GLOBAL), 0, sizeof(TT_GLOBAL));
        total_hits.store(0);
        total_misses.store(0);
    }

    void new_search() {
        current_age = (current_age + 1) & 127;
    }

    inline bool probe(uint64_t position_key, int depth, int alpha, int beta,
                      int& result, uint16_t& tt_move, int ply) const {
        const TranspositionEntry& entry = TT_GLOBAL[position_key & (TABLE_SIZE - 1)];
        uint64_t key = entry.key.load(memory_order_acquire);
        if (key != position_key) return false;

        uint16_t move = entry.best_move.load(memory_order_relaxed);
        int stored_score = entry.evaluation.load(memory_order_relaxed);
        uint32_t packed_meta = entry.meta.load(memory_order_relaxed);
        
        // Acquire fence ensures that the relaxed loads above cannot be reordered
        // to happen AFTER the final key verification check below.
        atomic_thread_fence(memory_order_acquire);
        
        if (entry.key.load(memory_order_relaxed) != position_key) return false;

        int stored_depth = (int)((packed_meta >> 2) & 0x7F);
        TranspositionFlag flag = (TranspositionFlag)(packed_meta & 3u);
        tt_move = move;
        if (stored_depth < depth) return false;

        int score = score_from_transposition_table(stored_score, ply);
        if (flag == TT_EXACT ||
            (flag == TT_ALPHA && score <= alpha) ||
            (flag == TT_BETA && score >= beta)) {
            result = score;
            return true;
        }
        return false;
    }

    inline void store(uint64_t position_key, int depth, int score, TranspositionFlag flag,
                      uint16_t best_move, int ply) {
        TranspositionEntry& entry = TT_GLOBAL[position_key & (TABLE_SIZE - 1)];
        uint64_t old_key = entry.key.load(memory_order_relaxed);
        uint32_t old_meta = entry.meta.load(memory_order_relaxed);
        int old_depth = (int)((old_meta >> 2) & 0x7F);
        int old_age = (int)((old_meta >> 9) & 0x7F);
        // Always overwrite if: different key, or deeper search, or old generation
        if (old_key == position_key && old_depth > depth &&
            old_age == (int)current_age) return;

        entry.best_move.store(best_move, memory_order_relaxed);
        entry.evaluation.store(score_to_transposition_table(score, ply), memory_order_relaxed);
        entry.meta.store(((uint32_t)(current_age & 0x7F) << 9) |
                         ((uint32_t)max(0, min(127, depth)) << 2) | (uint32_t)flag,
                         memory_order_relaxed);
        entry.key.store(position_key, memory_order_release);
    }
};

struct NullMoveState {
    Color side_to_move;
    uint8_t castling_rights;
    uint8_t en_passant;
    uint64_t zobrist_hash;
};

inline void make_null_move(Board& board, NullMoveState& state) {
    state.side_to_move = board.side_to_move;
    state.castling_rights = board.castling_rights;
    state.en_passant = board.en_passant;
    state.zobrist_hash = board.zobrist_hash;
    board.zobrist_hash ^= ZOBRIST_EP[board.en_passant == 255 ? 64 : board.en_passant];
    board.en_passant = 255;
    board.zobrist_hash ^= ZOBRIST_EP[64];
    board.side_to_move = board.side_to_move == WHITE ? BLACK : WHITE;
    board.enemy_color = board.side_to_move == WHITE ? BLACK : WHITE;
    board.zobrist_hash ^= ZOBRIST_SIDE;
}

inline void unmake_null_move(Board& board, const NullMoveState& state) {
    board.side_to_move = state.side_to_move;
    board.enemy_color = board.side_to_move == WHITE ? BLACK : WHITE;
    board.castling_rights = state.castling_rights;
    board.en_passant = state.en_passant;
    board.zobrist_hash = state.zobrist_hash;
}

class WorkerPool;

class Engine {
public:
    TranspositionTable tt;
    uint64_t nodes_searched = 0;
    uint64_t nodes_limit = 0;
    int time_limit_ms = 0;
    int thread_count = 1;
    int thread_id = 0;
    TranspositionTable* shared_tt = nullptr;
    const atomic<bool>* external_stop_flag = nullptr;
    bool search_stopped = false;
    int completed_depth = 0;
    vector<uint16_t> root_moves;
    vector<int> root_scores;
    chrono::steady_clock::time_point search_start_time;
    uint16_t killer_moves[MAX_SEARCH_PLY][2]{};
    int history_table[2][64][64]{};
    uint64_t node_check_mask = 65535;
    unique_ptr<WorkerPool> worker_pool;

    Engine();
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void configure_search(uint64_t node_limit_value, int time_limit,
                          const atomic<bool>* stop_flag) {
        nodes_limit = node_limit_value;
        time_limit_ms = time_limit;
        external_stop_flag = stop_flag;
        search_stopped = false;
        if (time_limit_ms > 0 && time_limit_ms <= 500) {
            node_check_mask = 1023;
        } else if (time_limit_ms > 500) {
            node_check_mask = 4095;
        } else {
            node_check_mask = 65535;
        }
    }

    TranspositionTable& get_tt() { return shared_tt ? *shared_tt : tt; }

    bool should_stop() {
        if (search_stopped) return true;
        if ((nodes_searched & node_check_mask) != 0) return false;

        if (external_stop_flag && external_stop_flag->load(memory_order_relaxed)) {
            search_stopped = true;
        } else if (nodes_limit && nodes_searched >= nodes_limit) {
            search_stopped = true;
        } else if (time_limit_ms && chrono::duration_cast<chrono::milliseconds>(
                       chrono::steady_clock::now() - search_start_time).count() >= time_limit_ms) {
            search_stopped = true;
        }
        return search_stopped;
    }

    void order_moves(MoveList& moves, uint16_t tt_move, int ply, int side, const Board& board) {
        // Score all moves: TT move gets locked top score, captures scored with guarded SEE, quiets with history/killers
        for (int i = 0; i < moves.size(); ++i) {
            uint16_t move = moves.move_list[i];
            if (tt_move && move == tt_move) {
                moves.scores[i] = 20000000; // Guaranteed to be tried first, immune to displacement
                continue;
            }
            if (move_is_capture(move) || move_is_promotion(move)) {
                Piece attacker = board.piece_on[move_from(move)];
                Piece victim;
                if (move_is_en_passant(move)) {
                    victim = (board.side_to_move == WHITE) ? Piece::p : Piece::P;
                } else {
                    victim = board.piece_on[move_to(move)];
                }
                int victim_val = see_piece_value((int)victim % 6);
                int attacker_val = see_piece_value((int)attacker % 6);

                if (victim_val >= attacker_val || move_is_promotion(move)) {
                    // Winning or equal trade: fast MVV-LVA score, skip expensive SEE simulation
                    moves.scores[i] = 10000 + (victim_val * 10 - attacker_val);
                } else {
                    // Potentially losing capture: verify with SEE
                    int see_score = see_full(board, move);
                    moves.scores[i] = (see_score >= 0) ? (10000 + see_score) : (-5000 + see_score);
                }
            } else {
                int score = history_table[side][move_from(move)][move_to(move)];
                if (ply < MAX_SEARCH_PLY) {
                    if (move == killer_moves[ply][0]) score += 1000;
                    else if (move == killer_moves[ply][1]) score += 500;
                }
                moves.scores[i] = score;
            }
        }
        // Insertion sort by score — fast for small N, good cache behavior
        for (int i = 1; i < moves.size(); ++i) {
            uint32_t move = moves.move_list[i];
            int score = moves.scores[i];
            int j = i - 1;
            while (j >= 0 && moves.scores[j] < score) {
                moves.move_list[j + 1] = moves.move_list[j];
                moves.scores[j + 1] = moves.scores[j];
                --j;
            }
            moves.move_list[j + 1] = move;
            moves.scores[j + 1] = score;
        }
    }

    void update_history(int side, int from, int to, int depth) {
        int& value = history_table[side][from][to];
        int bonus = min(MAX_HISTORY_SCORE, depth * depth);
        value += bonus - value * bonus / MAX_HISTORY_SCORE;
    }

    int evaluate_for_side(const Board& board) const {
        int score = evaluate(const_cast<Board&>(board));
        return board.side_to_move == WHITE ? score : -score;
    }

    int quiescence(Board& board, int alpha, int beta, int ply, bool in_check = false) {
        ++nodes_searched;
        if (should_stop() || ply >= MAX_QSEARCH_DEPTH) return evaluate_for_side(board);

        int stand_pat = 0;
        if (!in_check) {
            stand_pat = evaluate_for_side(board);
            if (stand_pat >= beta) return stand_pat;
            if (stand_pat > alpha) alpha = stand_pat;
        }

        // Delta pruning margin: queen value + small buffer
        static const int DELTA_MARGIN = 1000;

        MoveList moves;
        MoveGenerator generator;
        if (in_check) {
            generator.generate_moves(board, moves);
        } else {
            generator.generate_captures(board, moves, true);
        }
        moves.sort_mvv_lva();
        int legal_moves_searched = 0;
        for (int i = 0; i < moves.size() && !should_stop(); ++i) {
            uint16_t move = moves.move_list[i];

            if (!in_check) {
                // Guarded SEE pruning: only call see_full if victim < attacker
                Piece attacker = board.piece_on[move_from(move)];
                Piece victim;
                if (move_is_en_passant(move)) {
                    victim = (board.side_to_move == WHITE) ? Piece::p : Piece::P;
                } else {
                    victim = board.piece_on[move_to(move)];
                }
                int victim_val = see_piece_value((int)victim % 6);
                int attacker_val = see_piece_value((int)attacker % 6);
                if (victim_val < attacker_val && !move_is_promotion(move)) {
                    if (see_full(board, move) < 0) continue;
                }

                // Delta pruning: if captured piece value + margin can't reach alpha, skip
                if (!move_is_promotion(move)) {
                    if (victim != (Piece)0xF) {
                        static const int piece_delta[12] = {
                            100, 320, 330, 500, 900, 20000,
                            100, 320, 330, 500, 900, 20000
                        };
                        if (stand_pat + piece_delta[victim] + DELTA_MARGIN < alpha)
                            continue;
                    }
                }
            }

            board.make_move(move);
            _mm_prefetch((const char*)&TT_GLOBAL[board.zobrist_hash & (TT_SIZE - 1)], _MM_HINT_T0);
            legal_moves_searched++;
            bool gives_check = is_in_check(board);
            int score = -quiescence(board, -beta, -alpha, ply + 1, gives_check);
            board.unmake_move();
            if (score >= beta) return score;
            if (score > alpha) alpha = score;
        }

        if (in_check && legal_moves_searched == 0) {
            return -MATE_SCORE + ply;
        }

        return alpha;
    }

    int search(Board& board, int depth, int alpha, int beta, int ply, bool pv_node) {
        ++nodes_searched;
        if (should_stop()) return evaluate_for_side(board);
        if (ply >= MAX_SEARCH_PLY) return evaluate_for_side(board);
        if (ply > 0 && (board.is_repetition(3) || board.halfmove_clock >= 100)) return 0;

        // Mate distance pruning
        if (ply > 0) {
            int mating_value = -MATE_SCORE + ply;
            if (mating_value > alpha) alpha = mating_value;
            int mated_value = MATE_SCORE - ply - 1;
            if (mated_value < beta) beta = mated_value;
            if (alpha >= beta) return alpha;
        }

        int tt_score = 0;
        uint16_t tt_move = 0;
        if (get_tt().probe(board.zobrist_hash, depth, alpha, beta, tt_score, tt_move, ply)) {
            get_tt().total_hits.fetch_add(1, memory_order_relaxed);
            return tt_score;
        }
        get_tt().total_misses.fetch_add(1, memory_order_relaxed);

        bool in_check = is_in_check(board);
        if (depth <= 0 && !in_check) return quiescence(board, alpha, beta, 0, false);
        if (depth <= 0) depth = 1;

        // Static evaluation for pruning decisions
        int static_eval = in_check ? -MATE_SCORE : evaluate_for_side(board);

        // ── Reverse Futility Pruning (Static Null Move Pruning) ──
        // If eval is way above beta at low depth, we're likely winning
        if (!pv_node && !in_check && depth <= 3 && abs(beta) < MATE_THRESHOLD) {
            int margin = 120 * depth;
            if (static_eval - margin >= beta)
                return static_eval - margin;
        }

        // ── Null Move Pruning ──
        // Skip our turn; if we're still way ahead, we can prune
        if (!pv_node && !in_check && depth >= 3 && ply > 0 &&
            static_eval >= beta && abs(beta) < MATE_THRESHOLD) {
        // Don't null move in positions with few pieces (zugzwang risk)
        // Count non-pawn, non-king material only — pawn endgames are prone to zugzwang
        int non_pawn_material = popcount64(
            (board.occupancy[WHITE] | board.occupancy[BLACK])
            & ~board.bitboards[Piece::P]
            & ~board.bitboards[Piece::p]
            & ~board.bitboards[Piece::K]
            & ~board.bitboards[Piece::k]);
        if (non_pawn_material >= 2) {
            int R = 3 + depth / 4;
            NullMoveState null_state;
            make_null_move(board, null_state);
            int score = -search(board, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
            unmake_null_move(board, null_state);
            if (score >= beta) {
                if (score >= MATE_THRESHOLD) score = beta;
                return score;
            }
        }
        }

        MoveList moves;
        MoveGenerator generator;
        generator.generate_moves(board, moves);
        if (moves.size() == 0) {
            return in_check ? -MATE_SCORE + ply : 0;
        }

        order_moves(moves, tt_move, ply, board.side_to_move == WHITE ? 0 : 1, board);
        int original_alpha = alpha;
        int best_score = -MATE_SCORE;
        uint16_t best_move = 0;
        int moves_searched = 0;

        // ── Futility Pruning flag ──
        bool futility_pruning = false;
        if (!pv_node && !in_check && depth <= 2 && abs(alpha) < MATE_THRESHOLD) {
            if (static_eval + 200 * depth <= alpha)
                futility_pruning = true;
        }

        for (int i = 0; i < moves.size() && !should_stop(); ++i) {
            uint16_t move = moves.move_list[i];
            bool is_quiet = !move_is_capture(move) && !move_is_promotion(move);

            // Futility pruning: skip quiet moves at low depth when far behind
            if (futility_pruning && moves_searched > 0 && is_quiet)
                continue;

            // ── Late Move Pruning ──
            // At low depths, skip quiet moves beyond a certain count
            if (!in_check && depth <= 2 && moves_searched > 6 + depth * 2 && is_quiet)
                continue;

            board.make_move(move);
            _mm_prefetch((const char*)&TT_GLOBAL[board.zobrist_hash & (TT_SIZE - 1)], _MM_HINT_T0);
            int score;

            // ── Late Move Reductions (LMR) ──
            // After the first few moves, search quiet moves with reduced depth
            bool do_full_search = true;
            if (moves_searched >= (pv_node ? 5 : 3) && depth >= 3 && is_quiet && !in_check) {
                int hist = history_table[board.enemy_color == WHITE ? 0 : 1][move_from(move)][move_to(move)];
                int reduction = 1 + (moves_searched > 8 ? 1 : 0);
                reduction -= hist / 4000;
                reduction = max(1, min(reduction, depth - 2));

                score = -search(board, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, false);
                do_full_search = score > alpha;
            }

            if (do_full_search) {
                if (i == 0 || !pv_node) {
                    score = -search(board, depth - 1, -beta, -alpha, ply + 1, pv_node && i == 0);
                } else {
                    score = -search(board, depth - 1, -alpha - 1, -alpha, ply + 1, false);
                    if (score > alpha && score < beta)
                        score = -search(board, depth - 1, -beta, -alpha, ply + 1, true);
                }
            }
            board.unmake_move();
            moves_searched++;

            if (score > best_score) {
                best_score = score;
                best_move = move;
            }
            if (score > alpha) alpha = score;
            if (alpha >= beta) {
                if (is_quiet) {
                    if (ply < MAX_SEARCH_PLY) {
                        killer_moves[ply][1] = killer_moves[ply][0];
                        killer_moves[ply][0] = move;
                    }
                    // enemy_color is the side that just moved (side_to_move flipped after make_move)
                    update_history(board.enemy_color == WHITE ? 0 : 1,
                                   move_from(move), move_to(move), depth);
                }
                break;
            }
        }

        if (!should_stop() && best_move) {
            TranspositionFlag flag = best_score <= original_alpha ? TT_ALPHA
                : best_score >= beta ? TT_BETA : TT_EXACT;
            get_tt().store(board.zobrist_hash, depth, best_score, flag, best_move, ply);
        }
        return best_score;
    }

    uint16_t search_depth(Board& board, int depth) {
        MoveList moves;
        MoveGenerator generator;
        generator.generate_moves(board, moves);
        if (moves.size() == 0) return 0;
        moves.sort_mvv_lva();
        if (!root_moves.empty()) {
            for (int i = 0; i < moves.size(); ++i) {
                if (moves.move_list[i] == root_moves[0]) {
                    swap(moves.move_list[0], moves.move_list[i]);
                    swap(moves.scores[0], moves.scores[i]);
                    break;
                }
            }
        }

        bool pv = true;
        uint16_t best_move = moves.move_list[0];
        int alpha = -MATE_SCORE;
        int beta = MATE_SCORE;
        int root_score = 0;

        // Aspiration windows: narrow window around previous iteration's score
        int aspiration_delta = 25;
        bool use_aspiration = (depth >= 4 && !root_scores.empty() &&
                               abs(root_scores[0]) < MATE_THRESHOLD);
        if (use_aspiration) {
            alpha = root_scores[0] - aspiration_delta;
            beta = root_scores[0] + aspiration_delta;
        }

        // Main root search loop — may re-search with wider windows
        while (true) {
            int local_alpha = alpha;
            int local_beta = beta;
            int local_best_score = -MATE_SCORE;

            for (int i = 0; i < moves.size() && !should_stop(); ++i) {
                uint16_t move = moves.move_list[i];
                board.make_move(move);
                int score;
                if (i == 0) {
                    score = -search(board, depth - 1, -local_beta, -local_alpha, 1, pv);
                } else {
                    score = -search(board, depth - 1, -local_alpha - 1, -local_alpha, 1, false);
                    if (score > local_alpha && score < local_beta)
                        score = -search(board, depth - 1, -local_beta, -local_alpha, 1, true);
                }
                board.unmake_move();
                moves.scores[i] = score; // overwrite ordering score with search result
                if (score > local_best_score) {
                    local_best_score = score;
                    best_move = move;
                }
                if (score > local_alpha) local_alpha = score;
            }

            if (should_stop()) break;
            root_score = local_best_score;

            // If not using aspiration, we're done
            if (!use_aspiration) break;

            // Check if the score falls inside the aspiration window
            if (root_score > alpha && root_score < beta) {
                break; // Exact score within window
            }
            if (root_score <= alpha) {
                // Fail low: widen downward
                alpha = -MATE_SCORE;
                beta = root_score + aspiration_delta * 2;
            } else if (root_score >= beta) {
                // Fail high: widen upward only — keep alpha at its current lower bound
                beta = MATE_SCORE;
                // do NOT narrow alpha here; the true score is above beta
            }
        }

        if (should_stop()) return root_moves.empty() ? 0 : root_moves[0];
        vector<int> order(moves.size());
        for (int i = 0; i < moves.size(); ++i) order[i] = i;
        stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return moves.scores[a] > moves.scores[b];
        });
        MoveList ordered;
        for (int index : order) ordered.push(moves.move_list[index], moves.scores[index]);
        moves = ordered;
        prioritize_root(moves, best_move);
        root_moves.assign(moves.move_list, moves.move_list + moves.size());
        root_scores.clear();
        for (int i = 0; i < moves.size(); ++i) root_scores.push_back(moves.scores[i]);
        return best_move;
    }

    void prioritize_root(MoveList& moves, uint16_t move) {
        for (int i = 0; i < moves.size(); ++i) {
            if (moves.move_list[i] == move) {
                swap(moves.move_list[0], moves.move_list[i]);
                swap(moves.scores[0], moves.scores[i]);
                return;
            }
        }
    }

    void worker_search(Board board, int max_depth, atomic<bool>& stop_flag, int time_ms) {
        nodes_searched = 0;
        search_stopped = false;
        external_stop_flag = &stop_flag;
        time_limit_ms = time_ms;
        node_check_mask = (time_ms > 0 && time_ms <= 500) ? 1023 : (time_ms > 500 ? 4095 : 65535);
        nodes_limit = 0;
        search_start_time = chrono::steady_clock::now();
        memset(killer_moves, 0, sizeof(killer_moves));
        int* h_decay = &history_table[0][0][0];
        for (int i = 0; i < 2 * 64 * 64; ++i) h_decay[i] >>= 1;
        for (int depth = 1; depth <= max_depth && !should_stop(); ++depth) {
            search_depth(board, depth);
            if (!search_stopped) completed_depth = depth;
        }
    }

    uint16_t best_move(Board& board, int depth) {
        int hardware = (int)thread::hardware_concurrency();
#ifdef _WIN32
        unsigned long win_procs = GetActiveProcessorCount(0xFFFF);
        if ((int)win_procs > hardware) {
            hardware = (int)win_procs;
        }
#endif
        if (hardware < 1) hardware = 1;
        thread_count = max(1, min(thread_count, max(hardware, 256)));
        nodes_searched = 0;
        search_stopped = false;
        completed_depth = 0;
        root_moves.clear();
        root_scores.clear();
        search_start_time = chrono::steady_clock::now();
        memset(killer_moves, 0, sizeof(killer_moves));
        int* h_decay = &history_table[0][0][0];
        for (int i = 0; i < 2 * 64 * 64; ++i) h_decay[i] >>= 1;
        if (!shared_tt) shared_tt = &tt;
        get_tt().initialize();
        get_tt().new_search();
        uint16_t result = 0;

        if (thread_count <= 1) {
            for (int current = 1; current <= max(1, depth) && !should_stop(); ++current) {
                result = search_depth(board, current);
                if (!search_stopped) completed_depth = current;
            }
            return result;
        }

        atomic<bool> stop_flag{false};
        start_workers(board, depth, time_limit_ms, stop_flag);

        for (int current = 1; current <= max(1, depth) && !should_stop(); ++current) {
            result = search_depth(board, current);
            if (!search_stopped) completed_depth = current;
        }

        stop_and_collect_workers(stop_flag);
        return result;
    }

    void start_workers(const Board& board, int depth, int time_ms, atomic<bool>& stop_flag);
    void stop_and_collect_workers(atomic<bool>& stop_flag);
};

struct WorkerThread {
    Engine engine;
    thread thr;
    mutex mtx;
    condition_variable cv_work;
    condition_variable cv_done;
    bool has_work = false;
    bool is_done = true;
    bool terminate = false;
    Board work_board;
    int work_depth = 0;
    int work_time_ms = 0;
    atomic<bool>* stop_flag = nullptr;

    WorkerThread(int id, TranspositionTable* tt) {
        engine.shared_tt = tt;
        engine.thread_id = id;
        engine.thread_count = 1;
        thr = thread(&WorkerThread::run, this);
    }

    ~WorkerThread() {
        {
            lock_guard<mutex> lock(mtx);
            terminate = true;
            cv_work.notify_one();
        }
        if (thr.joinable()) thr.join();
    }

    void run() {
        while (true) {
            unique_lock<mutex> lock(mtx);
            cv_work.wait(lock, [this] { return has_work || terminate; });
            if (terminate) break;

            lock.unlock();
            if (stop_flag) {
                engine.worker_search(work_board, work_depth, *stop_flag, work_time_ms);
            }
            lock.lock();

            has_work = false;
            is_done = true;
            cv_done.notify_one();
        }
    }

    void start(const Board& board, int depth, int time_ms, atomic<bool>* stop) {
        lock_guard<mutex> lock(mtx);
        work_board = board;
        work_depth = depth;
        work_time_ms = time_ms;
        stop_flag = stop;
        has_work = true;
        is_done = false;
        cv_work.notify_one();
    }

    void wait_done() {
        unique_lock<mutex> lock(mtx);
        cv_done.wait(lock, [this] { return is_done; });
    }
};

class WorkerPool {
public:
    vector<unique_ptr<WorkerThread>> workers;

    void resize(int count, TranspositionTable* tt) {
        if ((int)workers.size() == count) return;
        workers.clear();
        for (int i = 0; i < count; ++i) {
            workers.push_back(make_unique<WorkerThread>(i + 1, tt));
        }
    }
};

inline Engine::Engine() = default;
inline Engine::~Engine() = default;
inline Engine::Engine(Engine&&) noexcept = default;
inline Engine& Engine::operator=(Engine&&) noexcept = default;

inline void Engine::start_workers(const Board& board, int depth, int time_ms, atomic<bool>& stop_flag) {
    if (!worker_pool) worker_pool = make_unique<WorkerPool>();
    worker_pool->resize(thread_count - 1, &get_tt());
    for (auto& w : worker_pool->workers) {
        w->start(board, depth, time_ms, &stop_flag);
    }
}

inline void Engine::stop_and_collect_workers(atomic<bool>& stop_flag) {
    stop_flag.store(true, memory_order_release);
    if (worker_pool) {
        for (auto& w : worker_pool->workers) {
            w->wait_done();
            nodes_searched += w->engine.nodes_searched;
        }
    }
}
