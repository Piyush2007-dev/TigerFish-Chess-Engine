#pragma once
#include <cstdint>
#include <array>
#include <utility>
#include <string>
#include <bit>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <vector>
#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <immintrin.h>
#endif
#include "pst_tables.h"

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
    GAME_INSUFFICIENT_MATERIAL,
    GAME_THREEFOLD_REPETITION
};

inline uint64_t NOT_A_FILE=0xFEFEFEFEFEFEFEFEULL;
inline uint64_t NOT_H_FILE=0x7F7F7F7F7F7F7F7FULL;
inline uint64_t NOT_AB_FILE=0xFCFCFCFCFCFCFCFCULL;
inline uint64_t NOT_GH_FILE=0x3F3F3F3F3F3F3F3FULL;

inline uint64_t RANK_1=0x00000000000000FFULL;
inline uint64_t RANK_2=0x000000000000FF00ULL;
inline uint64_t RANK_7=0x00FF000000000000ULL;
inline uint64_t RANK_8=0xFF00000000000000ULL;

// ── Portable Low-Level Hardware Bit Operations ─────────────────────────
// On modern x86 with BMI/POPCNT/LZCNT: compiles to single 1-cycle CPU instructions.
// On older/generic x86 or non-x86: transparently falls back to portable builtins / std::bit.

inline int popcount64(uint64_t mask) {
#if defined(__POPCNT__)
    return (int)_mm_popcnt_u64(mask);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(mask);
#else
    return std::popcount(mask);
#endif
}

inline int lsb_index(uint64_t mask) {
#if defined(__BMI__)
    return (int)_tzcnt_u64(mask);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(mask);
#else
    return std::countr_zero(mask);
#endif
}

inline int msb_index(uint64_t mask) {
#if defined(__LZCNT__)
    return 63 - (int)_lzcnt_u64(mask);
#elif defined(__GNUC__) || defined(__clang__)
    return 63 - __builtin_clzll(mask);
#else
    return 63 - std::countl_zero(mask);
#endif
}

inline int pop_lsb(uint64_t &mask) {
#if defined(__BMI__)
    int sq = (int)_tzcnt_u64(mask);
    mask = _blsr_u64(mask);
    return sq;
#elif defined(__GNUC__) || defined(__clang__)
    int sq = __builtin_ctzll(mask);
    mask &= mask - 1;
    return sq;
#else
    int sq = std::countr_zero(mask);
    mask &= mask - 1;
    return sq;
#endif
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

// 16-bit move layout: bits 0-5 from, bits 6-11 to, bits 12-15 flags
inline int move_from(uint16_t m){
    return (int)(m&0x3Fu);
}

inline int move_to(uint16_t m){
    return (int)((m>>6)&0x3Fu);
}

inline int move_flags(uint16_t m){
    return (int)((m>>12)&0xFu);
}

inline bool move_is_capture(uint16_t m){
    return ((m>>14)&1u)!=0;
}

inline bool move_is_promotion(uint16_t m){
    return ((m>>15)&1u)!=0;
}

inline bool move_is_double_push(uint16_t m){
    return move_flags(m)==0x1;
}

inline bool move_is_castle(uint16_t m){
    int f=move_flags(m);
    return f==0x2||f==0x3;
}

inline bool move_is_en_passant(uint16_t m){
    return move_flags(m)==0x5;
}

inline Piece move_promotion_piece_from_flags(uint16_t m,Color side){
    int promo_type=(move_flags(m)&3)+1;
    return (Piece)(promo_type+(side==BLACK?6:0));
}

inline uint16_t pack_move(int from_square,int to_square,int flags){
    return (uint16_t)((from_square&0x3F)|((to_square&0x3F)<<6)|((flags&0xF)<<12));
}

inline string move_to_uci(uint16_t m){
    int f=move_from(m);
    int t=move_to(m);
    int flags=move_flags(m);
    
    string uci="";
    uci+=(char)('a'+(f%8));
    uci+=(char)('1'+(f/8));
    uci+=(char)('a'+(t%8));
    uci+=(char)('1'+(t/8));
    
    if(flags&0x8){
        static const char promo_chars[]={'n','b','r','q'};
        uci+=promo_chars[flags&3];
    }
    
    return uci;
}

// Zobrist Hashing Global Tables
inline uint64_t ZOBRIST_PIECE[12][64];
inline uint64_t ZOBRIST_SIDE;
inline uint64_t ZOBRIST_CASTLING[16];
inline uint64_t ZOBRIST_EP[65];
inline bool zobrist_initialized = false;

inline uint64_t rng64(uint64_t& state) {
    // SplitMix64 deterministic PRNG
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

inline void init_zobrist() {
    if (zobrist_initialized) return;
    uint64_t state = 1070372ULL; // Fixed seed for reproducible keys
    for (int p = 0; p < 12; p++) {
        for (int sq = 0; sq < 64; sq++) {
            ZOBRIST_PIECE[p][sq] = rng64(state);
        }
    }
    ZOBRIST_SIDE = rng64(state);
    for (int i = 0; i < 16; i++) {
        ZOBRIST_CASTLING[i] = rng64(state);
    }
    for (int i = 0; i < 65; i++) {
        ZOBRIST_EP[i] = rng64(state);
    }
    zobrist_initialized = true;
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
    int material_score=0;
    int phase_score=0;
    int mg_pst=0;
    int eg_pst=0;
    string START_FEN="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    struct UndoState {
        uint16_t move;
        uint8_t captured_piece;
        uint8_t castling_rights;
        uint8_t en_passant;
        uint16_t halfmove_clock;
        int material_score;
        int phase_score;
        int mg_pst;
        int eg_pst;
        uint64_t zobrist_hash;
    };
    UndoState history_stack[8192];
    int history_ply = 0;
    uint64_t zobrist_hash = 0ULL;

    inline uint64_t get_ray(int direction,int square){
        return ray_table[direction][square];
    }
    Board(){
        init_zobrist();
        history_ply=0;
        set_fen(START_FEN);
    }

    uint64_t compute_zobrist_hash() const {
        uint64_t h = 0ULL;
        for (int sq = 0; sq < 64; sq++) {
            Piece p = piece_on[sq];
            if (p != (Piece)0xF) {
                h ^= ZOBRIST_PIECE[p][sq];
            }
        }
        if (side_to_move == BLACK) {
            h ^= ZOBRIST_SIDE;
        }
        h ^= ZOBRIST_CASTLING[castling_rights & 0xFu];
        h ^= ZOBRIST_EP[en_passant == 255 ? 64 : en_passant];
        return h;
    }
    

    bool set_fen(string fen_string){
        string fields[6];
        int nfields=0;
        istringstream input(fen_string);
        string extra;
        while (nfields < 6 && input >> fields[nfields]) nfields++;
        if (nfields != 6 || (input >> extra)) return false;

        int rank=7;
        int file=0;
        int white_kings=0;
        int black_kings=0;
        for(char c:fields[0]){
            if(c=='/'){
                if(file!=8 || rank==0) return false;
                rank--;
                file=0;
            }else if(c>='1'&&c<='8'){
                file+=c-'0';
                if(file>8) return false;
            }else{
                if(c!='P'&&c!='N'&&c!='B'&&c!='R'&&c!='Q'&&c!='K'&&
                   c!='p'&&c!='n'&&c!='b'&&c!='r'&&c!='q'&&c!='k') return false;
                if((c=='P' && rank==7) || (c=='p' && rank==0)) return false;
                if(c=='K') white_kings++;
                if(c=='k') black_kings++;
                if(++file>8) return false;
            }
        }
        if(rank!=0 || file!=8 || white_kings!=1 || black_kings!=1) return false;
        if(fields[1]!="w" && fields[1]!="b") return false;
        if(fields[2]!="-"){
            for(char c:fields[2]){
                if(c!='K'&&c!='Q'&&c!='k'&&c!='q') return false;
            }
        }
        if(fields[3]!="-"){
            if(fields[3].size()!=2 || fields[3][0]<'a' || fields[3][0]>'h' ||
               (fields[3][1]!='3' && fields[3][1]!='6')) return false;
        }
        auto valid_number=[](const string& value){
            if(value.empty()) return false;
            for(char c:value) if(c<'0'||c>'9') return false;
            return true;
        };
        if(!valid_number(fields[4]) || !valid_number(fields[5])) return false;
        int parsed_halfmove=0;
        int parsed_fullmove=1;
        try {
            parsed_halfmove=stoi(fields[4]);
            parsed_fullmove=stoi(fields[5]);
        } catch (const exception&) {
            return false;
        }

        history_ply=0;
        castling_rights=0;
        en_passant=255;
        halfmove_clock=0;
        fullmove_number=1;
        material_score=0;
        phase_score=0;
        mg_pst=0;
        eg_pst=0;
        
        for(int i=0;i<12;i++)bitboards[i]=0;
        for(int i=0;i<64;i++)piece_on[i]=(Piece)0xF;
        
        side_to_move=WHITE;
        {
            string placement=fields[0];
            rank=7;
            file=0;
            
            for(char c:placement){
                if(c=='/'){
                    rank--;
                    file=0;
                }

                else if(isdigit(c)){
                    file+=c-'0'; //c-'0' will give ascii of char - ascii of 0 to know to skip thtat much sq
                }

                else{
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
                        static const int piece_values[12] = {
                            100, 320, 330, 500, 900, 20000,
                            100, 320, 330, 500, 900, 20000
                        };
                        static const int phase_values[12] = {
                            0, 1, 1, 2, 4, 0,
                            0, 1, 1, 2, 4, 0
                        };
                        material_score += p < 6 ? piece_values[p] : -piece_values[p];
                        phase_score += phase_values[p];
                        mg_pst += PIECE_MG_PST[p][sq];
                        eg_pst += PIECE_EG_PST[p][sq];
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
            }
            else{
                en_passant=255;
            }
        }
        
        halfmove_clock=parsed_halfmove;
        fullmove_number=parsed_fullmove;
        
        history_ply=0;
        update_occupancy();
        zobrist_hash=compute_zobrist_hash();
        return true;
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

//old internal  function 
    void verify_board(Board& board){
        uint64_t white_occ=0,black_occ=0;
        
        for(int p=Piece::P;p<=Piece::K;p++)white_occ|=board.bitboards[p];
        for(int p=Piece::p;p<=Piece::k;p++)black_occ|=board.bitboards[p];
        
        uint64_t all_occ=white_occ|black_occ;
        
        if(white_occ!=board.occupancy[WHITE])cout<<"WHITE OCC MISMATCH\n";
        if(black_occ!=board.occupancy[BLACK])cout<<"BLACK OCC MISMATCH\n";
        if(all_occ!=board.occupancy[2])cout<<"ALL OCC MISMATCH\n";
    }

    void make_move(uint16_t move){
        Color enemy=(side_to_move==WHITE)?BLACK:WHITE;
        
        int from_square=move_from(move);
        int to_square=move_to(move);
        Piece piece=piece_on[from_square];
        bool is_capture=move_is_capture(move);
        int flags=move_flags(move);
        
        // Determine captured piece BEFORE modifying board
        Piece captured_piece=(Piece)0xF;
        if(flags==0x5){
            captured_piece=(side_to_move==WHITE)?Piece::p:Piece::P;
        }else if(is_capture){
            captured_piece=piece_on[to_square];
        }
        
        // Push undo state
        history_stack[history_ply].move=move;
        history_stack[history_ply].captured_piece=(uint8_t)captured_piece;
        history_stack[history_ply].castling_rights=castling_rights;
        history_stack[history_ply].en_passant=en_passant;
        history_stack[history_ply].halfmove_clock=halfmove_clock;
        history_stack[history_ply].material_score=material_score;
        history_stack[history_ply].phase_score=phase_score;
        history_stack[history_ply].mg_pst=mg_pst;
        history_stack[history_ply].eg_pst=eg_pst;
        history_stack[history_ply].zobrist_hash=zobrist_hash;
        history_ply++;
        
        uint8_t old_castling_rights=castling_rights;
        uint8_t old_en_passant=en_passant;
        
        zobrist_hash^=ZOBRIST_PIECE[piece][from_square];
        zobrist_hash^=ZOBRIST_PIECE[piece][to_square];
        
        uint64_t from_mask=1ULL<<from_square;
        uint64_t to_mask=1ULL<<to_square;
        
        halfmove_clock++;
        if(piece==Piece::P||piece==Piece::p)halfmove_clock=0;
        if(is_capture)halfmove_clock=0;
        
        bitboards[piece]^=from_mask|to_mask;
        piece_on[from_square]=(Piece)0xF;
        piece_on[to_square]=piece;
        mg_pst += PIECE_MG_PST[piece][to_square] - PIECE_MG_PST[piece][from_square];
        eg_pst += PIECE_EG_PST[piece][to_square] - PIECE_EG_PST[piece][from_square];
        
        occupancy[side_to_move]^=from_mask|to_mask;
        occupancy[2]^=from_mask|to_mask;
        
        if(move_is_promotion(move)){
            Piece promo=move_promotion_piece_from_flags(move,side_to_move);
            bitboards[piece]^=to_mask;
            bitboards[promo]^=to_mask;
            piece_on[to_square]=promo;
            zobrist_hash^=ZOBRIST_PIECE[piece][to_square];
            zobrist_hash^=ZOBRIST_PIECE[promo][to_square];
            mg_pst += PIECE_MG_PST[promo][to_square] - PIECE_MG_PST[piece][to_square];
            eg_pst += PIECE_EG_PST[promo][to_square] - PIECE_EG_PST[piece][to_square];
        }
        
        if(is_capture){
            if(move_is_en_passant(move)){
                int cap_sq=(side_to_move==WHITE)?to_square-8:to_square+8;
                uint64_t cap_mask=1ULL<<cap_sq;
                piece_on[cap_sq]=(Piece)0xF;
                bitboards[captured_piece]^=cap_mask;
                occupancy[enemy]^=cap_mask;
                occupancy[2]^=cap_mask;
                zobrist_hash^=ZOBRIST_PIECE[captured_piece][cap_sq];
                mg_pst -= PIECE_MG_PST[captured_piece][cap_sq];
                eg_pst -= PIECE_EG_PST[captured_piece][cap_sq];
            }else{
                bitboards[captured_piece]^=to_mask;
                occupancy[enemy]^=to_mask;
                occupancy[2]^=to_mask;
                zobrist_hash^=ZOBRIST_PIECE[captured_piece][to_square];
                mg_pst -= PIECE_MG_PST[captured_piece][to_square];
                eg_pst -= PIECE_EG_PST[captured_piece][to_square];
            }
        }
        
        static const int piece_values[12]={100,320,330,500,900,20000,100,320,330,500,900,20000};
        static const int phase_values[12]={0,1,1,2,4,0,0,1,1,2,4,0};
        if(is_capture){
            if((int)captured_piece<12){
                material_score+=captured_piece<6?-piece_values[captured_piece]:piece_values[captured_piece];
                phase_score-=phase_values[captured_piece];
            }
        }
        if(move_is_promotion(move)){
            Piece promotion=move_promotion_piece_from_flags(move,side_to_move);
            material_score+=piece<6?piece_values[promotion]-piece_values[piece]:-(piece_values[promotion]-piece_values[piece]);
            phase_score+=phase_values[promotion]-phase_values[piece];
        }
        
        if(move_is_castle(move)){
            uint64_t rook_from,rook_to;
            Piece rook_piece;
            int r_from_sq,r_to_sq;
            
            if(to_square==6){
                r_from_sq=7;r_to_sq=5;rook_piece=Piece::R;
                rook_from=1ULL<<7;rook_to=1ULL<<5;
                piece_on[7]=(Piece)0xF;piece_on[5]=Piece::R;
            }else if(to_square==2){
                r_from_sq=0;r_to_sq=3;rook_piece=Piece::R;
                rook_from=1ULL<<0;rook_to=1ULL<<3;
                piece_on[0]=(Piece)0xF;piece_on[3]=Piece::R;
            }else if(to_square==62){
                r_from_sq=63;r_to_sq=61;rook_piece=Piece::r;
                rook_from=1ULL<<63;rook_to=1ULL<<61;
                piece_on[63]=(Piece)0xF;piece_on[61]=Piece::r;
            }else{
                r_from_sq=56;r_to_sq=59;rook_piece=Piece::r;
                rook_from=1ULL<<56;rook_to=1ULL<<59;
                piece_on[56]=(Piece)0xF;piece_on[59]=Piece::r;
            }
            
            bitboards[rook_piece]^=rook_from|rook_to;
            occupancy[side_to_move]^=rook_from|rook_to;
            occupancy[2]^=rook_from|rook_to;
            zobrist_hash^=ZOBRIST_PIECE[rook_piece][r_from_sq];
            zobrist_hash^=ZOBRIST_PIECE[rook_piece][r_to_sq];
            mg_pst += PIECE_MG_PST[rook_piece][r_to_sq] - PIECE_MG_PST[rook_piece][r_from_sq];
            eg_pst += PIECE_EG_PST[rook_piece][r_to_sq] - PIECE_EG_PST[rook_piece][r_from_sq];
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
        
        if(flags==0x1){
            en_passant=(piece==Piece::P)?to_square-8:to_square+8;
        }else{
            en_passant=255;
        }
        
        zobrist_hash^=ZOBRIST_CASTLING[old_castling_rights&0xFu];
        zobrist_hash^=ZOBRIST_CASTLING[castling_rights&0xFu];
        zobrist_hash^=ZOBRIST_EP[old_en_passant==255?64:old_en_passant];
        zobrist_hash^=ZOBRIST_EP[en_passant==255?64:en_passant];
        zobrist_hash^=ZOBRIST_SIDE;
        
        if(side_to_move==BLACK)fullmove_number++;
        side_to_move=(side_to_move==WHITE)?BLACK:WHITE;
        enemy_color=(side_to_move==WHITE)?BLACK:WHITE;
    }

    void unmake_move(){
        history_ply--;
        UndoState &undo=history_stack[history_ply];
        
        uint16_t move=undo.move;
        castling_rights=undo.castling_rights;
        en_passant=undo.en_passant;
        halfmove_clock=undo.halfmove_clock;
        material_score=undo.material_score;
        phase_score=undo.phase_score;
        mg_pst=undo.mg_pst;
        eg_pst=undo.eg_pst;
        zobrist_hash=undo.zobrist_hash;
        Piece captured_piece=(Piece)undo.captured_piece;
        
        int from_square=move_from(move);
        int to_square=move_to(move);
        
        // Flip side first
        side_to_move=(side_to_move==WHITE)?BLACK:WHITE;
        enemy_color=(side_to_move==WHITE)?BLACK:WHITE;
        if(side_to_move==BLACK)fullmove_number--;
        
        // Determine the moving piece
        Piece piece;
        if(move_is_promotion(move)){
            piece=(side_to_move==WHITE)?Piece::P:Piece::p;
        }else{
            piece=piece_on[to_square];
        }
        
        piece_on[from_square]=piece;
        piece_on[to_square]=(Piece)0xF;
        
        uint64_t from_mask=1ULL<<from_square;
        uint64_t to_mask=1ULL<<to_square;
        
        if(move_is_promotion(move)){
            Piece promo=move_promotion_piece_from_flags(move,side_to_move);
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
        
        if(move_is_capture(move)){
            if(move_is_en_passant(move)){
                int cap_sq=(side_to_move==WHITE)?to_square-8:to_square+8;
                uint64_t cap_mask=1ULL<<cap_sq;
                piece_on[cap_sq]=captured_piece;
                bitboards[captured_piece]|=cap_mask;
                occupancy[enemy_color]|=cap_mask;
                occupancy[2]|=cap_mask;
            }else{
                piece_on[to_square]=captured_piece;
                bitboards[captured_piece]|=to_mask;
                occupancy[enemy_color]|=to_mask;
                occupancy[2]|=to_mask;
            }
        }
    }

    inline bool is_repetition(int count = 3) const {
        int occurrences = 1;
        int limit = std::min(history_ply, (int)halfmove_clock);
        int start_idx = history_ply - 2;

        for (int i = start_idx; i >= history_ply - limit; i -= 2) {
            if (i >= 0 && history_stack[i].zobrist_hash == zobrist_hash) {
                occurrences++;
                if (occurrences >= count) return true;
            }
        }
        return false;
    }
};