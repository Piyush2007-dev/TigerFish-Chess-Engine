#pragma once
#include "board.cpp"
#include <cstring>

struct MoveList{
    uint16_t move_list[218];
    int scores[218];
    int count=0;
    
    void clear(){
        count=0;
    }
    void push(uint16_t m, int score=0){
        scores[count]=score;
        move_list[count++]=m;
    }
    int size() const {
        return count;
    }
    void sort_mvv_lva() {
        for (int i = 1; i < count; i++) {
            uint16_t key_move = move_list[i];
            int key_score = scores[i];
            int j = i - 1;
            while (j >= 0 && scores[j] < key_score) {
                move_list[j + 1] = move_list[j];
                scores[j + 1] = scores[j];
                j--;
            }
            move_list[j + 1] = key_move;
            scores[j + 1] = key_score;
        }
    }
};

// rook_attacks — O(1) lookup: masks occ to relevant squares, hashes with PEXT or magic, returns precomputed attack bitboard
inline uint64_t rook_attacks(int sq, uint64_t occ){
#if defined(__BMI2__)
    return ROOK_ATTACKS[sq][_pext_u64(occ, ROOK_MASK[sq])];
#else
    occ&=ROOK_MASK[sq];
    return ROOK_ATTACKS[sq][(occ*ROOK_MAGICS[sq])>>ROOK_SHIFT[sq]];
#endif
}

// bishop_attacks — same as rook_attacks but for diagonals
inline uint64_t bishop_attacks(int sq, uint64_t occ){
#if defined(__BMI2__)
    return BISHOP_ATTACKS[sq][_pext_u64(occ, BISHOP_MASK[sq])];
#else
    occ&=BISHOP_MASK[sq];
    return BISHOP_ATTACKS[sq][(occ*BISHOP_MAGICS[sq])>>BISHOP_SHIFT[sq]];
#endif
}

// queen_attacks — queen = rook + bishop; OR both attack sets together
inline uint64_t queen_attacks(int sq, uint64_t occ){
    return rook_attacks(sq,occ)|bishop_attacks(sq,occ);
}

// Precomputed attack tables for ultra-fast check and move queries
inline uint64_t KNIGHT_ATTACKS_TABLE[64];
inline uint64_t PAWN_ATTACKS_TABLE[2][64];

inline void init_rules_luts(){
    static bool initialized = false;
    if(initialized) return;
    initialized = true;

    for(int sq=0;sq<64;++sq){
        uint64_t lsb = 1ULL << sq;
        uint64_t l1 = (lsb & NOT_H_FILE) << 1;
        uint64_t l2 = (lsb & NOT_GH_FILE) << 2;
        uint64_t r1 = (lsb & NOT_A_FILE) >> 1;
        uint64_t r2 = (lsb & NOT_AB_FILE) >> 2;
        uint64_t h1 = l1 | r1;
        uint64_t h2 = l2 | r2;
        KNIGHT_ATTACKS_TABLE[sq] = (h1 << 16) | (h1 >> 16) | (h2 << 8) | (h2 >> 8);

        // White perspective attacks (hitting squares to the north)
        PAWN_ATTACKS_TABLE[WHITE][sq] = ((lsb & NOT_A_FILE) << 7) | ((lsb & NOT_H_FILE) << 9);
        // Black perspective attacks (hitting squares to the south)
        PAWN_ATTACKS_TABLE[BLACK][sq] = ((lsb & NOT_A_FILE) >> 9) | ((lsb & NOT_H_FILE) >> 7);
    }
}

struct RulesLUTInitializer {
    RulesLUTInitializer() {
        init_rules_luts();
    }
};
static RulesLUTInitializer s_rules_lut_init;

struct PinInfo{
    uint64_t pinned=0;
    uint64_t pin_mask[64];
    
    PinInfo(){
        memset(pin_mask, 0xFF, sizeof(pin_mask));
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
        
        int type = (int)p % 6;
        bool slider = (dir < 4) ? (type == 3 || type == 4) : (type == 2 || type == 4);
        if (!slider) continue;
        
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
            moves.push(pack_move(from_sq,to_sq,0x0));
        }
        
        while(captures){
            int to_sq=pop_lsb(captures);
            moves.push(pack_move(from_sq,to_sq,0x4));
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
    // Strip pinned knights upfront — pinned knights can never move legally
    uint64_t mask=board.bitboards[piece] & ~pins.pinned;
    
    while(mask){
        uint64_t lsb=mask&(0ULL-mask);
        int from_sq=lsb_index(lsb);
        
        auto pr=knight_attacks(lsb,all_occ,enemy_occ);
        uint64_t moves_mask=pr.first;
        uint64_t captures=pr.second;
        uint64_t piece_attack_mask=moves_mask|captures;
        
        moves_mask&=legal_mask;
        captures&=legal_mask;
        
        while(moves_mask){
            int to_sq=pop_lsb(moves_mask);
            moves.push(pack_move(from_sq,to_sq,0x0));
        }
        
        while(captures){
            int to_sq=pop_lsb(captures);
            moves.push(pack_move(from_sq,to_sq,0x4));
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
            moves.push(pack_move(from_square,to_square,dbl?0x1:0x0));
            mask&=mask-1;
        }
        
        {
            uint64_t left_caps=left_side&board.occupancy[enemy]&legal_mask&~promo_rank;
            
            while(left_caps){
                int to_square=pop_lsb(left_caps);
                int from_square=to_square-left_side_shift;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                moves.push(pack_move(from_square,to_square,0x4));
            }
        }
        
        {
            uint64_t right_caps=right_side&board.occupancy[enemy]&legal_mask&~promo_rank;
            
            while(right_caps){
                int to_square=pop_lsb(right_caps);
                int from_square=to_square-right_side_shift;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                moves.push(pack_move(from_square,to_square,0x4));
            }
        }
        
        {
            uint64_t push_promos=single_push_target&legal_mask&promo_rank;
            
            while(push_promos){
                int to_square=pop_lsb(push_promos);
                int from_square=to_square-push;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                // flags: 0x8=N, 0x9=B, 0xA=R, 0xB=Q (promo only)
                for(int i=0;i<4;i++){
                    moves.push(pack_move(from_square,to_square,0x8|i));
                }
            }
        }
        
        {
            uint64_t left_promo_caps=left_side&board.occupancy[enemy]&legal_mask&promo_rank;
            
            while(left_promo_caps){
                int to_square=pop_lsb(left_promo_caps);
                int from_square=to_square-left_side_shift;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                // flags: 0xC=N, 0xD=B, 0xE=R, 0xF=Q (promo+capture)
                for(int i=0;i<4;i++){
                    moves.push(pack_move(from_square,to_square,0xC|i));
                }
            }
        }
        
        {
            uint64_t right_promo_caps=right_side&board.occupancy[enemy]&legal_mask&promo_rank;
            
            while(right_promo_caps){
                int to_square=pop_lsb(right_promo_caps);
                int from_square=to_square-right_side_shift;
                uint64_t pin_mask=pins.pin_mask[from_square];
                
                if(!(pin_mask&(1ULL<<to_square)))continue;
                
                // flags: 0xC=N, 0xD=B, 0xE=R, 0xF=Q (promo+capture)
                for(int i=0;i<4;i++){
                    moves.push(pack_move(from_square,to_square,0xC|i));
                }
            }
        }
        
        if(board.en_passant!=255){
            uint64_t ep_bb=1ULL<<board.en_passant;
            uint64_t ep_pawns;
            int captured_sq=white?board.en_passant-8:board.en_passant+8;
            if(board.en_passant>=64 || captured_sq<0 || captured_sq>=64 ||
               board.piece_on[board.en_passant]!=(Piece)0xF ||
               board.piece_on[captured_sq]!=enemy_pawn) return;
            
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
                    moves.push(pack_move(from_sq,board.en_passant,0x5));
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

    void KingMoves(Board &board,MoveList &moves,uint64_t enemy_attack_mask,bool captures_only=false){
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
        
        if(!captures_only){
            uint64_t move_mask=raw_mask&~board.occupancy[2];
            while(move_mask){
                int to_square=pop_lsb(move_mask);
                moves.push(pack_move(from_square,to_square,0x0));
            }
        }
        
        while(captures_mask){
            int to_square=pop_lsb(captures_mask);
            moves.push(pack_move(from_square,to_square,0x4));
        }
        
        if(captures_only) return;
        
        if(board.side_to_move==WHITE){
            if((board.castling_rights&WHITE_KING_SIDE)&&
               from_square==4&&
                    (board.bitboards[Piece::R]&(1ULL<<7))&&
               !(board.occupancy[2]&((1ULL<<5)|(1ULL<<6)))&&
               !(enemy_attack_mask&((1ULL<<4)|(1ULL<<5)|(1ULL<<6)))){
                moves.push(pack_move(4,6,0x2));
            }
            if((board.castling_rights&WHITE_QUEEN_SIDE)&&
                   from_square==4&&
                    (board.bitboards[Piece::R]&(1ULL<<0))&&
               !(board.occupancy[2]&((1ULL<<1)|(1ULL<<2)|(1ULL<<3)))&&
               !(enemy_attack_mask&((1ULL<<2)|(1ULL<<3)|(1ULL<<4)))){
                moves.push(pack_move(4,2,0x3));
            }
        }else{
            if((board.castling_rights&BLACK_KING_SIDE)&&
               from_square==60&&
                    (board.bitboards[Piece::r]&(1ULL<<63))&&
               !(board.occupancy[2]&((1ULL<<61)|(1ULL<<62)))&&
               !(enemy_attack_mask&((1ULL<<60)|(1ULL<<61)|(1ULL<<62)))){
                moves.push(pack_move(60,62,0x2));
            }
            if((board.castling_rights&BLACK_QUEEN_SIDE)&&
                   from_square==60&&
                    (board.bitboards[Piece::r]&(1ULL<<56))&&
               !(board.occupancy[2]&((1ULL<<57)|(1ULL<<58)|(1ULL<<59)))&&
               !(enemy_attack_mask&((1ULL<<58)|(1ULL<<59)|(1ULL<<60)))){
                moves.push(pack_move(60,58,0x3));
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
        
        int check_count=popcount64(checkers);
        
        if(check_count==0){
            uint64_t legal_mask=~0ULL;
            
            KingMoves(board,moves,attack_mask);
            PawnMoves(board,moves,pins,legal_mask);
            KnightMoves(board,moves,pins,legal_mask);
            BishopMoves(board,moves,pins,legal_mask);
            RookMoves(board,moves,pins,legal_mask);
            QueenMoves(board,moves,pins,legal_mask);
            
        }
        else if(check_count==1){
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

    // Dedicated capture-only move generator for Quiescence Search (skips 80% quiet moves)
    void generate_captures(Board &board, MoveList &moves, bool known_not_in_check = false){
        Color enemy = (board.side_to_move == WHITE) ? BLACK : WHITE;
        Piece king_piece = (board.side_to_move == WHITE) ? Piece::K : Piece::k;
        uint64_t king_bb = board.bitboards[king_piece];
        
        // Fast test: is King adjacent to any enemy piece?
        uint64_t east = (king_bb & NOT_H_FILE) << 1;
        uint64_t west = (king_bb & NOT_A_FILE) >> 1;
        uint64_t r = king_bb | east | west;
        uint64_t king_adj = ((r << 8) | r | (r >> 8)) & ~king_bb;
        uint64_t possible_king_caps = king_adj & board.occupancy[enemy];

        uint64_t attack_mask = 0;
        int check_count = 0;

        // Skip attack map generation when known not in check and King has no adjacent enemy captures
        if (!known_not_in_check || possible_king_caps) {
            uint64_t checkers = 0;
            Piece bishop = board.side_to_move == WHITE ? Piece::b : Piece::B;
            Piece rook   = board.side_to_move == WHITE ? Piece::r : Piece::R;
            Piece queen  = board.side_to_move == WHITE ? Piece::q : Piece::Q;
            
            attack_mask |= pawnatk(board, checkers);
            attack_mask |= knightatk(board, checkers);
            attack_mask |= sliding_atks(board, bishop, 4, 8, checkers);
            attack_mask |= sliding_atks(board, rook, 0, 4, checkers);
            attack_mask |= sliding_atks(board, queen, 0, 8, checkers);
            attack_mask |= kingatk(board);
            
            check_count = known_not_in_check ? 0 : popcount64(checkers);
        }

        if (check_count == 1) {
            generate_moves(board, moves);
            int write_idx = 0;
            for (int i = 0; i < moves.count; ++i) {
                uint16_t m = moves.move_list[i];
                if (move_is_capture(m) || move_is_promotion(m)) {
                    moves.move_list[write_idx] = m;
                    moves.scores[write_idx] = moves.scores[i];
                    write_idx++;
                }
            }
            moves.count = write_idx;
            return;
        }

        if (check_count > 1) {
            KingMoves(board, moves, attack_mask, true);
        } else {
            PinInfo pins = find_pins(board);
            uint64_t promo_rank = (board.side_to_move == WHITE) ? RANK_8 : RANK_1;
            uint64_t capture_targets = board.occupancy[enemy] | promo_rank;

            if (possible_king_caps) {
                KingMoves(board, moves, attack_mask, true);
            }
            PawnMoves(board, moves, pins, capture_targets);
            KnightMoves(board, moves, pins, board.occupancy[enemy]);
            BishopMoves(board, moves, pins, board.occupancy[enemy]);
            RookMoves(board, moves, pins, board.occupancy[enemy]);
            QueenMoves(board, moves, pins, board.occupancy[enemy]);
        }

        static const int MVV_LVA_VAL[12] = {100, 300, 325, 500, 900, 10000, 100, 300, 325, 500, 900, 10000};
        for (int i = 0; i < moves.count; i++) {
            uint16_t m = moves.move_list[i];
            if (move_is_capture(m)) {
                Piece attacker = board.piece_on[move_from(m)];
                Piece victim;
                if (move_is_en_passant(m)) {
                    victim = (board.side_to_move == WHITE) ? Piece::p : Piece::P;
                } else {
                    victim = board.piece_on[move_to(m)];
                }
                if ((int)attacker < 12 && (int)victim < 12) {
                    moves.scores[i] = (MVV_LVA_VAL[victim] * 10) - MVV_LVA_VAL[attacker] + 10000;
                } else {
                    moves.scores[i] = 10000;
                }
            } else if (move_is_promotion(m)) {
                moves.scores[i] = 9000;
            } else {
                moves.scores[i] = 0;
            }
        }
    }
};

// Ultra-fast King-centric check detection: tests rays outward from King square with early exit
inline bool is_in_check(Board &board){
    init_rules_luts();
    Piece king = (board.side_to_move == WHITE) ? Piece::K : Piece::k;
    uint64_t king_bb = board.bitboards[king];
    if (!king_bb) return false;
    int king_sq = lsb_index(king_bb);
    if (king_sq < 0 || king_sq > 63) return false;

    Color enemy = (board.side_to_move == WHITE) ? BLACK : WHITE;
    uint64_t occ = board.occupancy[2];

    // 1. Pawn checks
    Piece enemy_pawn = (board.side_to_move == WHITE) ? Piece::p : Piece::P;
    if (PAWN_ATTACKS_TABLE[board.side_to_move][king_sq] & board.bitboards[enemy_pawn])
        return true;

    // 2. Knight checks
    Piece enemy_knight = (board.side_to_move == WHITE) ? Piece::n : Piece::N;
    if (KNIGHT_ATTACKS_TABLE[king_sq] & board.bitboards[enemy_knight])
        return true;

    // 3. Bishop & Queen diagonal checks
    Piece enemy_bishop = (board.side_to_move == WHITE) ? Piece::b : Piece::B;
    Piece enemy_queen = (board.side_to_move == WHITE) ? Piece::q : Piece::Q;
    if (bishop_attacks(king_sq, occ) & (board.bitboards[enemy_bishop] | board.bitboards[enemy_queen]))
        return true;

    // 4. Rook & Queen orthogonal checks
    Piece enemy_rook = (board.side_to_move == WHITE) ? Piece::r : Piece::R;
    if (rook_attacks(king_sq, occ) & (board.bitboards[enemy_rook] | board.bitboards[enemy_queen]))
        return true;

    return false;
}

inline bool is_insufficient_material(const Board &board){
    if(board.bitboards[Piece::P]||board.bitboards[Piece::p]||
       board.bitboards[Piece::R]||board.bitboards[Piece::r]||
       board.bitboards[Piece::Q]||board.bitboards[Piece::q]){
        return false;
    }
    
    int white_knights=popcount64(board.bitboards[Piece::N]);
    int black_knights=popcount64(board.bitboards[Piece::n]);
    int white_bishops=popcount64(board.bitboards[Piece::B]);
    int black_bishops=popcount64(board.bitboards[Piece::b]);
    int total_pieces=2+white_knights+black_knights+white_bishops+black_bishops;
    
    if(total_pieces==2)return true;
    if(total_pieces==3){
        if(white_bishops==1||black_bishops==1||white_knights==1||black_knights==1){
            return true;
        }
    }
    
    // Check bishop square colors across all bishops
    if (white_knights == 0 && black_knights == 0 && (white_bishops + black_bishops > 0)) {
        // If all bishops on board are on the SAME square color, checkmate is impossible
        uint64_t all_bishops = board.bitboards[Piece::B] | board.bitboards[Piece::b];
        bool has_light = false, has_dark = false;
        while (all_bishops) {
            int sq = pop_lsb(all_bishops);
            bool is_light = ((sq / 8) + (sq % 8)) % 2 != 0;
            if (is_light) has_light = true;
            else has_dark = true;
        }
        // If all bishops on board share the exact same square color
        if (!(has_light && has_dark)) {
            return true;
        }
    }

    return false;
}

inline GameResult get_game_result(Board &board){
    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board,moves);
    
    if(moves.size()==0){
        if(is_in_check(board))return GAME_CHECKMATE;
        else return GAME_STALEMATE;
    }

    if(board.halfmove_clock>=150)return GAME_SEVENTY_FIVE_MOVE_DRAW;
    if(board.halfmove_clock>=100)return GAME_FIFTY_MOVE_DRAW;
    if(is_insufficient_material(board))return GAME_INSUFFICIENT_MATERIAL;
    if(board.is_repetition(3))return GAME_THREEFOLD_REPETITION;
    
    return GAME_ONGOING;
}

// ═══════════════════════════════════════════════════════════════════
// STATIC EXCHANGE EVALUATION (SEE)
// Estimates the outcome of a sequence of captures on a single square.
// Returns a score: positive if the move is likely winning, negative if losing.
// ═══════════════════════════════════════════════════════════════════

static inline int see_piece_value(int piece_type) {
    // piece_type: 0=P,1=N,2=B,3=R,4=Q,5=K
    static const int vals[6] = { 100, 320, 330, 500, 900, 20000 };
    return vals[piece_type];
}

// Find the least valuable attacker of a given square from the given side
// Returns the square of the attacker, or -1 if none found.
// Updates occupancy to remove the found attacker.
inline int see_find_attacker(const Board& board, int to_sq, int side,
                             uint64_t& occ) {
    static const uint64_t FILE_A = 0x0101010101010101ULL;
    static const uint64_t FILE_H = 0x8080808080808080ULL;
    // Pawn attacks
    uint64_t pawn_attacks;
    if (side == WHITE) {
        uint64_t left = (1ULL << to_sq) & ~FILE_A ? (1ULL << to_sq) >> 9 : 0;
        uint64_t right = (1ULL << to_sq) & ~FILE_H ? (1ULL << to_sq) >> 7 : 0;
        pawn_attacks = (left | right) & board.bitboards[Piece::P] & occ;
    } else {
        uint64_t left = (1ULL << to_sq) & ~FILE_A ? (1ULL << to_sq) << 7 : 0;
        uint64_t right = (1ULL << to_sq) & ~FILE_H ? (1ULL << to_sq) << 9 : 0;
        pawn_attacks = (left | right) & board.bitboards[Piece::p] & occ;
    }
    if (pawn_attacks) {
        int sq = lsb_index(pawn_attacks);
        occ ^= (1ULL << sq); // remove this specific attacker
        return sq;
    }

    // Knight attacks
    Piece knight = (side == WHITE) ? Piece::N : Piece::n;
    uint64_t knights = board.bitboards[knight] & occ;
    while (knights) {
        int sq = lsb_index(knights);
        uint64_t lsb = 1ULL << sq;
        uint64_t l1 = (lsb & NOT_H_FILE) << 1;
        uint64_t l2 = (lsb & NOT_GH_FILE) << 2;
        uint64_t r1 = (lsb & NOT_A_FILE) >> 1;
        uint64_t r2 = (lsb & NOT_AB_FILE) >> 2;
        uint64_t attacks = ((l1|r1)<<16) | ((l1|r1)>>16) | ((l2|r2)<<8) | ((l2|r2)>>8);
        if (attacks & (1ULL << to_sq)) {
            occ ^= (1ULL << sq); // remove this specific attacker
            return sq;
        }
        knights &= knights - 1;
    }

    // Bishop attacks
    Piece bishop = (side == WHITE) ? Piece::B : Piece::b;
    uint64_t bishops = board.bitboards[bishop] & occ;
    while (bishops) {
        int sq = lsb_index(bishops);
        if (bishop_attacks(sq, occ) & (1ULL << to_sq)) {
            occ ^= (1ULL << sq); // remove this specific attacker
            return sq;
        }
        bishops &= bishops - 1;
    }

    // Rook attacks
    Piece rook = (side == WHITE) ? Piece::R : Piece::r;
    uint64_t rooks = board.bitboards[rook] & occ;
    while (rooks) {
        int sq = lsb_index(rooks);
        if (rook_attacks(sq, occ) & (1ULL << to_sq)) {
            occ ^= (1ULL << sq); // remove this specific attacker
            return sq;
        }
        rooks &= rooks - 1;
    }

    // Queen attacks
    Piece queen = (side == WHITE) ? Piece::Q : Piece::q;
    uint64_t queens = board.bitboards[queen] & occ;
    while (queens) {
        int sq = lsb_index(queens);
        if (queen_attacks(sq, occ) & (1ULL << to_sq)) {
            occ ^= (1ULL << sq); // remove this specific attacker
            return sq;
        }
        queens &= queens - 1;
    }

    // King attacks
    Piece king = (side == WHITE) ? Piece::K : Piece::k;
    uint64_t king_bb = board.bitboards[king] & occ;
    if (king_bb) {
        int sq = lsb_index(king_bb);
        uint64_t lsb = 1ULL << sq;
        uint64_t east = (lsb & NOT_H_FILE) << 1;
        uint64_t west = (lsb & NOT_A_FILE) >> 1;
        uint64_t r = lsb | east | west;
        uint64_t attacks = ((r<<8)|r|(r>>8)) & ~lsb;
        if (attacks & (1ULL << to_sq)) {
            occ ^= (1ULL << sq); // remove this specific attacker
            return sq;
        }
    }

    return -1;
}

// SEE: simulate the capture sequence on to_sq and return the estimated net gain
inline int see(const Board& board, uint16_t move) {
    int from_sq = move_from(move);
    int to_sq = move_to(move);
    Piece attacker = board.piece_on[from_sq];
    Piece victim;
    if (move_is_en_passant(move)) {
        victim = (attacker <= Piece::K) ? Piece::p : Piece::P;
    } else {
        victim = board.piece_on[to_sq];
    }
    if (victim == (Piece)0xF) return 0;

    int attacker_side = (attacker <= Piece::K) ? WHITE : BLACK;
    int victim_value = see_piece_value(victim % 6);
    int attacker_value = see_piece_value(attacker % 6);

    // Simple SEE: if attacker value < victim value, likely winning
    // Full SEE with swing: gain = victim - min(attacker,SEE下一步)
    // For now use a fast approximation based on piece values
    if (attacker_value <= victim_value) return victim_value - attacker_value;

    // For more expensive attackers capturing cheap pieces, use
    // victim_value as the best-case gain (opponent recaptures for free)
    return victim_value - attacker_value;
}

// Full SEE with iterative capture simulation (used for accurate ordering)
inline int see_full(const Board& board, uint16_t move) {
    int from_sq = move_from(move);
    int to_sq = move_to(move);
    Piece attacker = board.piece_on[from_sq];
    Piece victim;
    if (move_is_en_passant(move)) {
        victim = (attacker <= Piece::K) ? Piece::p : Piece::P;
    } else {
        victim = board.piece_on[to_sq];
    }
    if (victim == (Piece)0xF) return 0;

    int attacker_side = (attacker <= Piece::K) ? WHITE : BLACK;
    int victim_type = victim % 6;
    int attacker_type = attacker % 6;

    // Build initial occupancy
    uint64_t occ = board.occupancy[2] ^ (1ULL << from_sq);

    int gain[32];
    int depth = 0;

    gain[0] = see_piece_value(victim_type);

    int side = attacker_side;
    uint64_t occ_for_side[2] = { board.occupancy[WHITE], board.occupancy[BLACK] };
    // Remove the attacker from its side's occupancy
    if (attacker_side == WHITE) occ_for_side[WHITE] ^= (1ULL << from_sq);
    else occ_for_side[BLACK] ^= (1ULL << from_sq);

    // Attacker was on from_sq; we need to simulate the attacker reaching to_sq
    // The first capture is already accounted for in gain[0]

    while (true) {
        depth++;
        side = (side == WHITE) ? BLACK : WHITE;
        gain[depth] = see_piece_value(attacker_type) - gain[depth - 1];
        if (max(-gain[depth - 1], gain[depth]) < 0) break;

        // Find least valuable attacker of this side
        int att_sq = see_find_attacker(board, to_sq, side, occ);
        if (att_sq == -1) break;

        // Remove attacker from occupancy and its side occupancy
        occ &= ~(1ULL << att_sq);
        Piece att_piece = board.piece_on[att_sq];
        attacker_type = att_piece % 6;
    }

    // Negamax the gain array
    while (--depth > 0) {
        gain[depth - 1] = -max(-gain[depth - 1], gain[depth]);
    }

    return gain[0];
}
