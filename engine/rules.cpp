#pragma once
#include "board.cpp"

struct MoveList{
    Move move_list[218];
    int scores[218];
    int count=0;
    
    void clear(){
        count=0;
    }
    void push(Move m, int score=0){
        scores[count]=score;
        move_list[count++]=m;
    }
    int size(){
        return count;
    }
};

// rook_attacks — O(1) lookup: masks occ to relevant squares, hashes with magic, returns precomputed attack bitboard
inline uint64_t rook_attacks(int sq, uint64_t occ){
    occ&=ROOK_MASK[sq];
    return ROOK_ATTACKS[sq][(occ*ROOK_MAGICS[sq])>>ROOK_SHIFT[sq]];
}

// bishop_attacks — same as rook_attacks but for diagonals
inline uint64_t bishop_attacks(int sq, uint64_t occ){
    occ&=BISHOP_MASK[sq];
    return BISHOP_ATTACKS[sq][(occ*BISHOP_MAGICS[sq])>>BISHOP_SHIFT[sq]];
}

// queen_attacks — queen = rook + bishop; OR both attack sets together
inline uint64_t queen_attacks(int sq, uint64_t occ){
    return rook_attacks(sq,occ)|bishop_attacks(sq,occ);
}

struct PinInfo{
    uint64_t pinned=0;
    uint64_t pin_mask[64];
    
    PinInfo(){
        for(int i=0;i<64;i++){
            pin_mask[i]=~0ULL;
        }
    }
};

PinInfo find_pins(Board &board){
    PinInfo pins;
    
    Piece king=(board.side_to_move==WHITE)?Piece::K:Piece::k;
    int king_sq=lsb_index(board.bitboards[king]);
    uint64_t friendly=board.occupancy[board.side_to_move];
    int opposite_dir[8]={1,0,3,2,7,6,5,4};
    
    for(int dir=0;dir<8;dir++){
        uint64_t ray=board.get_ray(dir,king_sq);
        uint64_t blockers=ray&board.occupancy[2];

        if(!blockers)continue;
        
        int first=(dir%2==0)?lsb_index(blockers):msb_index(blockers);
        uint64_t first_bb=1ULL<<first;

        if(!(first_bb&friendly))continue;
        blockers&=~first_bb;

        if(!blockers)continue;

        int second=(dir%2==0)?lsb_index(blockers):msb_index(blockers);
        Piece p=board.piece_on[second];

        if(((p<=Piece::K)?WHITE:BLACK)==board.side_to_move)continue;
        
        bool slider=false;
        if(dir<4)
            slider=(p==Piece::r||p==Piece::q||p==Piece::R||p==Piece::Q);
        else
            slider=(p==Piece::b||p==Piece::q||p==Piece::B||p==Piece::Q);
            
        if(!slider)continue;
        
        pins.pinned|=first_bb;
        pins.pin_mask[first]=ray|board.get_ray(opposite_dir[dir],king_sq);
    }
    
    return pins;
}

bool ep_exposes_king(Board &board,int from_sq,int ep_sq){
    Color mover=board.side_to_move;
    Color enemy=(mover==WHITE)?BLACK:WHITE;
    Piece king_pc=(mover==WHITE)?Piece::K:Piece::k;
    
    int king_sq=lsb_index(board.bitboards[king_pc]);
    int captured_sq=(mover==WHITE)?ep_sq-8:ep_sq+8;
    
    uint64_t occ=board.occupancy[2];
    occ&=~(1ULL<<from_sq);
    occ&=~(1ULL<<captured_sq);
    occ|=(1ULL<<ep_sq);
    
    uint64_t enemy_occ=board.occupancy[enemy]&~(1ULL<<captured_sq);
    
    for(int dir=0;dir<4;dir++){
        uint64_t ray=board.get_ray(dir,king_sq);
        uint64_t blockers=ray&occ;
        
        if(!blockers)continue;
        
        int blocker_sq=(dir%2==0)?lsb_index(blockers):msb_index(blockers);
        if(!((enemy_occ)&(1ULL<<blocker_sq)))continue;
        
        Piece p=board.piece_on[blocker_sq];
        if(p==Piece::r||p==Piece::R||p==Piece::q||p==Piece::Q)
            return true;
    }
    
    for(int dir=4;dir<8;dir++){
        uint64_t ray=board.get_ray(dir,king_sq);
        uint64_t blockers=ray&occ;
        
        if(!blockers)continue;
        
        int blocker_sq=(dir%2==0)?lsb_index(blockers):msb_index(blockers);
        if(!((enemy_occ)&(1ULL<<blocker_sq)))continue;
        
        Piece p=board.piece_on[blocker_sq];
        if(p==Piece::b||p==Piece::B||p==Piece::q||p==Piece::Q)
            return true;
    }
    
    return false;
}

pair<uint64_t,uint64_t> knight_attacks(uint64_t lsb,uint64_t all_occ,uint64_t enemy_occ){
    uint64_t l1=(lsb&NOT_H_FILE)<<1;
    uint64_t l2=(lsb&NOT_GH_FILE)<<2;
    uint64_t r1=(lsb&NOT_A_FILE)>>1;
    uint64_t r2=(lsb&NOT_AB_FILE)>>2;
    
    uint64_t h1=l1|r1;
    uint64_t h2=l2|r2;
    
    uint64_t moves_mask=(h1<<16)|(h1>>16)|(h2<<8)|(h2>>8);
    uint64_t captures=moves_mask&enemy_occ;
    moves_mask&=~all_occ;
    
    return {moves_mask,captures};
}

Color piece_color(Piece p){
    return (p<=Piece::K)?WHITE:BLACK;
}


uint64_t append_sliding_moves(MoveList &moves,Board &board,Piece piece,int dir_start,int dir_end,const PinInfo &pins,uint64_t legal_mask=~0ULL){
    Color enemy=(piece_color(piece)==WHITE)?BLACK:WHITE;
    
    uint64_t all_occ=board.occupancy[2];
    uint64_t enemy_occ=board.occupancy[enemy];
    uint64_t own_occ=board.occupancy[piece_color(piece)];
    
    uint64_t attack_mask=0;
    uint64_t mask=board.bitboards[piece];
    
    while(mask){
        int from_sq=lsb_index(mask);
        uint64_t pmask=pins.pin_mask[from_sq];
        uint64_t attacks;
        
        if(dir_start==0&&dir_end==4)
            attacks=rook_attacks(from_sq,all_occ);
        else if(dir_start==4&&dir_end==8)
            attacks=bishop_attacks(from_sq,all_occ);
        else
            attacks=queen_attacks(from_sq,all_occ);
            
        attacks&=~own_occ;
        attack_mask|=attacks;
        
        uint64_t legal=attacks&legal_mask&pmask;
        uint64_t quiets=legal&~all_occ;
        uint64_t captures=legal&enemy_occ;
        
        while(quiets){
            int to_sq=pop_lsb(quiets);
            Move m;
            m.value=Move::pack(from_sq,to_sq,piece);
            moves.push(m);
        }
        
        while(captures){
            int to_sq=pop_lsb(captures);
            Move m;
            m.value=Move::pack(from_sq,to_sq,piece,(uint32_t)board.piece_on[to_sq]);
            moves.push(m);
        }
        mask&=mask-1;
    }
    
    return attack_mask;
}

uint64_t append_knight_moves(MoveList &moves,Board &board,Piece piece,const PinInfo &pins,uint64_t legal_mask=~0ULL){
    uint64_t all_occ=board.occupancy[2];
    Color enemy=(piece_color(piece)==WHITE)?BLACK:WHITE;
    uint64_t enemy_occ=board.occupancy[enemy];
    
    uint64_t attack_mask=0;
    uint64_t mask=board.bitboards[piece];
    
    while(mask){
        uint64_t lsb=mask&(0ULL-mask);
        int from_sq=lsb_index(lsb);
        
        if(pins.pinned&lsb){
            mask&=mask-1;
            continue;
        }
        
        auto pr=knight_attacks(lsb,all_occ,enemy_occ);
        uint64_t moves_mask=pr.first;
        uint64_t captures=pr.second;
        uint64_t piece_attack_mask=moves_mask|captures;
        
        moves_mask&=legal_mask;
        captures&=legal_mask;
        
        while(moves_mask){
            int to_sq=pop_lsb(moves_mask);
            Move m;
            m.value=Move::pack(from_sq,to_sq,piece);
            moves.push(m);
        }
        
        while(captures){
            int to_sq=pop_lsb(captures);
            Move m;
            m.value=Move::pack(from_sq,to_sq,piece,(uint32_t)board.piece_on[to_sq]);
            moves.push(m);
        }
        
        attack_mask|=piece_attack_mask;
        mask&=mask-1;
    }
    
    return attack_mask;
}

class MoveGenerator{
public:
    void PawnMoves(Board &board,MoveList &moves,const PinInfo &pins,uint64_t legal_mask=~0ULL){
        const bool white=board.side_to_move==WHITE;
        Piece pawn_piece=white?Piece::P:Piece::p;
        Piece enemy_pawn=white?Piece::p:Piece::P;
        Color enemy=white?BLACK:WHITE;
        
        int push=white?8:-8;
        int left_side_shift=white?9:-9;
        int right_side_shift=white?7:-7;
        
        uint64_t start_rank=white?RANK_2:RANK_7;
        uint64_t promo_rank=white?RANK_8:RANK_1;
        
        Piece queen_promo=white?Piece::Q:Piece::q;
        Piece rook_promo=white?Piece::R:Piece::r;
        Piece bishop_promo=white?Piece::B:Piece::b;
        Piece knight_promo=white?Piece::N:Piece::n;
        Piece promo_pieces[4]={queen_promo,rook_promo,bishop_promo,knight_promo};
        
        uint64_t pawn=board.bitboards[pawn_piece];
        uint64_t single_push_target=white?((pawn<<8)&~board.occupancy[2]):((pawn>>8)&~board.occupancy[2]);
        uint64_t rank_2_pawns=pawn&start_rank;
        uint64_t first_step=white?((rank_2_pawns<<8)&~board.occupancy[2]):((rank_2_pawns>>8)&~board.occupancy[2]);
        uint64_t double_push_targets=white?((first_step<<8)&~board.occupancy[2]):((first_step>>8)&~board.occupancy[2]);
        
        uint64_t left_side;
        uint64_t right_side;
        
        if(white){
            left_side=(pawn&NOT_H_FILE)<<9;
            right_side=(pawn&NOT_A_FILE)<<7;
        }else{
            left_side=(pawn&NOT_A_FILE)>>9;
            right_side=(pawn&NOT_H_FILE)>>7;
        }
        
        uint64_t captures=(left_side|right_side)&board.occupancy[enemy];
        captures&=legal_mask;
        
        uint64_t mask=single_push_target|double_push_targets;
        mask&=legal_mask;
        mask&=~promo_rank;
        captures&=~promo_rank;
        
        while(mask){
            uint64_t lsb=mask&(0ULL-mask);
            int to_square=lsb_index(lsb);
            int from_square=((double_push_targets>>to_square)&1)?to_square-2*push:to_square-push;
            uint64_t pin_mask=pins.pin_mask[from_square];
            
            if(!(pin_mask&(1ULL<<to_square))){
                mask&=mask-1;
                continue;
            }
            
            bool dbl=(double_push_targets>>to_square)&1;
            Move m;
            m.value=Move::pack(from_square,to_square,pawn_piece,0xFu,0xFu,false,false,dbl);
            moves.push(m);
            mask&=mask-1;
        }
        
        {
            uint64_t left_caps=left_side&board.occupancy[enemy]&legal_mask&~promo_rank;
            
            while(left_caps){
                int to_square=pop_lsb(left_caps);
                int from_square=to_square-left_side_shift;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                Move m;
                m.value=Move::pack(from_square,to_square,pawn_piece,(uint32_t)board.piece_on[to_square]);
                moves.push(m);
            }
        }
        
        {
            uint64_t right_caps=right_side&board.occupancy[enemy]&legal_mask&~promo_rank;
            
            while(right_caps){
                int to_square=pop_lsb(right_caps);
                int from_square=to_square-right_side_shift;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                Move m;
                m.value=Move::pack(from_square,to_square,pawn_piece,(uint32_t)board.piece_on[to_square]);
                moves.push(m);
            }
        }
        
        {
            uint64_t promo_push=single_push_target&promo_rank&legal_mask;
            
            while(promo_push){
                int to_square=pop_lsb(promo_push);
                int from_square=to_square-push;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                for(int i=0;i<4;i++){
                    Move m;
                    m.value=Move::pack(from_square,to_square,pawn_piece,0xFu,(uint32_t)promo_pieces[i]);
                    moves.push(m);
                }
            }
        }
        
        {
            uint64_t left_promo=left_side&board.occupancy[enemy]&promo_rank&legal_mask;
            
            while(left_promo){
                int to_square=pop_lsb(left_promo);
                int from_square=to_square-left_side_shift;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                for(int i=0;i<4;i++){
                    Move m;
                    m.value=Move::pack(from_square,to_square,pawn_piece,(uint32_t)board.piece_on[to_square],(uint32_t)promo_pieces[i]);
                    moves.push(m);
                }
            }
        }
        
        {
            uint64_t right_promo=right_side&board.occupancy[enemy]&promo_rank&legal_mask;
            
            while(right_promo){
                int to_square=pop_lsb(right_promo);
                int from_square=to_square-right_side_shift;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                for(int i=0;i<4;i++){
                    Move m;
                    m.value=Move::pack(from_square,to_square,pawn_piece,(uint32_t)board.piece_on[to_square],(uint32_t)promo_pieces[i]);
                    moves.push(m);
                }
            }
        }
        
        if(board.en_passant!=255){
            uint64_t ep_bb=1ULL<<board.en_passant;
            uint64_t ep_pawns;
            
            if(white){
                ep_pawns=board.bitboards[pawn_piece]&(((ep_bb&NOT_H_FILE)>>7)|((ep_bb&NOT_A_FILE)>>9));
            }else{
                ep_pawns=board.bitboards[pawn_piece]&(((ep_bb&NOT_A_FILE)<<7)|((ep_bb&NOT_H_FILE)<<9));
            }
            
            while(ep_pawns){
                int from_sq=pop_lsb(ep_pawns);
                uint64_t pin_mask=pins.pin_mask[from_sq];
                
                if(!(pin_mask&(1ULL<<board.en_passant)))continue;
                
                if(!ep_exposes_king(board,from_sq,board.en_passant)){
                    Move m;
                    m.value=Move::pack(from_sq,board.en_passant,pawn_piece,(uint32_t)enemy_pawn,0xFu,false,true);
                    moves.push(m);
                }
            }
        }
    }

    void KnightMoves(Board &board,MoveList &moves,const PinInfo &pins,uint64_t legal_mask=~0ULL){
        Piece knight=(board.side_to_move==WHITE)?Piece::N:Piece::n;
        append_knight_moves(moves,board,knight,pins,legal_mask);
    }
    
    void BishopMoves(Board &board,MoveList &moves,const PinInfo &pins,uint64_t legal_mask=~0ULL){
        Piece bishop=(board.side_to_move==WHITE)?Piece::B:Piece::b;
        append_sliding_moves(moves,board,bishop,4,8,pins,legal_mask);
    }
    
    void RookMoves(Board &board,MoveList &moves,const PinInfo &pins,uint64_t legal_mask=~0ULL){
        Piece rook=(board.side_to_move==WHITE)?Piece::R:Piece::r;
        append_sliding_moves(moves,board,rook,0,4,pins,legal_mask);
    }
    
    void QueenMoves(Board &board,MoveList &moves,const PinInfo &pins,uint64_t legal_mask=~0ULL){
        Piece queen=(board.side_to_move==WHITE)?Piece::Q:Piece::q;
        append_sliding_moves(moves,board,queen,0,8,pins,legal_mask);
    }

    void KingMoves(Board &board,MoveList &moves,uint64_t enemy_attack_mask){
        Piece king_piece=(board.side_to_move==WHITE)?Piece::K:Piece::k;
        Color enemy=(board.side_to_move==WHITE)?BLACK:WHITE;
        
        uint64_t king_bb=board.bitboards[king_piece];
        uint64_t lsb=king_bb&(0ULL-king_bb);
        int from_square=lsb_index(lsb);
        
        uint64_t east=(lsb&NOT_H_FILE)<<1;
        uint64_t west=(lsb&NOT_A_FILE)>>1;
        uint64_t r=lsb|east|west;
        
        uint64_t raw_mask=(((r<<8|r|r>>8))&~lsb)&~enemy_attack_mask;
        uint64_t captures_mask=raw_mask&board.occupancy[enemy];
        uint64_t move_mask=raw_mask&~board.occupancy[2];
        
        while(move_mask){
            int to_square=pop_lsb(move_mask);
            Move m;
            m.value=Move::pack(from_square,to_square,king_piece);
            moves.push(m);
        }
        
        while(captures_mask){
            int to_square=pop_lsb(captures_mask);
            Move m;
            m.value=Move::pack(from_square,to_square,king_piece,(uint32_t)board.piece_on[to_square]);
            moves.push(m);
        }
        
        if(board.side_to_move==WHITE){
            if((board.castling_rights&WHITE_KING_SIDE)&&
               !(board.occupancy[2]&((1ULL<<5)|(1ULL<<6)))&&
               !(enemy_attack_mask&((1ULL<<4)|(1ULL<<5)|(1ULL<<6)))){
                Move m;
                m.value=Move::pack(4,6,king_piece,0xFu,0xFu,true);
                moves.push(m);
            }
            if((board.castling_rights&WHITE_QUEEN_SIDE)&&
               !(board.occupancy[2]&((1ULL<<1)|(1ULL<<2)|(1ULL<<3)))&&
               !(enemy_attack_mask&((1ULL<<2)|(1ULL<<3)|(1ULL<<4)))){
                Move m;
                m.value=Move::pack(4,2,king_piece,0xFu,0xFu,true);
                moves.push(m);
            }
        }else{
            if((board.castling_rights&BLACK_KING_SIDE)&&
               !(board.occupancy[2]&((1ULL<<61)|(1ULL<<62)))&&
               !(enemy_attack_mask&((1ULL<<60)|(1ULL<<61)|(1ULL<<62)))){
                Move m;
                m.value=Move::pack(60,62,king_piece,0xFu,0xFu,true);
                moves.push(m);
            }
            if((board.castling_rights&BLACK_QUEEN_SIDE)&&
               !(board.occupancy[2]&((1ULL<<57)|(1ULL<<58)|(1ULL<<59)))&&
               !(enemy_attack_mask&((1ULL<<58)|(1ULL<<59)|(1ULL<<60)))){
                Move m;
                m.value=Move::pack(60,58,king_piece,0xFu,0xFu,true);
                moves.push(m);
            }
        }
    }

    uint64_t pawnatk(Board &board,uint64_t &checkers){
        Piece pawn;
        Piece enemy_king;
        
        if(board.side_to_move==WHITE){
            pawn=Piece::p;
            enemy_king=Piece::K;
        }else{
            pawn=Piece::P;
            enemy_king=Piece::k;
        }
        
        uint64_t mask=board.bitboards[pawn];
        uint64_t attack_mask=0;
        
        while(mask){
            uint64_t lsb=mask&-mask;
            uint64_t left_side;
            uint64_t right_side;
            
            if(pawn==Piece::p){
                left_side=(lsb&NOT_A_FILE)>>9;
                right_side=(lsb&NOT_H_FILE)>>7;
            }else{
                left_side=(lsb&NOT_A_FILE)<<7;
                right_side=(lsb&NOT_H_FILE)<<9;
            }
            
            uint64_t attacks=left_side|right_side;
            if(attacks&board.bitboards[enemy_king]){
                checkers|=lsb;
            }
            
            attack_mask|=attacks;
            mask&=mask-1;
        }
        
        return attack_mask;
    }

    uint64_t knightatk(Board &board,uint64_t &checkers){
        Piece knight;
        Piece enemy_king;
        
        if(board.side_to_move==WHITE){
            knight=Piece::n;
            enemy_king=Piece::K;
        }else{
            knight=Piece::N;
            enemy_king=Piece::k;
        }
        
        uint64_t mask=board.bitboards[knight];
        uint64_t attack_mask=0;
        
        while(mask){
            uint64_t lsb=mask&-mask;
            uint64_t l1=(lsb&NOT_H_FILE)<<1;
            uint64_t l2=(lsb&NOT_GH_FILE)<<2;
            uint64_t r1=(lsb&NOT_A_FILE)>>1;
            uint64_t r2=(lsb&NOT_AB_FILE)>>2;
            
            uint64_t h1=l1|r1;
            uint64_t h2=l2|r2;
            
            uint64_t attacks=(h1<<16)|(h1>>16)|(h2<<8)|(h2>>8);
            if(attacks&board.bitboards[enemy_king]){
                checkers|=lsb;
            }
            
            attack_mask|=attacks;
            mask&=mask-1;
        }
        
        return attack_mask;
    }

    uint64_t sliding_atks(Board &board,Piece piece,int dir_start,int dir_end,uint64_t &checkers){
        Color enemy=(piece_color(piece)==WHITE)?BLACK:WHITE;
        Piece king=(enemy==WHITE)?Piece::K:Piece::k;
        uint64_t occ=board.occupancy[2]^board.bitboards[king];
        
        uint64_t attack_mask=0;
        uint64_t mask=board.bitboards[piece];
        
        while(mask){
            int from_sq=lsb_index(mask);
            uint64_t attacks;
            
            if(dir_start==0&&dir_end==4)
                attacks=rook_attacks(from_sq,occ);
            else if(dir_start==4&&dir_end==8)
                attacks=bishop_attacks(from_sq,occ);
            else
                attacks=queen_attacks(from_sq,occ);
                
            if(attacks&board.bitboards[king])
                checkers|=(1ULL<<from_sq);
                
            attack_mask|=attacks;
            mask&=mask-1;
        }
        
        return attack_mask;
    }
    
    uint64_t kingatk(Board &board){
        Piece king=(board.side_to_move==WHITE)?Piece::k:Piece::K;
        uint64_t king_bb=board.bitboards[king];
        
        uint64_t east=(king_bb&NOT_H_FILE)<<1;
        uint64_t west=(king_bb&NOT_A_FILE)>>1;
        uint64_t r=king_bb|east|west;
        
        uint64_t attack_mask=((r<<8)|r|(r>>8))&~king_bb;
        return attack_mask;
    }

    void generate_moves(Board &board,MoveList &moves){
        PinInfo pins=find_pins(board);
        uint64_t attack_mask=0;
        uint64_t checkers=0;
        
        Piece bishop=board.side_to_move==WHITE?Piece::b:Piece::B;
        Piece rook=board.side_to_move==WHITE?Piece::r:Piece::R;
        Piece queen=board.side_to_move==WHITE?Piece::q:Piece::Q;
        
        attack_mask|=pawnatk(board,checkers);
        attack_mask|=knightatk(board,checkers);
        attack_mask|=sliding_atks(board,bishop,4,8,checkers);
        attack_mask|=sliding_atks(board,rook,0,4,checkers);
        attack_mask|=sliding_atks(board,queen,0,8,checkers);
        attack_mask|=kingatk(board);
        
        int check_count=__builtin_popcountll(checkers);
        
        if(check_count==0){
            uint64_t legal_mask=~0ULL;
            
            KingMoves(board,moves,attack_mask);
            PawnMoves(board,moves,pins,legal_mask);
            KnightMoves(board,moves,pins,legal_mask);
            BishopMoves(board,moves,pins,legal_mask);
            RookMoves(board,moves,pins,legal_mask);
            QueenMoves(board,moves,pins,legal_mask);
            
        }else if(check_count==1){
            Piece king_piece=board.side_to_move==WHITE?Piece::K:Piece::k;
            Piece enemy_pawn=board.side_to_move==WHITE?Piece::p:Piece::P;
            Piece enemy_knight=board.side_to_move==WHITE?Piece::n:Piece::N;
            
            int checker_square=lsb_index(checkers);
            Piece checker_piece=board.piece_on[checker_square];
            uint64_t legal_mask;
            
            if(checker_piece==enemy_pawn||checker_piece==enemy_knight){
                legal_mask=checkers;
            }else{
                int king_sq=lsb_index(board.bitboards[king_piece]);
                legal_mask=checkers;
                
                for(int dir=0;dir<8;dir++){
                    uint64_t ray=board.get_ray(dir,king_sq);
                    
                    if(ray&(1ULL<<checker_square)){
                        legal_mask=(ray^board.get_ray(dir,checker_square))|(1ULL<<checker_square);
                        break;
                    }
                }
            }
            
            KingMoves(board,moves,attack_mask);
            PawnMoves(board,moves,pins,legal_mask);
            KnightMoves(board,moves,pins,legal_mask);
            BishopMoves(board,moves,pins,legal_mask);
            RookMoves(board,moves,pins,legal_mask);
            QueenMoves(board,moves,pins,legal_mask);
            
        }else{
            KingMoves(board,moves,attack_mask);
        }
    }
};

inline bool is_in_check(Board &board){
    uint64_t checkers=0;
    Piece bishop=board.side_to_move==WHITE?Piece::b:Piece::B;
    Piece rook=board.side_to_move==WHITE?Piece::r:Piece::R;
    Piece queen=board.side_to_move==WHITE?Piece::q:Piece::Q;
    
    MoveGenerator mg;
    mg.pawnatk(board,checkers);
    mg.knightatk(board,checkers);
    mg.sliding_atks(board,bishop,4,8,checkers);
    mg.sliding_atks(board,rook,0,4,checkers);
    mg.sliding_atks(board,queen,0,8,checkers);
    
    return checkers!=0;
}

inline bool is_insufficient_material(const Board &board){
    if(board.bitboards[Piece::P]||board.bitboards[Piece::p]||
       board.bitboards[Piece::R]||board.bitboards[Piece::r]||
       board.bitboards[Piece::Q]||board.bitboards[Piece::q]){
        return false;
    }
    
    int white_knights=__builtin_popcountll(board.bitboards[Piece::N]);
    int black_knights=__builtin_popcountll(board.bitboards[Piece::n]);
    int white_bishops=__builtin_popcountll(board.bitboards[Piece::B]);
    int black_bishops=__builtin_popcountll(board.bitboards[Piece::b]);
    int total_pieces=2+white_knights+black_knights+white_bishops+black_bishops;
    
    if(total_pieces==2)return true;
    if(total_pieces==3){
        if(white_bishops==1||black_bishops==1||white_knights==1||black_knights==1){
            return true;
        }
    }
    
    if(total_pieces==4){
        if(white_bishops==1&&black_bishops==1){
            int white_bishop_sq=lsb_index(board.bitboards[Piece::B]);
            int black_bishop_sq=lsb_index(board.bitboards[Piece::b]);
            bool white_light=((white_bishop_sq/8)+(white_bishop_sq%8))%2!=0;
            bool black_light=((black_bishop_sq/8)+(black_bishop_sq%8))%2!=0;
            
            if(white_light==black_light)return true;
        }
    }
    
    return false;
}

inline GameResult get_game_result(Board &board){
    if(board.halfmove_clock>=150)return GAME_SEVENTY_FIVE_MOVE_DRAW;
    if(board.halfmove_clock>=100)return GAME_FIFTY_MOVE_DRAW;
    if(is_insufficient_material(board))return GAME_INSUFFICIENT_MATERIAL;
    
    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board,moves);
    
    if(moves.size()==0){
        if(is_in_check(board))return GAME_CHECKMATE;
        else return GAME_STALEMATE;
    }
    
    return GAME_ONGOING;
}
