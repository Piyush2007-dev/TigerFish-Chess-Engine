#!/usr/bin/env python3
"""Write search.cpp with all advanced techniques and fixed quiescence."""

SEARCH_CPP = r'''#include "rules.cpp"
#include "eval_lut.cpp"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <atomic>

using namespace std;

// ── Constants ────────────────────────────────────────────────────
static const int MATE_SCORE     = 100000;
static const int MATE_THRESHOLD = 90000;
static const int MAX_PLY        = 128;
static const int MAX_QSEARCH_NODES = 8000;  // Quiescence node limit per call

inline bool is_mate_score(int s) {
    return abs(s) >= MATE_THRESHOLD;
}

inline int mate_in_plies(int score) {
    if (score > 0) return MATE_SCORE - score;
    return -(MATE_SCORE + score);
}

inline int score_to_tt(int score, int ply) {
    if (score >= MATE_THRESHOLD)  return score + ply;
    if (score <= -MATE_THRESHOLD) return score - ply;
    return score;
}

inline int score_from_tt(int score, int ply) {
    if (score >= MATE_THRESHOLD)  return score - ply;
    if (score <= -MATE_THRESHOLD) return score + ply;
    return score;
}

// ── Transposition Table ──────────────────────────────────────────
enum TTFlag : uint8_t {
    TT_EXACT = 0,
    TT_ALPHA = 1,
    TT_BETA  = 2
};

struct TTEntry {
    uint64_t key      = 0;
    uint32_t best_move = 0;
    int16_t  eval      = 0;
    int8_t   depth     = -1;
    TTFlag   flag      = TT_EXACT;
};

class TranspositionTable {
public:
    static const size_t TABLE_SIZE = 2097152;
    vector<TTEntry> table;
    uint64_t hits   = 0;
    uint64_t misses = 0;

    TranspositionTable() : table(TABLE_SIZE) {}

    void clear() {
        fill(table.begin(), table.end(), TTEntry());
        hits = misses = 0;
    }

    bool probe(uint64_t key, int depth, int alpha, int beta,
               int& eval, uint32_t& tt_move, int ply)
    {
        TTEntry& e = table[key & (TABLE_SIZE - 1)];
        if (e.key == key) {
            tt_move = e.best_move;
            if (e.depth < depth) return false;
            int score = score_from_tt(e.eval, ply);
            if (e.flag == TT_EXACT) { eval = score; return true; }
            if (e.flag == TT_ALPHA && score <= alpha) { eval = alpha; return true; }
            if (e.flag == TT_BETA && score >= beta) { eval = beta; return true; }
        }
        return false;
    }

    void store(uint64_t key, int depth, int eval, TTFlag flag,
               uint32_t best_move, int ply)
    {
        TTEntry& e = table[key & (TABLE_SIZE - 1)];
        if (e.key != key || depth >= e.depth) {
            e.key       = key;
            e.best_move = best_move;
            e.eval      = (int16_t)score_to_tt(eval, ply);
            e.depth     = (int8_t)depth;
            e.flag      = flag;
        }
    }
};

// ── Null Move State ──────────────────────────────────────────────
struct NullMoveState {
    Color    side_to_move;
    uint8_t  castling_rights;
    uint8_t  en_passant;
    uint64_t zobrist_hash;
};

inline void make_null_move(Board& board, NullMoveState& state) {
    state.side_to_move    = board.side_to_move;
    state.castling_rights = board.castling_rights;
    state.en_passant      = board.en_passant;
    state.zobrist_hash    = board.zobrist_hash;

    board.zobrist_hash ^= ZOBRIST_EP[board.en_passant == 255 ? 64 : board.en_passant];
    board.en_passant = 255;
    board.zobrist_hash ^= ZOBRIST_EP[64];

    board.side_to_move = (board.side_to_move == WHITE) ? BLACK : WHITE;
    board.enemy_color  = (board.side_to_move == WHITE) ? BLACK : WHITE;
    board.zobrist_hash ^= ZOBRIST_SIDE;
}

inline void unmake_null_move(Board& board, const NullMoveState& state) {
    board.side_to_move    = state.side_to_move;
    board.enemy_color     = (board.side_to_move == WHITE) ? BLACK : WHITE;
    board.castling_rights = state.castling_rights;
    board.en_passant      = state.en_passant;
    board.zobrist_hash    = state.zobrist_hash;
}

// ── Engine ───────────────────────────────────────────────────────
class Engine {
public:
    TranspositionTable tt;

    uint64_t nodes_searched = 0;
    uint64_t nodes_limit    = 0;
    int      time_limit_ms  = 0;
    int      thread_count   = 1;

    const atomic<bool>* stop_flag = nullptr;
    bool stopped = false;

    int completed_depth = 0;
    vector<uint32_t> root_moves;
    vector<int>      root_scores;

    chrono::steady_clock::time_point search_start;

    uint32_t killer_moves[MAX_PLY][2];
    int      history[2][64][64];

    // ── Configuration ─────────────────────────────────────────────
    void configure_search(uint64_t node_limit, int time_limit, const atomic<bool>* flag) {
        nodes_limit   = node_limit;
        time_limit_ms = time_limit;
        stop_flag     = flag;
        stopped       = false;
    }

    bool should_stop() {
        if (stopped) return true;
        if (stop_flag && stop_flag->load()) { stopped = true; return true; }
        if (nodes_limit && nodes_searched >= nodes_limit) { stopped = true; return true; }
        if (time_limit_ms) {
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - search_start).count();
            if (elapsed >= time_limit_ms) { stopped = true; return true; }
        }
        return false;
    }

    // ── Evaluation ────────────────────────────────────────────────
    static int mirror(int sq) {
        return (7 - sq / 8) * 8 + sq % 8;
    }

    int evaluate(Board& board) {
        int eval = 0;
        for (int sq = 0; sq < 64; sq++) {
            Piece piece = board.piece_on[sq];
            if (piece == (Piece)0xF) continue;
            int value = PIECE_VALUE[piece];
            int table_sq = piece < 6 ? sq : mirror(sq);
            switch (piece) {
                case Piece::P: case Piece::p: value += PAWN_PST[table_sq];   break;
                case Piece::N: case Piece::n: value += KNIGHT_PST[table_sq]; break;
                case Piece::B: case Piece::b: value += BISHOP_PST[table_sq]; break;
                case Piece::R: case Piece::r: value += ROOK_PST[table_sq];   break;
                case Piece::Q: case Piece::q: value += QUEEN_PST[table_sq];  break;
                case Piece::K: case Piece::k: value += KING_PST[table_sq];   break;
                default: break;
            }
            eval += piece < 6 ? value : -value;
        }
        return eval;
    }

    // ── Move Ordering Helpers ─────────────────────────────────────
    void promote_move(MoveList& moves, uint32_t m) {
        if (!m) return;
        for (int i = 0; i < moves.size(); i++) {
            if (moves.move_list[i] == m) {
                swap(moves.move_list[0], moves.move_list[i]);
                return;
            }
        }
    }

    void score_quiets(MoveList& moves, int ply, bool side) {
        for (int i = 0; i < moves.size(); i++) {
            uint32_t m = moves.move_list[i];
            if (move_is_capture(m) || move_is_promotion(m)) continue;
            int from = move_from(m);
            int to   = move_to(m);
            int score = history[side][from][to];
            if (ply < MAX_PLY) {
                if (m == killer_moves[ply][0]) score += 9000000;
                else if (m == killer_moves[ply][1]) score += 8000000;
            }
            moves.scores[i] = score;
        }
    }

    void sort_quiets(MoveList& moves) {
        for (int i = 1; i < moves.size(); i++) {
            if (move_is_capture(moves.move_list[i]) || move_is_promotion(moves.move_list[i]))
                continue;
            uint32_t key_move  = moves.move_list[i];
            int      key_score = moves.scores[i];
            int j = i - 1;
            while (j >= 0 &&
                   !(move_is_capture(moves.move_list[j]) || move_is_promotion(moves.move_list[j])) &&
                   moves.scores[j] < key_score)
            {
                moves.move_list[j + 1] = moves.move_list[j];
                moves.scores[j + 1]    = moves.scores[j];
                j--;
            }
            moves.move_list[j + 1] = key_move;
            moves.scores[j + 1]    = key_score;
        }
    }

    // ── Quiescence Search ─────────────────────────────────────────
    int quiescence(Board& board, int alpha, int beta, bool side) {
        nodes_searched++;
        if (should_stop()) return evaluate(board);

        int stand_pat = evaluate(board);

        if (side == 0) {
            if (stand_pat >= beta) return beta;
            if (stand_pat > alpha) alpha = stand_pat;
        } else {
            if (stand_pat <= beta) return beta;
            if (stand_pat < alpha) alpha = stand_pat;
        }

        MoveList moves;
        MoveGenerator gen;
        gen.generate_moves(board, moves);

        for (int i = 0; i < moves.size(); i++) {
            uint32_t m = moves.move_list[i];
            if (!move_is_capture(m) && !move_is_promotion(m)) continue;

            // Delta pruning: skip captures that can't possibly raise alpha
            if (!move_is_promotion(m)) {
                int victim_val = PIECE_VALUE[(int)move_captured_piece(m)];
                int attacker_val = PIECE_VALUE[(int)move_piece(m)];
                int delta = victim_val - attacker_val + 200; // small margin

                if (side == 0 && stand_pat + delta < alpha && victim_val < 800) continue;
                if (side == 1 && stand_pat - delta > beta && victim_val < 800) continue;
            }

            board.make_move(m);
            int score = quiescence(board, beta, alpha, !side);
            board.unmake_move();
            score = -score;

            if (side == 0) {
                if (score >= beta) return beta;
                if (score > alpha) alpha = score;
            } else {
                if (score <= beta) return beta;
                if (score < alpha) alpha = score;
            }
        }
        return alpha;
    }

    // ── Negamax Search ────────────────────────────────────────────
    int search(Board& board, int depth, int alpha, int beta,
               int ply, bool side, bool do_null, bool is_pv)
    {
        nodes_searched++;
        if (should_stop()) return 0;
        if (ply >= MAX_PLY) return evaluate(board);

        if (ply > 0 && (board.is_repetition(3) || board.halfmove_clock >= 100))
            return 0;

        // TT probe
        int alpha_orig = alpha;
        uint32_t tt_move = 0;
        int tt_score = 0;
        if (tt.probe(board.zobrist_hash, depth, alpha, beta, tt_score, tt_move, ply)) {
            if (ply > 0) return tt_score;
        }

        bool in_check = is_in_check(board);

        if (depth <= 0) {
            if (in_check) {
                depth = 1;
            } else {
                return quiescence(board, alpha, beta, side);
            }
        }

        // Null move pruning
        if (do_null && !in_check && ply > 0 && depth >= 3 && !is_pv) {
            int pieces = 0;
            for (int sq = 0; sq < 64; sq++) {
                Piece p = board.piece_on[sq];
                if (p != (Piece)0xF && p != Piece::K && p != Piece::k) pieces++;
            }
            if (pieces >= 3) {
                int R = 2 + depth / 4;
                NullMoveState nms;
                make_null_move(board, nms);
                int score = -search(board, depth - 1 - R, -beta, -beta + 1,
                                    ply + 1, !side, false, false);
                unmake_null_move(board, nms);
                if (score >= beta) return beta;
            }
        }

        // Generate & order moves
        MoveList moves;
        MoveGenerator gen;
        gen.generate_moves(board, moves);

        if (moves.size() == 0) {
            if (in_check) return -MATE_SCORE + ply;
            return 0;
        }

        promote_move(moves, tt_move);
        score_quiets(moves, ply, side);
        sort_quiets(moves);

        uint32_t best_move = 0;
        int best = -MATE_SCORE;
        int moves_searched = 0;

        for (int i = 0; i < moves.size() && !should_stop(); i++) {
            uint32_t m = moves.move_list[i];
            bool is_quiet = !move_is_capture(m) && !move_is_promotion(m);

            board.make_move(m);
            int score;

            // LMR
            if (moves_searched >= 4 && depth >= 3 && is_quiet && !in_check) {
                int reduction = 1 + (moves_searched >= 8 ? 1 : 0);
                if (is_pv) reduction--;
                if (ply < MAX_PLY && (m == killer_moves[ply][0] || m == killer_moves[ply][1]))
                    reduction--;
                reduction = max(1, min(reduction, depth - 2));
                score = -search(board, depth - 1 - reduction, -alpha - 1, -alpha,
                                ply + 1, !side, true, false);
                if (score > alpha) {
                    score = -search(board, depth - 1, -beta, -alpha,
                                    ply + 1, !side, true, is_pv);
                }
            }
            // PVS
            else if (moves_searched > 0 && is_pv) {
                score = -search(board, depth - 1, -alpha - 1, -alpha,
                                ply + 1, !side, true, false);
                if (score > alpha && score < beta) {
                    score = -search(board, depth - 1, -beta, -alpha,
                                    ply + 1, !side, true, true);
                }
            }
            else {
                score = -search(board, depth - 1, -beta, -alpha,
                                ply + 1, !side, true, is_pv);
            }

            board.unmake_move();
            if (should_stop()) return 0;

            if (score > best) {
                best = score;
                best_move = m;
            }
            if (score > alpha) alpha = score;

            if (alpha >= beta) {
                if (is_quiet) {
                    if (ply < MAX_PLY) {
                        if (m != killer_moves[ply][0]) {
                            killer_moves[ply][1] = killer_moves[ply][0];
                            killer_moves[ply][0] = m;
                        }
                    }
                    int bonus = depth * depth;
                    history[side][move_from(m)][move_to(m)] += bonus;
                    if (history[side][move_from(m)][move_to(m)] > 1000000) {
                        for (int f = 0; f < 64; f++)
                            for (int t = 0; t < 64; t++)
                                history[side][f][t] /= 2;
                    }
                }
                break;
            }
            moves_searched++;
        }

        // TT store
        if (!should_stop() && best_move != 0) {
            TTFlag flag;
            if (best <= alpha_orig) flag = TT_ALPHA;
            else if (best >= beta)  flag = TT_BETA;
            else                    flag = TT_EXACT;
            tt.store(board.zobrist_hash, depth, best, flag, best_move, ply);
        }
        return best;
    }

    // ── Root Search ───────────────────────────────────────────────
    uint32_t search_depth(Board& board, int depth) {
        MoveList moves;
        MoveGenerator gen;
        gen.generate_moves(board, moves);
        if (moves.size() == 0) return 0;

        bool side = board.side_to_move == WHITE ? 0 : 1;

        if (!root_moves.empty()) promote_move(moves, root_moves[0]);

        int alpha = -MATE_SCORE;
        int beta  =  MATE_SCORE;

        if (depth >= 3 && root_scores.size() > 0 && abs(root_scores[0]) < MATE_THRESHOLD) {
            int prev = root_scores[0];
            alpha = prev - 50;
            beta  = prev + 50;
        }

        bool first_root_move = true;

        while (true) {
            for (int i = 0; i < moves.size(); i++) {
                board.make_move(moves.move_list[i]);
                int score;
                if (first_root_move) {
                    score = -search(board, depth - 1, -beta, -alpha, 1, !side, true, true);
                    first_root_move = false;
                } else {
                    score = -search(board, depth - 1, -alpha - 1, -alpha, 1, !side, true, false);
                    if (score > alpha && score < beta) {
                        score = -search(board, depth - 1, -beta, -alpha, 1, !side, true, true);
                    }
                }
                board.unmake_move();
                moves.scores[i] = score;
                if (score > alpha) alpha = score;
                if (alpha >= beta) break;
            }

            if (should_stop()) break;

            if (depth >= 3 && abs(alpha) < MATE_THRESHOLD) {
                bool fail_low = (alpha <= (root_scores.empty() ? -MATE_SCORE : root_scores[0] - 50));
                bool fail_high = false;
                for (int i = 0; i < moves.size(); i++) {
                    if (moves.scores[i] >= beta) { fail_high = true; break; }
                }
                if (!fail_low && !fail_high) break;
                alpha = -MATE_SCORE;
                beta  =  MATE_SCORE;
                continue;
            }
            break;
        }

        if (should_stop()) return root_moves.empty() ? 0 : root_moves[0];

        vector<int> order(moves.size());
        for (int i = 0; i < (int)moves.size(); i++) order[i] = i;
        sort(order.begin(), order.end(), [&](int a, int b) {
            return moves.scores[a] > moves.scores[b];
        });

        root_moves.clear();
        root_scores.clear();
        for (int idx : order) {
            root_moves.push_back(moves.move_list[idx]);
            root_scores.push_back(moves.scores[idx]);
        }
        return root_moves[0];
    }

    // ── Iterative Deepening ───────────────────────────────────────
    uint32_t best_move(Board& board, int depth) {
        nodes_searched = 0;
        stopped        = false;
        completed_depth = 0;
        root_moves.clear();
        root_scores.clear();
        search_start = chrono::steady_clock::now();
        memset(killer_moves, 0, sizeof(killer_moves));
        memset(history, 0, sizeof(history));

        uint32_t result = 0;
        for (int current = 1; current <= max(1, depth); current++) {
            result = search_depth(board, current);
            if (stopped) break;
            completed_depth = current;
        }
        return result;
    }
};
'''

with open('search.cpp', 'w', encoding='utf-8', newline='\n') as f:
    f.write(SEARCH_CPP)

print(f"Written {len(SEARCH_CPP)} bytes to search.cpp")
