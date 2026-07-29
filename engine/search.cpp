#include "rules.cpp"
#include "eval_lut.cpp"

#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>

using namespace std;

class Engine {
public:
    static int mirror(int sq){
        int rank=sq/8;
        int file=sq%8;
        
        return (7-rank)*8+file;
    }

    int evaluate(Board& board){
        int score=0;

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

            if(is_white)score+=base+pst;
            else score-=base+pst;
        }

        return score;
    }

    int minimax(Board& board,int depth,int alpha,int beta,bool maximizing){
        if(depth==0)return evaluate(board);

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board,moves);

        if(moves.size()==0){
            if(is_in_check(board)){
                return maximizing?(-20000-depth):(20000+depth);
            }
            return 0;
        }

        if(maximizing){
            int best=-100000;
            
            for(int i=0;i<moves.size();i++){
                board.make_move(moves.move_list[i]);
                int score=minimax(board,depth-1,alpha,beta,false);
                board.unmake_move();
                
                if(score>best)best=score;
                if(best>alpha)alpha=best;
                if(beta<=alpha)break;
            }
            
            return best;
        }else{
            int best=100000;
            
            for(int i=0;i<moves.size();i++){
                board.make_move(moves.move_list[i]);
                int score=minimax(board,depth-1,alpha,beta,true);
                board.unmake_move();
                
                if(score<best)best=score;
                if(best<beta)beta=best;
                if(beta<=alpha)break;
            }
            
            return best;
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

        int best_score = maximizing ? -100000 : 100000;
        for(int i=0;i<moves.size();i++){
            board.make_move(moves.move_list[i]);
            int score=minimax(board,depth-1,-100000,100000,!maximizing);
            board.unmake_move();

            moves.scores[i] = score;
            if (maximizing) {
                if (score > best_score) best_score = score;
            } else {
                if (score < best_score) best_score = score;
            }
        }

        // Only choose randomly from moves that are within 50 centipawns (0.5 pawn value) of the best move.
        // This ensures the computer plays tactically strong moves while maintaining game variety.
        vector<uint32_t> candidates;
        const int THRESHOLD = 50;

        for (int i=0; i<moves.size(); i++) {
            int diff = abs(moves.scores[i] - best_score);
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
};
