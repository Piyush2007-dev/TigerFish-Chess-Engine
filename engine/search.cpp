#include "rules.cpp"
#include "eval_lut.cpp"

#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstdint>

using namespace std;

enum TTFlag : uint8_t {
    TT_EXACT = 0,
    TT_ALPHA = 1,
    TT_BETA  = 2
};

struct TTEntry {
    uint64_t key = 0;       // 8 bytes: Full 64-bit Zobrist key
    uint32_t best_move = 0; // 4 bytes: Best move found
    int16_t  eval = 0;      // 2 bytes: Evaluation score
    int8_t   depth = -1;    // 1 byte : Search depth (-1 = empty)
    TTFlag   flag = TT_EXACT;// 1 byte : Bound flag
};

class TranspositionTable {
public:
    static const size_t TABLE_SIZE = 1048576; // 1,048,576 entries = 16 MB
    vector<TTEntry> table;

    TranspositionTable() : table(TABLE_SIZE) {}

    void clear() {
        fill(table.begin(), table.end(), TTEntry());
    }

    bool lookup(uint64_t key, int depth, int alpha, int beta, int& eval, uint32_t& tt_move) {
        size_t index = key & (TABLE_SIZE - 1);
        const TTEntry& entry = table[index];

        if (entry.key == key && entry.depth >= 0) {
            tt_move = entry.best_move;
            if (entry.depth >= depth) {
                if (entry.flag == TT_EXACT) {
                    eval = entry.eval;
                    return true;
                }
                if (entry.flag == TT_ALPHA && entry.eval <= alpha) {
                    eval = alpha;
                    return true;
                }
                if (entry.flag == TT_BETA && entry.eval >= beta) {
                    eval = beta;
                    return true;
                }
            }
        }
        return false;
    }

    void store(uint64_t key, int depth, int eval, TTFlag flag, uint32_t best_move) {
        size_t index = key & (TABLE_SIZE - 1);
        TTEntry& entry = table[index];
        
        if (entry.key != key || depth >= entry.depth) {
            entry.key = key;
            entry.eval = static_cast<int16_t>(eval);
            entry.depth = static_cast<int8_t>(depth);
            entry.flag = flag;
            entry.best_move = best_move;
        }
    }
};

class Engine {
public:
    TranspositionTable tt;

    static int mirror(int sq){
        int rank=sq/8;
        int file=sq%8;
        
        return (7-rank)*8+file;
    }

    int evaluate(Board& board){
        int eval=0;

        for(int sq=0;sq<64;sq++){
            Piece p=board.piece_on[sq];
            
            if(p==(Piece)0xF)continue;
            
            int base=PIECE_VALUE[p];
            int pst=0;
            bool is_white=(p<6);
            int table_sq=is_white?sq:mirror(sq);

            switch(p){
                case Piece::P:case Piece::p:pst=PAWN_PST[table_sq];break;
                case Piece::N:case Piece::n:pst=KNIGHT_PST[table_sq];break;
                case Piece::B:case Piece::b:pst=BISHOP_PST[table_sq];break;
                case Piece::R:case Piece::r:pst=ROOK_PST[table_sq];break;
                case Piece::Q:case Piece::q:pst=QUEEN_PST[table_sq];break;
                case Piece::K:case Piece::k:pst=KING_PST[table_sq];break;
                default:break;
            }

            if(is_white)eval+=base+pst;
            else eval-=base+pst;
        }

        return eval;
    }

    int minimax(Board& board,int depth,int alpha,int beta,bool maximizing){
        if(depth==0)return evaluate(board);

        int alpha_orig = alpha;
        int beta_orig = beta;

        uint32_t tt_move = 0;
        int tt_eval = 0;
        if(tt.lookup(board.zobrist_hash, depth, alpha, beta, tt_eval, tt_move)) {
            return tt_eval;
        }

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board,moves);

        if(moves.size()==0){
            if(is_in_check(board)){
                return maximizing?(-20000-depth):(20000+depth);
            }
            return 0;
        }

        // TT Move Ordering Boost
        if(tt_move != 0){
            for(int i = 0; i < moves.size(); i++){
                if(moves.move_list[i] == tt_move){
                    swap(moves.move_list[0], moves.move_list[i]);
                    break;
                }
            }
        }

        uint32_t best_move = 0;

        if(maximizing){
            int max_eval = -100000;
            
            for(int i=0;i<moves.size();i++){
                board.make_move(moves.move_list[i]);
                int eval = minimax(board,depth-1,alpha,beta,false);
                board.unmake_move();
                
                if(eval > max_eval){
                    max_eval = eval;
                    best_move = moves.move_list[i];
                }
                alpha = max(alpha, max_eval);
                if(beta<=alpha)break;
            }
            
            TTFlag flag = TT_EXACT;
            if(max_eval <= alpha_orig){
                flag = TT_ALPHA;
            } else if(max_eval >= beta){
                flag = TT_BETA;
            }
            tt.store(board.zobrist_hash, depth, max_eval, flag, best_move);

            return max_eval;
        }
        else{
            int min_eval = 100000;
            
            for(int i=0;i<moves.size();i++){
                board.make_move(moves.move_list[i]);
                int eval = minimax(board,depth-1,alpha,beta,true);
                board.unmake_move();
                
                if(eval < min_eval){
                    min_eval = eval;
                    best_move = moves.move_list[i];
                }
                beta = min(beta, min_eval);
                if(beta<=alpha)break;
            }
            
            TTFlag flag = TT_EXACT;
            if(min_eval >= beta_orig){
                flag = TT_BETA;
            } else if(min_eval <= alpha_orig){
                flag = TT_ALPHA;
            }
            tt.store(board.zobrist_hash, depth, min_eval, flag, best_move);

            return min_eval;
        }
    }

    uint32_t best_move(Board& board,int depth){
        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board,moves);

        if (moves.size() == 0) {
            return 0;
        }

        bool maximizing=(board.side_to_move==WHITE);

        if (maximizing) {
            int max_eval = -100000;
            for (int i = 0; i < moves.size(); i++) {
                board.make_move(moves.move_list[i]);
                int eval = minimax(board, depth - 1, -100000, 100000, false);
                board.unmake_move();

                moves.scores[i] = eval;
                max_eval = max(max_eval, eval);
            }

            vector<uint32_t> candidates;
            const int THRESHOLD = 50;

            for (int i = 0; i < moves.size(); i++) {
                int diff = abs(moves.scores[i] - max_eval);
                if (diff <= THRESHOLD) {
                    candidates.push_back(moves.move_list[i]);
                }
            }

            int limit = min(5, (int)candidates.size());
            
            static bool seeded = false;
            if (!seeded) {
                srand(time(NULL));
                seeded = true;
            }
            
            int pick = rand() % limit;
            return candidates[pick];
        } else {
            int min_eval = 100000;
            for (int i = 0; i < moves.size(); i++) {
                board.make_move(moves.move_list[i]);
                int eval = minimax(board, depth - 1, -100000, 100000, true);
                board.unmake_move();

                moves.scores[i] = eval;
                min_eval = min(min_eval, eval);
            }

            vector<uint32_t> candidates;
            const int THRESHOLD = 50;

            for (int i = 0; i < moves.size(); i++) {
                int diff = abs(moves.scores[i] - min_eval);
                if (diff <= THRESHOLD) {
                    candidates.push_back(moves.move_list[i]);
                }
            }

            int limit = min(5, (int)candidates.size());
            
            static bool seeded = false;
            if (!seeded) {
                srand(time(NULL));
                seeded = true;
            }
            
            int pick = rand() % limit;
            return candidates[pick];
        }
    }
};
