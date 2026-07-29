#include <cstdint>
#include <array>
#include <utility>
#include <string>
#include <bit>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

enum Direction{
    NORTH=0,
    SOUTH=1,
    EAST=2,
    WEST=3,
    NE=4,
    SE=5,
    NW=6,
    SW=7
};

enum Piece{
    P=0,
    N=1,
    B=2,
    R=3,
    Q=4,
    K=5,
    p=6,
    n=7,
    b=8,
    r=9,
    q=10,
    k=11
};

enum Color{
    WHITE=0,
    BLACK=1
};

enum CastleRights{
    WHITE_KING_SIDE=1,
    WHITE_QUEEN_SIDE=2,
    BLACK_KING_SIDE=4,
    BLACK_QUEEN_SIDE=8
};

enum GameResult{
    GAME_ONGOING,
    GAME_CHECKMATE,
    GAME_STALEMATE,
    GAME_FIFTY_MOVE_DRAW,
    GAME_SEVENTY_FIVE_MOVE_DRAW,
    GAME_INSUFFICIENT_MATERIAL
};


inline uint64_t NOT_A_FILE=0xFEFEFEFEFEFEFEFEULL;
inline uint64_t NOT_H_FILE=0x7F7F7F7F7F7F7F7FULL;
inline uint64_t NOT_AB_FILE=0xFCFCFCFCFCFCFCFCULL;
inline uint64_t NOT_GH_FILE=0x3F3F3F3F3F3F3F3FULL;

inline uint64_t RANK_1=0x00000000000000FFULL;
inline uint64_t RANK_2=0x000000000000FF00ULL;
inline uint64_t RANK_7=0x00FF000000000000ULL;
inline uint64_t RANK_8=0xFF00000000000000ULL;

inline int lsb_index(uint64_t mask){
    return std::countr_zero(mask);
}

inline int msb_index(uint64_t mask){
    return 63-std::countl_zero(mask);
}

inline int pop_lsb(uint64_t &mask){
    int sq=lsb_index(mask);
    mask&=mask-1;
    return sq;
}

inline void print_bitboard(uint64_t bb){
    for(int r=7;r>=0;r--){
        for(int f=0;f<8;f++){
            int sq=r*8+f;
            cout<<(((bb>>sq)&1ULL)?'1':'.')<<' ';
        }   
        cout<<'\n';
    }
    cout<<'\n';
}

#include "magic_lut.cpp"

inline int move_from(uint32_t m){
    return (int)(m&0x3Fu);
}

inline int move_to(uint32_t m){
    return (int)((m>>6)&0x3Fu);
}

inline Piece move_piece(uint32_t m){
    return (Piece)((m>>12)&0xFu);
}

inline Piece move_captured_piece(uint32_t m){
    return (Piece)((m>>16)&0xFu);
}

inline Piece move_promotion_piece(uint32_t m){
    return (Piece)((m>>20)&0xFu);
}

inline bool move_is_capture(uint32_t m){
    return ((m>>16)&0xFu)!=0xFu;
}

inline bool move_is_promotion(uint32_t m){
    return ((m>>20)&0xFu)!=0xFu;
}

inline uint8_t move_castling_rights(uint32_t m){
    return (uint8_t)((m>>24)&0xFu);
}

inline bool move_is_castle(uint32_t m){
    return ((m>>28)&0x1u)!=0;
}

inline bool move_is_en_passant(uint32_t m){
    return ((m>>29)&0x1u)!=0;
}

inline bool move_is_double_push(uint32_t m){
    return ((m>>30)&0x1u)!=0;
}

inline Color move_side(uint32_t m){
    return (Color)((m>>31)&0x1u);
}

inline uint32_t pack_move(int from_square,int to_square,Piece piece,
                         uint32_t captured_piece=0xFu,uint32_t promotion_piece=0xFu,
                         bool is_castle=false,bool is_en_passant=false,bool is_double_push=false){
    uint32_t m=0;
    m=((uint32_t)from_square&0x3Fu);
    m|=(((uint32_t)to_square&0x3Fu)<<6);
    m|=(((uint32_t)piece&0xFu)<<12);
    m|=((captured_piece&0xFu)<<16);
    m|=((promotion_piece&0xFu)<<20);
    if(is_castle)m|=1u<<28;
    if(is_en_passant)m|=1u<<29;
    if(is_double_push)m|=1u<<30;
    return m;
}

inline string move_to_uci(uint32_t m){
    int f=move_from(m);
    int t=move_to(m);
    
    string uci="";
    uci+=(char)('a'+(f%8));
    uci+=(char)('1'+(f/8));
    uci+=(char)('a'+(t%8));
    uci+=(char)('1'+(t/8));
    
    if(move_is_promotion(m)){
        Piece promo=move_promotion_piece(m);
        
        if(promo==Piece::N||promo==Piece::n)uci+='n';
        else if(promo==Piece::B||promo==Piece::b)uci+='b';
        else if(promo==Piece::R||promo==Piece::r)uci+='r';
        else if(promo==Piece::Q||promo==Piece::q)uci+='q';
    }
    
    return uci;
}

class Board{
public:
    array<uint64_t,12> bitboards;
    array<uint64_t,3> occupancy;
    Piece piece_on[64];
    
    Color side_to_move=WHITE;
    Color enemy_color=BLACK;
    uint8_t castling_rights=0;
    uint8_t en_passant=255;
    
    int halfmove_clock=0;
    int fullmove_number=1;
    string START_FEN="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    
    vector<uint32_t> move_history;
    vector<uint8_t> ep_history;
    vector<uint8_t> halfmove_history;

    inline uint64_t get_ray(int direction,int square){
        return ray_table[direction][square];
    }
    Board(){
        move_history.reserve(4096);
        ep_history.reserve(4096);
        halfmove_history.reserve(4096);
        
        set_fen(START_FEN);
    }
    

    void set_fen(string fen_string){
        move_history.clear();
        ep_history.clear();
        halfmove_history.clear();
        
        for(int i=0;i<12;i++)bitboards[i]=0;
        for(int i=0;i<64;i++)piece_on[i]=(Piece)0xF;
        
        side_to_move=WHITE;
        istringstream ss(fen_string);
        string fields[6];
        int nfields=0;
        
        while(ss>>fields[nfields]){
            nfields++;
            if(nfields>=6)break;
        }
        
        if(nfields>0){
            string placement=fields[0];
            int rank=7;
            int file=0;
            
            for(char c:placement){
                if(c=='/'){
                    rank--;
                    file=0;
                }else if(isdigit(c)){
                    file+=c-'0';
                }else{
                    int sq=rank*8+file;
                    Piece p;
                    
                    switch(c){
                        case 'P':p=Piece::P;break;
                        case 'N':p=Piece::N;break;
                        case 'B':p=Piece::B;break;
                        case 'R':p=Piece::R;break;
                        case 'Q':p=Piece::Q;break;
                        case 'K':p=Piece::K;break;
                        case 'p':p=Piece::p;break;
                        case 'n':p=Piece::n;break;
                        case 'b':p=Piece::b;break;
                        case 'r':p=Piece::r;break;
                        case 'q':p=Piece::q;break;
                        case 'k':p=Piece::k;break;
                        default:p=(Piece)0xF;break;
                    }
                    
                    if(p!=(Piece)0xF){
                        bitboards[p]|=1ULL<<sq;
                        piece_on[sq]=p;
                    }
                    file++;
                }
            }
        }
        
        if(nfields>1)side_to_move=(fields[1][0]=='w')?WHITE:BLACK;
        
        if(nfields>2){
            string cr=fields[2];
            castling_rights=0;
            
            for(char c:cr){
                if(c=='K')castling_rights|=WHITE_KING_SIDE;
                else if(c=='Q')castling_rights|=WHITE_QUEEN_SIDE;
                else if(c=='k')castling_rights|=BLACK_KING_SIDE;
                else if(c=='q')castling_rights|=BLACK_QUEEN_SIDE;
            }
        }
        
        if(nfields>3){
            string ep=fields[3];
            
            if(ep!="-"&&ep.length()==2){
                int f=ep[0]-'a';
                int r=ep[1]-'1';
                en_passant=r*8+f;
            }else{
                en_passant=255;
            }
        }
        
        if(nfields>4)halfmove_clock=stoi(fields[4]);
        if(nfields>5)fullmove_number=stoi(fields[5]);
        
        update_occupancy();
    }

    string to_fen() {
        string fen="";
        
        for(int rank=7;rank>=0;--rank){
            int empty_count=0;
            
            for(int file=0;file<8;++file){
                int sq=rank*8+file;
                Piece p=piece_on[sq];
                
                if(p==0xF){
                    empty_count++;
                }else{
                    if(empty_count>0){
                        fen+=to_string(empty_count);
                        empty_count=0;
                    }
                    char piece_chars[12]={'P','N','B','R','Q','K','p','n','b','r','q','k'};
                    fen+=piece_chars[p];
                }
            }
            if(empty_count>0)fen+=to_string(empty_count);
            if(rank>0)fen+="/";
        }
        
        fen+=(side_to_move==WHITE)?" w ":" b ";
        string castling="";
        
        if(castling_rights&WHITE_KING_SIDE)castling+="K";
        if(castling_rights&WHITE_QUEEN_SIDE)castling+="Q";
        if(castling_rights&BLACK_KING_SIDE)castling+="k";
        if(castling_rights&BLACK_QUEEN_SIDE)castling+="q";
        if(castling=="")castling="-";
        fen+=castling+" ";
        
        if(en_passant!=255){
            int file=en_passant%8;
            int rank=en_passant/8;
            fen+=(char)('a'+file);
            fen+=(char)('1'+rank);
        }else{
            fen+="-";
        }
        
        fen+=" "+to_string(halfmove_clock);
        fen+=" "+to_string(fullmove_number);
        
        return fen;
    }

    void update_occupancy(){
        occupancy[Color::WHITE]=0;
        for(int i=0;i<6;++i)occupancy[Color::WHITE]|=bitboards[i];
        
        occupancy[Color::BLACK]=0;
        for(int i=6;i<12;++i)occupancy[Color::BLACK]|=bitboards[i];
        
        occupancy[2]=occupancy[Color::WHITE]|occupancy[Color::BLACK];
    }

    void print_board(){
        char piece_chars[12]={'P','N','B','R','Q','K','p','n','b','r','q','k'};
        
        for(int rank=7;rank>=0;--rank){
            cout<<(rank+1)<<" ";
            
            for(int file=0;file<8;++file){
                int square=rank*8+file;
                char piece='.';
                
                for(int p=0;p<12;++p){
                    if((bitboards[p]>>square)&1ULL){
                        piece=piece_chars[p];
                        break;
                    }
                }
                
                cout<<piece<<' ';
            }
            cout<<'\n';
        }
        cout<<"  a b c d e f g h\n";
    }

    void verify_board(Board& board){
        uint64_t white_occ=0,black_occ=0;
        
        for(int p=Piece::P;p<=Piece::K;p++)white_occ|=board.bitboards[p];
        for(int p=Piece::p;p<=Piece::k;p++)black_occ|=board.bitboards[p];
        
        uint64_t all_occ=white_occ|black_occ;
        
        if(white_occ!=board.occupancy[WHITE])cout<<"WHITE OCC MISMATCH\n";
        if(black_occ!=board.occupancy[BLACK])cout<<"BLACK OCC MISMATCH\n";
        if(all_occ!=board.occupancy[2])cout<<"ALL OCC MISMATCH\n";
    }

    void make_move(uint32_t move){
        Color enemy=(side_to_move==WHITE)?BLACK:WHITE;
        
        uint32_t packed_undo_move=pack_move(
            move_from(move),move_to(move),move_piece(move),
            (uint32_t)move_captured_piece(move),(uint32_t)move_promotion_piece(move),
            move_is_castle(move),move_is_en_passant(move),move_is_double_push(move));
        packed_undo_move|=((uint32_t)castling_rights<<24);
        packed_undo_move|=((uint32_t)side_to_move<<31);
        
        move_history.push_back(packed_undo_move);
        ep_history.push_back(en_passant);
        halfmove_history.push_back(halfmove_clock);
        
        Piece piece=move_piece(move);
        int from_square=move_from(move);
        int to_square=move_to(move);
        bool is_capture=move_is_capture(move);
        
        uint64_t from_mask=1ULL<<from_square;
        uint64_t to_mask=1ULL<<to_square;
        
        halfmove_clock++;
        if(piece==Piece::P||piece==Piece::p)halfmove_clock=0;
        if(is_capture)halfmove_clock=0;
        
        bitboards[piece]^=from_mask|to_mask;
        piece_on[from_square]=(Piece)0xF;
        piece_on[to_square]=piece;
        
        if(move_is_promotion(move)){
            Piece promo=move_promotion_piece(move);
            bitboards[piece]^=to_mask;
            bitboards[promo]^=to_mask;
            piece_on[to_square]=promo;
        }
        
        if(is_capture){
            if(move_is_en_passant(move)){
                int cap_sq=(side_to_move==WHITE)?to_square-8:to_square+8;
                Piece cap_pawn=(side_to_move==WHITE)?Piece::p:Piece::P;
                uint64_t cap_mask=1ULL<<cap_sq;
                
                piece_on[cap_sq]=(Piece)0xF;
                bitboards[cap_pawn]^=cap_mask;
                occupancy[enemy]^=cap_mask;
                occupancy[2]^=cap_mask;
            }else{
                Piece captured=move_captured_piece(move);
                bitboards[captured]^=to_mask;
                occupancy[enemy]^=to_mask;
                occupancy[2]^=to_mask;
            }
        }
        
        if(move_is_castle(move)){
            uint64_t rook_from,rook_to;
            Piece rook_piece;
            
            if(to_square==6){
                rook_from=1ULL<<7;rook_to=1ULL<<5;rook_piece=Piece::R;
                piece_on[7]=(Piece)0xF;piece_on[5]=Piece::R;
            }else if(to_square==2){
                rook_from=1ULL<<0;rook_to=1ULL<<3;rook_piece=Piece::R;
                piece_on[0]=(Piece)0xF;piece_on[3]=Piece::R;
            }else if(to_square==62){
                rook_from=1ULL<<63;rook_to=1ULL<<61;rook_piece=Piece::r;
                piece_on[63]=(Piece)0xF;piece_on[61]=Piece::r;
            }else{
                rook_from=1ULL<<56;rook_to=1ULL<<59;rook_piece=Piece::r;
                piece_on[56]=(Piece)0xF;piece_on[59]=Piece::r;
            }
            
            bitboards[rook_piece]^=rook_from|rook_to;
            occupancy[side_to_move]^=rook_from|rook_to;
            occupancy[2]^=rook_from|rook_to;
        }
        
        if(piece==Piece::K)castling_rights&=~(WHITE_KING_SIDE|WHITE_QUEEN_SIDE);
        if(piece==Piece::k)castling_rights&=~(BLACK_KING_SIDE|BLACK_QUEEN_SIDE);
        
        if(piece==Piece::R){
            if(from_square==7)castling_rights&=~WHITE_KING_SIDE;
            if(from_square==0)castling_rights&=~WHITE_QUEEN_SIDE;
        }
        if(piece==Piece::r){
            if(from_square==63)castling_rights&=~BLACK_KING_SIDE;
            if(from_square==56)castling_rights&=~BLACK_QUEEN_SIDE;
        }
        
        if(is_capture){
            if(to_square==7)castling_rights&=~WHITE_KING_SIDE;
            if(to_square==0)castling_rights&=~WHITE_QUEEN_SIDE;
            if(to_square==63)castling_rights&=~BLACK_KING_SIDE;
            if(to_square==56)castling_rights&=~BLACK_QUEEN_SIDE;
        }
        
        if(move_is_double_push(move)){
            en_passant=(piece==Piece::P)?to_square-8:to_square+8;
        }else{
            en_passant=255;
        }
        
        if(side_to_move==BLACK)fullmove_number++;
        side_to_move=(side_to_move==WHITE)?BLACK:WHITE;
        enemy_color=(side_to_move==WHITE)?BLACK:WHITE;
        
        update_occupancy();
    }

    void unmake_move(){
        uint32_t move=move_history.back();
        move_history.pop_back();
        
        castling_rights=move_castling_rights(move);
        en_passant=ep_history.back();
        ep_history.pop_back();
        halfmove_clock=halfmove_history.back();
        halfmove_history.pop_back();
        
        Piece piece=move_piece(move);
        int from_square=move_from(move);
        int to_square=move_to(move);
        bool is_capture=move_is_capture(move);
        
        uint64_t from_mask=1ULL<<from_square;
        uint64_t to_mask=1ULL<<to_square;
        
        side_to_move=(side_to_move==WHITE)?BLACK:WHITE;
        enemy_color=(side_to_move==WHITE)?BLACK:WHITE;
        if(side_to_move==BLACK)fullmove_number--;
        
        piece_on[from_square]=piece;
        piece_on[to_square]=(Piece)0xF;
        
        if(move_is_promotion(move)){
            Piece promo=move_promotion_piece(move);
            bitboards[promo]^=to_mask;
            bitboards[piece]^=from_mask;
        }else{
            bitboards[piece]^=from_mask|to_mask;
        }
        
        if(move_is_castle(move)){
            uint64_t rook_from,rook_to;
            Piece rook_piece;
            
            if(to_square==6){
                rook_from=1ULL<<7;rook_to=1ULL<<5;rook_piece=Piece::R;
                piece_on[7]=Piece::R;piece_on[5]=(Piece)0xF;
            }else if(to_square==2){
                rook_from=1ULL<<0;rook_to=1ULL<<3;rook_piece=Piece::R;
                piece_on[0]=Piece::R;piece_on[3]=(Piece)0xF;
            }else if(to_square==62){
                rook_from=1ULL<<63;rook_to=1ULL<<61;rook_piece=Piece::r;
                piece_on[63]=Piece::r;piece_on[61]=(Piece)0xF;
            }else{
                rook_from=1ULL<<56;rook_to=1ULL<<59;rook_piece=Piece::r;
                piece_on[56]=Piece::r;piece_on[59]=(Piece)0xF;
            }
            
            bitboards[rook_piece]^=rook_from|rook_to;
            occupancy[side_to_move]^=rook_from|rook_to;
            occupancy[2]^=rook_from|rook_to;
        }
        
        occupancy[side_to_move]^=from_mask|to_mask;
        occupancy[2]^=from_mask|to_mask;
        
        if(is_capture){
            if(move_is_en_passant(move)){
                int cap_sq=(move_side(move)==WHITE)?to_square-8:to_square+8;
                Piece cap_pawn=(move_side(move)==WHITE)?Piece::p:Piece::P;
                uint64_t cap_mask=1ULL<<cap_sq;
                
                piece_on[cap_sq]=cap_pawn;
                bitboards[cap_pawn]|=cap_mask;
                occupancy[enemy_color]|=cap_mask;
                occupancy[2]|=cap_mask;
            }else{
                Piece captured=move_captured_piece(move);
                
                piece_on[to_square]=captured;
                bitboards[captured]|=to_mask;
                occupancy[enemy_color]|=to_mask;
                occupancy[2]|=to_mask;
            }
        }
    }
};