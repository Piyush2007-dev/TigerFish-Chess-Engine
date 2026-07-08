#include <cstdint>
#include <array>
#include <utility>
#include <string>
#include <bit>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <vector>


//magic bb version  468 ns benchmark prev without was 611 ns

using namespace std;

enum Direction{
    NORTH = 0,
    SOUTH = 1,
    EAST = 2,
    WEST = 3,
    NE = 4,
    SE = 5,
    NW = 6,
    SW = 7,
};

enum Piece{
    P = 0,
    N = 1,
    B = 2,
    R = 3,
    Q = 4,
    K = 5,
    p = 6,
    n = 7,
    b = 8,
    r = 9,
    q = 10,
    k = 11
};

enum Color{
    WHITE = 0,
    BLACK = 1
};

enum CastleRights{
    WHITE_KING_SIDE = 1,
    WHITE_QUEEN_SIDE = 2,
    BLACK_KING_SIDE = 4,
    BLACK_QUEEN_SIDE = 8
};

enum GameResult {
    GAME_ONGOING,
    GAME_CHECKMATE,
    GAME_STALEMATE,
    GAME_FIFTY_MOVE_DRAW,
    GAME_SEVENTY_FIVE_MOVE_DRAW,
    GAME_INSUFFICIENT_MATERIAL
};


int lsb_index(uint64_t mask){
    return countr_zero(mask);
}

int msb_index(uint64_t mask){
    return 63 - countl_zero(mask);
}

int pop_lsb(uint64_t &mask){
    int sq = lsb_index(mask);
    mask &= mask - 1;
    return sq;
}

void print_bitboard(uint64_t bb){
    for(int r=7;r>=0;r--){
        for(int f=0;f<8;f++){
            int sq=r*8+f;
            cout << (((bb>>sq)&1ULL)?'1':'.') << ' ';
        }
        cout << '\n';
    }
    cout << '\n';
}

uint64_t NOT_A_FILE = 0xFEFEFEFEFEFEFEFEULL;
uint64_t NOT_H_FILE = 0x7F7F7F7F7F7F7F7FULL;
uint64_t NOT_AB_FILE = 0xFCFCFCFCFCFCFCFCULL;
uint64_t NOT_GH_FILE = 0x3F3F3F3F3F3F3F3FULL;

uint64_t RANK_1 = 0x00000000000000FFULL;
uint64_t RANK_2 = 0x000000000000FF00ULL;
uint64_t RANK_7 = 0x00FF000000000000ULL;
uint64_t RANK_8 = 0xFF00000000000000ULL;

array<array<uint64_t, 64>, 8> ray_table;

void init_rays(){
    int DR[8] = {1, -1, 0, 0, 1, -1, 1, -1};
    int DF[8] = {0, 0, 1, -1, 1, 1, -1, -1};

    for (int sq = 0; sq < 64; ++sq){
        int rank = sq / 8;
        int file = sq % 8;
        for (int d = 0; d < 8; ++d){
            uint64_t bb = 0;
            int r = rank + DR[d], f = file + DF[d];
            while (r >= 0 && r < 8 && f >= 0 && f < 8){
                bb |= 1ULL << (r * 8 + f);
                r += DR[d];
                f += DF[d];
            }
            ray_table[d][sq] = bb;
        }
    }
}

uint64_t get_ray(int direction, int square){
    return ray_table[direction][square];
}
// ── Magic Bitboard Tables ────────────────────────────────────────────────

// Rook magic numbers (one per square) — standard fixed magics
const uint64_t ROOK_MAGICS[64] = {
    0x8a80104000800020ULL, 0x140002000100040ULL,  0x2801880a0017001ULL,
    0x100081001000420ULL,  0x200020010080420ULL,  0x3001c0002010008ULL,
    0x8480008002000100ULL, 0x2080088004402900ULL, 0x800098204000ULL,
    0x2024401000200040ULL, 0x100802000801000ULL,  0x120800800801000ULL,
    0x208808088000400ULL,  0x2802200800400ULL,    0x2200800100020080ULL,
    0x801000060821100ULL,  0x80044006422000ULL,   0x100808020004000ULL,
    0x12108a0010204200ULL, 0x140848010000802ULL,  0x481828014002800ULL,
    0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL,  0x2040002120081000ULL, 0x21200680100081ULL,
    0x20100080080080ULL,   0x2000a00200410ULL,    0x20080800400ULL,
    0x80088400100102ULL,   0x80004600042881ULL,   0x4040008040800020ULL,
    0x440003000200801ULL,  0x4200011004500ULL,    0x188020010100100ULL,
    0x14800401802800ULL,   0x2080040080800200ULL, 0x124080204001001ULL,
    0x200046502000484ULL,  0x480400080088020ULL,  0x1000422010034000ULL,
    0x30200100110040ULL,   0x100021010009ULL,     0x2002080100110004ULL,
    0x202008004008002ULL,  0x20020004010100ULL,   0x2048440040820001ULL,
    0x101002200408200ULL,  0x40802000401080ULL,   0x4008142004410100ULL,
    0x2060820c0120200ULL,  0x1001004080100ULL,    0x20c020080040080ULL,
    0x2935610830022400ULL, 0x44440041009200ULL,   0x280001040802101ULL,
    0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL,    0x12001008414402ULL,   0x2006104900a0804ULL,
    0x1004081002402ULL
};

// Bishop magic numbers (one per square)
const uint64_t BISHOP_MAGICS[64] = {
    0x40040844404084ULL,   0x2004208a004208ULL,   0x10190041080202ULL,
    0x108060845042010ULL,  0x581104180800210ULL,  0x2112080446200010ULL,
    0x1080820820060210ULL, 0x3c0808410220200ULL,  0x4050404440404ULL,
    0x21001420088ULL,      0x24d0080801082102ULL, 0x1020a0a020400ULL,
    0x40308200402ULL,      0x4011002100800ULL,    0x401484104104005ULL,
    0x801010402020200ULL,  0x400210c3880100ULL,   0x404022024108200ULL,
    0x810018200204102ULL,  0x4002801a02003ULL,    0x85040820080400ULL,
    0x810102c808880400ULL, 0xe900410884800ULL,    0x8002020480840102ULL,
    0x220200865090201ULL,  0x2010100a02021202ULL, 0x152048408022401ULL,
    0x20080002081110ULL,   0x4001001021004000ULL, 0x800040400a011002ULL,
    0xe4004081011002ULL,   0x1c004001012080ULL,   0x8004200962a00220ULL,
    0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
    0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL,
    0x42008c0340209202ULL, 0x209188240001000ULL,  0x400408a884001800ULL,
    0x110400a6080400ULL,   0x1840060a44020800ULL, 0x90080104000041ULL,
    0x201011000808101ULL,  0x1a2208080504f080ULL, 0x8012020600211212ULL,
    0x500861011240000ULL,  0x180806108200800ULL,  0x4000020e01040044ULL,
    0x300000261044000aULL, 0x802241102020002ULL,  0x20906061210001ULL,
    0x5a84841004010310ULL, 0x4010801011c04ULL,    0xa010109502200ULL,
    0x4a02012000ULL,       0x500201010098b028ULL, 0x8040002811040900ULL,
    0x28000010020204ULL,   0x6000020202d0240ULL,  0x8918844842082200ULL,
    0x4010011029020020ULL
};

// Bits in the attack table index per square
// rook:   64 - popcount(rook_mask[sq])
// bishop: 64 - popcount(bishop_mask[sq])
int ROOK_SHIFT[64];
int BISHOP_SHIFT[64];

// Masks (relevant occupancy squares, edges excluded)
uint64_t ROOK_MASK[64];
uint64_t BISHOP_MASK[64];

// Attack tables
// Rook: up to 2^12 = 4096 entries per square
// Bishop: up to 2^9 = 512 entries per square
uint64_t ROOK_ATTACKS[64][4096];
uint64_t BISHOP_ATTACKS[64][512];


// Compute rook attacks for a given square and blocker set (used only during init)
static uint64_t compute_rook_attacks(int sq, uint64_t blockers) {
    uint64_t attacks = 0;
    int r = sq / 8, f = sq % 8;
    // North
    for (int i = r+1; i < 8; i++) { attacks |= (1ULL<<(i*8+f)); if (blockers & (1ULL<<(i*8+f))) break; }
    // South
    for (int i = r-1; i >= 0; i--) { attacks |= (1ULL<<(i*8+f)); if (blockers & (1ULL<<(i*8+f))) break; }
    // East
    for (int i = f+1; i < 8; i++) { attacks |= (1ULL<<(r*8+i)); if (blockers & (1ULL<<(r*8+i))) break; }
    // West
    for (int i = f-1; i >= 0; i--) { attacks |= (1ULL<<(r*8+i)); if (blockers & (1ULL<<(r*8+i))) break; }
    return attacks;
}

// Compute bishop attacks for a given square and blocker set (used only during init)
static uint64_t compute_bishop_attacks(int sq, uint64_t blockers) {
    uint64_t attacks = 0;
    int r = sq / 8, f = sq % 8;
    // NE
    for (int i=r+1,j=f+1; i<8&&j<8; i++,j++) { attacks|=(1ULL<<(i*8+j)); if(blockers&(1ULL<<(i*8+j))) break; }
    // NW
    for (int i=r+1,j=f-1; i<8&&j>=0; i++,j--) { attacks|=(1ULL<<(i*8+j)); if(blockers&(1ULL<<(i*8+j))) break; }
    // SE
    for (int i=r-1,j=f+1; i>=0&&j<8; i--,j++) { attacks|=(1ULL<<(i*8+j)); if(blockers&(1ULL<<(i*8+j))) break; }
    // SW
    for (int i=r-1,j=f-1; i>=0&&j>=0; i--,j--) { attacks|=(1ULL<<(i*8+j)); if(blockers&(1ULL<<(i*8+j))) break; }
    return attacks;
}

// Iterate all subsets of a mask (carry-ripple trick)
static uint64_t next_subset(uint64_t sub, uint64_t mask) {
    return (sub - 1) & mask;
}

void init_magics() {
    for (int sq = 0; sq < 64; sq++) {
        int r = sq / 8, f = sq % 8;

        // ── Rook mask: rank + file, exclude edges ──────────────────────
        uint64_t rmask = 0;
        for (int i = r+1; i < 7; i++) rmask |= (1ULL << (i*8+f));  // north, exclude rank 8
        for (int i = r-1; i > 0; i--) rmask |= (1ULL << (i*8+f));  // south, exclude rank 1
        for (int i = f+1; i < 7; i++) rmask |= (1ULL << (r*8+i));  // east,  exclude file h
        for (int i = f-1; i > 0; i--) rmask |= (1ULL << (r*8+i));  // west,  exclude file a
        ROOK_MASK[sq]  = rmask;
        ROOK_SHIFT[sq] = 64 - __builtin_popcountll(rmask);

        // ── Bishop mask: diagonals, exclude edges ──────────────────────
        uint64_t bmask = 0;
        for (int i=r+1,j=f+1; i<7&&j<7; i++,j++) bmask |= (1ULL<<(i*8+j));
        for (int i=r+1,j=f-1; i<7&&j>0; i++,j--) bmask |= (1ULL<<(i*8+j));
        for (int i=r-1,j=f+1; i>0&&j<7; i--,j++) bmask |= (1ULL<<(i*8+j));
        for (int i=r-1,j=f-1; i>0&&j>0; i--,j--) bmask |= (1ULL<<(i*8+j));
        BISHOP_MASK[sq]  = bmask;
        BISHOP_SHIFT[sq] = 64 - __builtin_popcountll(bmask);

        // ── Fill rook attack table ──────────────────────────────────────
        uint64_t rsub = 0;
        do {
            int idx = (int)((rsub * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]);
            ROOK_ATTACKS[sq][idx] = compute_rook_attacks(sq, rsub);
            rsub = next_subset(rsub, rmask);
        } while (rsub != 0);

        // ── Fill bishop attack table ────────────────────────────────────
        uint64_t bsub = 0;
        do {
            int idx = (int)((bsub * BISHOP_MAGICS[sq]) >> BISHOP_SHIFT[sq]);
            BISHOP_ATTACKS[sq][idx] = compute_bishop_attacks(sq, bsub);
            bsub = next_subset(bsub, bmask);
        } while (bsub != 0);
    }
}

// ── Runtime lookup functions — use these everywhere ──────────────────────
inline uint64_t rook_attacks(int sq, uint64_t occ) {
    occ &= ROOK_MASK[sq];
    return ROOK_ATTACKS[sq][(occ * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]];
}

inline uint64_t bishop_attacks(int sq, uint64_t occ) {
    occ &= BISHOP_MASK[sq];
    return BISHOP_ATTACKS[sq][(occ * BISHOP_MAGICS[sq]) >> BISHOP_SHIFT[sq]];
}

inline uint64_t queen_attacks(int sq, uint64_t occ) {
    return rook_attacks(sq, occ) | bishop_attacks(sq, occ);
}
class Move{
    public:
    uint32_t value;

    static int from(Move move){
        return (int)(move.value & 0x3Fu);
    }
    static int to(Move move){
        return (int)((move.value >> 6) & 0x3Fu);
    }
    static Piece piece(Move move){
        return (Piece)((move.value >> 12) & 0xFu);
    }
    static Piece captured_piece(Move move){
        return (Piece)((move.value >> 16) & 0xFu);
    }
    static Piece promotion_piece(Move move){
        return (Piece)((move.value >> 20) & 0xFu);
    }

    // derived
    static bool is_capture(Move move){
        return ((move.value >> 16) & 0xFu) != 0xFu;
    }
    static bool is_promotion(Move move){
        return ((move.value >> 20) & 0xFu) != 0xFu;
    }

    static uint8_t castling_rights(Move move){
        return (uint8_t)((move.value >> 24) & 0xFu);
    }

    static bool is_castle(Move move){
        return ((move.value >> 28) & 0x1u) != 0;
    }
    static bool is_en_passant(Move move){
        return ((move.value >> 29) & 0x1u) != 0;
    }
    static bool is_double_push(Move move){
        return ((move.value >> 30) & 0x1u) != 0;
    }

    // bit 31: side that made this move (0=WHITE, 1=BLACK)
    static Color side(Move move){
        return (Color)((move.value >> 31) & 0x1u);
    }
};

struct PackedMove{
    Piece piece;
    int from_square;
    int to_square;

    uint32_t captured_piece  = 0xFu;   
    uint32_t promotion_piece = 0xFu;   

    bool is_castle      = false;
    bool is_en_passant  = false;
    bool is_double_push = false;

    static uint32_t pack(PackedMove d){
        uint32_t move = 0;
        move  = ((uint32_t)d.from_square & 0x3Fu);
        move |= (((uint32_t)d.to_square  & 0x3Fu) << 6);
        move |= (((uint32_t)d.piece      & 0xFu)  << 12);
        move |= ((d.captured_piece       & 0xFu)  << 16);
        move |= ((d.promotion_piece      & 0xFu)  << 20);
        
        if (d.is_castle)      move |= 1u << 28;
        if (d.is_en_passant)  move |= 1u << 29;
        if (d.is_double_push) move |= 1u << 30;

        return move;
    }
};

struct MoveList{
    Move move_list[218];
    int count = 0;
    void clear() { count = 0; }
    void push(Move move) { move_list[count++] = move; }
    int size() { return count; }
};

class Board{
public:
    array<uint64_t,12> bitboards;
    array<uint64_t,3> occupancy;
    Piece piece_on[64];
    Color side_to_move = WHITE;
    Color enemy_color=BLACK;
    uint8_t castling_rights = 0;
    uint8_t en_passant = 255;
    int halfmove_clock = 0;
    int fullmove_number = 1;

    string START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    vector<Move> move_history;
    vector<uint8_t> ep_history;   
    vector<uint8_t> halfmove_history;

    bool is_in_check();
    bool is_insufficient_material() const;
    GameResult get_game_result();

    Board(){
        move_history.reserve(4096);
        ep_history.reserve(4096);
        halfmove_history.reserve(4096);
        init_rays();
        init_magics();
        set_fen(START_FEN);
    }

    void set_fen(string fen_string){
        move_history.clear();
        ep_history.clear();
        halfmove_history.clear();
        for (int i=0;i<12;i++){
            bitboards[i] = 0;
        }
        for(int i=0;i<64;i++){
            piece_on[i]=(Piece)0xF;
        }
        side_to_move = WHITE;
        castling_rights = 0;
        en_passant = 255;
        halfmove_clock = 0;
        fullmove_number = 1;

        istringstream ss(fen_string);
        string fields[6];
        int nfields = 0;

        while (nfields < 6 && ss >> fields[nfields])
            ++nfields;

        string placement = fields[0];
        int rank = 7, file = 0;

        for (char c : placement){
            int square = rank * 8 + file;
            switch (c)
            {
            case 'p':
                bitboards[Piece::p] |= 1ULL << square;
                piece_on[square] = Piece::p;
                ++file;
                break;
            case 'n':
                bitboards[Piece::n] |= 1ULL << square;
                piece_on[square] = Piece::n;
                ++file;
                break;
            case 'b':
                bitboards[Piece::b] |= 1ULL << square;
                piece_on[square] = Piece::b;
                ++file;
                break;
            case 'r':
                bitboards[Piece::r] |= 1ULL << square;
                piece_on[square] = Piece::r;
                ++file;
                break;
            case 'q':
                bitboards[Piece::q] |= 1ULL << square;
                piece_on[square] = Piece::q;
                ++file;
                break;
            case 'k':
                bitboards[Piece::k] |= 1ULL << square;
                piece_on[square] = Piece::k;
                ++file;
                break;
            case 'P':
                bitboards[Piece::P] |= 1ULL << square;
                piece_on[square] = Piece::P;
                ++file;
                break;
            case 'N':
                bitboards[Piece::N] |= 1ULL << square;
                piece_on[square] = Piece::N;
                ++file;
                break;
            case 'B':
                bitboards[Piece::B] |= 1ULL << square;
                piece_on[square] = Piece::B;
                ++file;
                break;
            case 'R':
                bitboards[Piece::R] |= 1ULL << square;
                piece_on[square] = Piece::R;
                ++file;
                break;
            case 'Q':
                bitboards[Piece::Q] |= 1ULL << square;
                piece_on[square] = Piece::Q;
                ++file;
                break;
            case 'K':
                bitboards[Piece::K] |= 1ULL << square;
                piece_on[square] = Piece::K;
                ++file;
                break;
            case '/':
                --rank;
                file = 0;
                break;
            case '1':
                file += 1;
                break;
            case '2':
                file += 2;
                break;
            case '3':
                file += 3;
                break;
            case '4':
                file += 4;
                break;
            case '5':
                file += 5;
                break;
            case '6':
                file += 6;
                break;
            case '7':
                file += 7;
                break;
            case '8':
                file += 8;
                break;
            default:
                break;
            }
        }

        if (nfields > 1){
            side_to_move = (fields[1][0] == 'w') ? WHITE : BLACK;
        }

        if (nfields > 2){
            string cr = fields[2];
            castling_rights = 0;
            for (char c : cr)
            {
                if (c == 'K')
                    castling_rights |= WHITE_KING_SIDE;
                else if (c == 'Q')
                    castling_rights |= WHITE_QUEEN_SIDE;
                else if (c == 'k')
                    castling_rights |= BLACK_KING_SIDE;
                else if (c == 'q')
                    castling_rights |= BLACK_QUEEN_SIDE;
            }
        }

        if(nfields > 3){
            string ep = fields[3];
            if (ep != "-" && ep.length() == 2){
                int f = ep[0] - 'a';
                int r = ep[1] - '1';
                en_passant = r * 8 + f;
            }
            else{
                en_passant = 255;
            }
        }

        if(nfields > 4)
            halfmove_clock = stoi(fields[4]);


        
            if(nfields > 5)
            fullmove_number = stoi(fields[5]);

        update_occupancy();
    }
    string to_fen() {
    string fen = "";
    // 1. Piece placement
    for (int rank = 7; rank >= 0; --rank) {
        int empty_count = 0;
        for (int file = 0; file < 8; ++file) {
            int sq = rank * 8 + file;
            Piece p = piece_on[sq];
            if (p == 0xF) { // empty
                empty_count++;
            } else {
                if (empty_count > 0) {
                    fen += to_string(empty_count);
                    empty_count = 0;
                }
                char piece_chars[12] = {
                    'P', 'N', 'B', 'R', 'Q', 'K',
                    'p', 'n', 'b', 'r', 'q', 'k'
                };
                fen += piece_chars[p];
            }
        }
        if (empty_count > 0) {
            fen += to_string(empty_count);
        }
        if (rank > 0) {
            fen += "/";
        }
    }

    // 2. Side to move
    fen += (side_to_move == WHITE) ? " w " : " b ";

    // 3. Castling rights
    string castling = "";
    if (castling_rights & WHITE_KING_SIDE) castling += "K";
    if (castling_rights & WHITE_QUEEN_SIDE) castling += "Q";
    if (castling_rights & BLACK_KING_SIDE) castling += "k";
    if (castling_rights & BLACK_QUEEN_SIDE) castling += "q";
    if (castling == "") castling = "-";
    fen += castling + " ";

    // 4. En passant
    if (en_passant != 255) {
        int file = en_passant % 8;
        int rank = en_passant / 8;
        fen += (char)('a' + file);
        fen += (char)('1' + rank);
    } else {
        fen += "-";
    }

    // 5. Halfmove clock
    fen += " " + to_string(halfmove_clock);

    // 6. Fullmove number
    fen += " " + to_string(fullmove_number);

    return fen;
}
    void update_occupancy(){
        occupancy[Color::WHITE] = 0;
        for (int i = 0; i < 6; ++i)
            occupancy[Color::WHITE] |= bitboards[i];

        occupancy[Color::BLACK] = 0;
        for (int i = 6; i < 12; ++i)
            occupancy[Color::BLACK] |= bitboards[i];

        occupancy[2] = occupancy[Color::WHITE] | occupancy[Color::BLACK];
    }

    void print_board(){
        char piece_chars[12] = {
            'P', 'N', 'B', 'R', 'Q', 'K',
            'p', 'n', 'b', 'r', 'q', 'k'};
        for (int rank = 7; rank >= 0; --rank){
            cout << (rank + 1) << " ";
            for (int file = 0; file < 8; ++file){
                int square = rank * 8 + file;
                char piece = '.';
                for (int p = 0; p < 12; ++p){
                    if ((bitboards[p] >> square) & 1ULL){
                        piece = piece_chars[p];
                        break;
                    }
                }
                cout << piece << ' ';
            }
            cout << '\n';
        }
        cout << "  a b c d e f g h\n";
    }

    void verify_board(Board& board)
{
    uint64_t white_occ = 0;
    uint64_t black_occ = 0;

    for (int p = Piece::P; p <= Piece::K; p++)
        white_occ |= board.bitboards[p];

    for (int p = Piece::p; p <= Piece::k; p++)
        black_occ |= board.bitboards[p];

    uint64_t all_occ = white_occ | black_occ;

    if (white_occ != board.occupancy[WHITE])
    {
        cout << "WHITE OCC MISMATCH\n";

        cout << "Expected:\n";
        print_bitboard(white_occ);

        cout << "Actual:\n";
        print_bitboard(board.occupancy[WHITE]);
    }

    if (black_occ != board.occupancy[BLACK])
    {
        cout << "BLACK OCC MISMATCH\n";

        cout << "Expected:\n";
        print_bitboard(black_occ);

        cout << "Actual:\n";
        print_bitboard(board.occupancy[BLACK]);
    }

    if (all_occ != board.occupancy[2])
    {
        cout << "ALL OCC MISMATCH\n";
    }
}
    void make_move(Move move, MoveList &moves){
        Color enemy =
    (side_to_move == WHITE)
    ? BLACK
    : WHITE;
        move.value&=~(0xFu<<24);//24 to 27 bits 0
        move.value|=((uint32_t)(castling_rights&0xF)<<24);//24 to 27 are setted to current rights

        if (side_to_move == BLACK){
            move.value |= (1u << 31);
            fullmove_number++;
        }

        ep_history.push_back(en_passant);
        halfmove_history.push_back((uint8_t)halfmove_clock);
        move_history.push_back(move);

        Piece piece= Move::piece(move);
        int from_square= Move::from(move);
        int to_square= Move::to(move);
        bool is_capture= Move::is_capture(move);

        if (piece == Piece::P || piece == Piece::p || is_capture) {
            halfmove_clock = 0;
        } else {
            halfmove_clock++;
        }

        uint64_t from_mask = 1ULL << from_square;
        uint64_t to_mask = 1ULL << to_square;

        if (is_capture && !Move::is_en_passant(move)){
            Piece captured = piece_on[to_square];
        
            bitboards[captured] ^= to_mask;
            occupancy[enemy] ^= to_mask;
            occupancy[2] ^= to_mask;
        }
        
        piece_on[from_square] = (Piece)0xF;
        piece_on[to_square] = piece;
        
        bitboards[piece] ^= from_mask | to_mask;
        occupancy[side_to_move] ^= from_mask | to_mask;
        occupancy[2] ^= from_mask | to_mask;

     // till here capture and nornal moves are handled

        if (Move::is_promotion(move)){
            bitboards[piece]^=to_mask;
            Piece promo=Move::promotion_piece(move);
            bitboards[promo]^=to_mask;
            piece_on[to_square] = promo;
        }

        if (Move::is_castle(move)){
            uint64_t rook_from,rook_to;
            Piece rook_piece;

            if (to_square == 6){
                rook_from = 1ULL << 7;
                rook_to = 1ULL << 5;
                rook_piece = Piece::R;
                piece_on[7] = (Piece)0xF;
                piece_on[5] = rook_piece;

            }

            else if (to_square == 2){
                rook_from = 1ULL << 0;
                rook_to = 1ULL << 3;
                rook_piece = Piece::R;
                piece_on[0] = (Piece)0xF;
                piece_on[3] = rook_piece;

            }

            else if (to_square == 62){
                rook_from = 1ULL << 63;
                rook_to = 1ULL << 61;
                rook_piece = Piece::r;
                piece_on[63] = (Piece)0xF;
                piece_on[61] = rook_piece;

            }

            else{
                rook_from = 1ULL << 56;
                rook_to = 1ULL << 59;
                rook_piece = Piece::r;
                piece_on[56] = (Piece)0xF;
                piece_on[59] = rook_piece;

            }

            bitboards[rook_piece] ^= rook_from | rook_to;
            occupancy[side_to_move] ^= rook_from | rook_to;
            occupancy[2] ^= rook_from | rook_to;
        }

        if (Move::is_en_passant(move)){
            int cap_sq = (side_to_move == WHITE) ? to_square - 8 : to_square + 8;
            piece_on[cap_sq] = (Piece)0xF;
            Piece cap_pawn = (side_to_move == WHITE) ? Piece::p : Piece::P;
            uint64_t cap_mask = 1ULL << cap_sq;
            bitboards[cap_pawn] ^= cap_mask;
            occupancy[enemy] ^= cap_mask;
            occupancy[2] ^= cap_mask;
        }

        // castliong rights handeling 
        if (piece == Piece::K){
            castling_rights &= ~(WHITE_KING_SIDE | WHITE_QUEEN_SIDE);
        }
        if (piece == Piece::k){
            castling_rights &= ~(BLACK_KING_SIDE | BLACK_QUEEN_SIDE);
        }
        if (piece == Piece::R){
            if (from_square == 7){
                castling_rights &= ~WHITE_KING_SIDE;
            }
            if (from_square == 0){
                castling_rights &= ~WHITE_QUEEN_SIDE;
            }
        }
        if (piece == Piece::r){
            if (from_square == 63){
                castling_rights &= ~BLACK_KING_SIDE;
            }
            if (from_square == 56){
                castling_rights &= ~BLACK_QUEEN_SIDE;
            }
        }
        if (is_capture){//if any rooks gets captured
            if (to_square == 7)  castling_rights &= ~WHITE_KING_SIDE;
            if (to_square == 0)  castling_rights &= ~WHITE_QUEEN_SIDE;
            if (to_square == 63) castling_rights &= ~BLACK_KING_SIDE;
            if (to_square == 56) castling_rights &= ~BLACK_QUEEN_SIDE;
        }

        // Set en_passant target 
       if (Move::is_double_push(move)){
            if (piece == Piece::P){
            en_passant = to_square - 8;
            }
            else{
            en_passant = to_square + 8;
            }
        }
        else{
            en_passant = 255;
        }

        side_to_move = (side_to_move == WHITE) ? BLACK : WHITE;
        enemy_color = (side_to_move == WHITE) ? BLACK : WHITE;

        moves.clear();
    }

    void unmake_move(){
        Move move = move_history.back();
        move_history.pop_back();

        castling_rights = Move::castling_rights(move);
        en_passant = ep_history.back();
        ep_history.pop_back();

        halfmove_clock = halfmove_history.back();
        halfmove_history.pop_back();

        Piece piece = Move::piece(move);
        int from_square = Move::from(move);
        int to_square = Move::to(move);
        bool is_capture= Move::is_capture(move);

        uint64_t from_mask = 1ULL << from_square;
        uint64_t to_mask = 1ULL << to_square;

        side_to_move = (side_to_move == WHITE) ? BLACK : WHITE;
        enemy_color= (side_to_move == WHITE) ? BLACK : WHITE;

        if (side_to_move == BLACK) {
            fullmove_number--;
        }

        piece_on[from_square] = piece;
        piece_on[to_square] = (Piece)0xF;

        if (Move::is_promotion(move)){
            Piece promo = Move::promotion_piece(move);
            bitboards[promo] ^= to_mask;
            bitboards[piece] ^= from_mask;
        
        }
        else{
            bitboards[piece] ^= from_mask | to_mask;
        }

        
        if (Move::is_castle(move)){
            uint64_t rook_from, rook_to;
            Piece rook_piece;

            if (to_square == 6){
                rook_from = 1ULL << 7;
                rook_to = 1ULL << 5;
                rook_piece = Piece::R;
                piece_on[7] = Piece::R;
                piece_on[5] = (Piece)0xF;
            }
            else if (to_square == 2){
                rook_from = 1ULL << 0;
                rook_to = 1ULL << 3;
                rook_piece = Piece::R;
                piece_on[0] = Piece::R;
                piece_on[3] = (Piece)0xF;
            }
            else if (to_square == 62){
                rook_from = 1ULL << 63;
                rook_to = 1ULL << 61;
                rook_piece = Piece::r;
                piece_on[63] = Piece::r;
                piece_on[61] = (Piece)0xF;
            }
            else{
                rook_from = 1ULL << 56;
                rook_to = 1ULL << 59;
                rook_piece = Piece::r;
                piece_on[56] = Piece::r;
                piece_on[59] = (Piece)0xF;
            }
            bitboards[rook_piece] ^= rook_from | rook_to;
            occupancy[side_to_move] ^= rook_from | rook_to;
            occupancy[2] ^= rook_from | rook_to;
        }


        occupancy[side_to_move] ^= from_mask | to_mask;
        occupancy[2]^= from_mask | to_mask;

        if (is_capture){
            if (Move::is_en_passant(move)){
                int cap_sq = (Move::side(move) == WHITE) ? to_square - 8 : to_square + 8;
                Piece cap_pawn = (Move::side(move) == WHITE) ? Piece::p : Piece::P;
                uint64_t cap_mask = 1ULL << cap_sq;
                piece_on[cap_sq] = cap_pawn;
                bitboards[cap_pawn] |= cap_mask;
                occupancy[enemy_color] |= cap_mask;
                occupancy[2] |= cap_mask;
            }
            else{
                Piece captured_piece = Move::captured_piece(move);
                piece_on[to_square] = captured_piece;
                bitboards[captured_piece] |= to_mask;
                occupancy[enemy_color] |= to_mask;
                occupancy[2] |= to_mask;
            }
        }
    }
};
struct PinInfo{
    uint64_t pinned = 0;
    uint64_t pin_mask[64];

    PinInfo(){
        for (int i = 0; i < 64; i++)
            pin_mask[i] = ~0ULL;
    }
};
PinInfo find_pins(Board &board) {
    PinInfo pins;

    Piece king = (board.side_to_move == WHITE) ? Piece::K : Piece::k;
    int king_sq = lsb_index(board.bitboards[king]);
    uint64_t friendly = board.occupancy[board.side_to_move];

    // opposite direction lookup: NORTH<->SOUTH, EAST<->WEST, NE<->SW, SE<->NW
    int opposite_dir[8] = {1, 0, 3, 2, 7, 6, 5, 4};

    for (int dir = 0; dir < 8; dir++) {
        uint64_t ray = get_ray(dir, king_sq);
        uint64_t blockers = ray & board.occupancy[2];
        if (!blockers)
            continue;

        int first = (dir % 2 == 0) ? lsb_index(blockers) : msb_index(blockers);
        uint64_t first_bb = 1ULL << first;

        // first blocker must be a friendly piece to be pinned
        if (!(first_bb & friendly))
            continue;

        // look for second blocker beyond the friendly piece
        blockers &= ~first_bb;
        if (!blockers)
            continue;

        int second = (dir % 2 == 0) ? lsb_index(blockers) : msb_index(blockers);
        Piece p = board.piece_on[second];

        // pinning piece must be an enemy piece
        if (((p <= Piece::K) ? WHITE : BLACK) == board.side_to_move)
            continue;

        // check if second piece is a slider that attacks along this direction
        bool slider = false;
        if (dir < 4)
            slider = (p == Piece::r || p == Piece::q ||
                      p == Piece::R || p == Piece::Q);
        else
            slider = (p == Piece::b || p == Piece::q ||
                      p == Piece::B || p == Piece::Q);

        if (!slider)
            continue;

        // pin confirmed — allow movement along the full ray through the king
        pins.pinned |= first_bb;
        pins.pin_mask[first] = ray | get_ray(opposite_dir[dir], king_sq);
    }

    return pins;
}

    bool ep_exposes_king(Board &board, int from_sq, int ep_sq){
        // Simulate the occupancy after the EP capture and check if the king
        // is attacked from any direction.
        Color mover   = board.side_to_move;
        Color enemy   = (mover == WHITE) ? BLACK : WHITE;
        Piece king_pc = (mover == WHITE) ? Piece::K : Piece::k;
        int   king_sq = lsb_index(board.bitboards[king_pc]);

        int captured_sq = (mover == WHITE) ? ep_sq - 8 : ep_sq + 8;

        // Build the occupancy as it will look after the EP move
        uint64_t occ = board.occupancy[2];
        occ &= ~(1ULL << from_sq);      // capturing pawn leaves its square
        occ &= ~(1ULL << captured_sq);  // captured pawn is removed
        occ |=  (1ULL << ep_sq);        // capturing pawn arrives

        uint64_t enemy_occ = board.occupancy[enemy] & ~(1ULL << captured_sq);

        // Check rook/queen attacks (rank and file)
        for (int dir = 0; dir < 4; dir++) {
            uint64_t ray = get_ray(dir, king_sq);
            uint64_t blockers = ray & occ;
            if (!blockers) continue;
            int blocker_sq = (dir % 2 == 0) ? lsb_index(blockers) : msb_index(blockers);
            if (!((enemy_occ) & (1ULL << blocker_sq))) continue;
            int start = (enemy == WHITE) ? 0 : 6;
            Piece p = board.piece_on[blocker_sq];
            if (p == Piece::r || p == Piece::R || p == Piece::q || p == Piece::Q)
                return true;
        }

        // Check bishop/queen attacks (diagonals)
        for (int dir = 4; dir < 8; dir++) {
            uint64_t ray = get_ray(dir, king_sq);
            uint64_t blockers = ray & occ;
            if (!blockers) continue;
            int blocker_sq = (dir % 2 == 0) ? lsb_index(blockers) : msb_index(blockers);
            if (!((enemy_occ) & (1ULL << blocker_sq))) continue;
            int start = (enemy == WHITE) ? 0 : 6;
            Piece p = board.piece_on[blocker_sq];
            if (p == Piece::b || p == Piece::B || p == Piece::q || p == Piece::Q)
                return true;
        }

        return false;
    }

    pair<uint64_t, uint64_t> knight_attacks(uint64_t lsb,uint64_t all_occ,uint64_t enemy_occ){
    uint64_t l1 = (lsb & NOT_H_FILE) << 1;
    uint64_t l2 = (lsb & NOT_GH_FILE) << 2;
    uint64_t r1 = (lsb & NOT_A_FILE) >> 1;
    uint64_t r2 = (lsb & NOT_AB_FILE) >> 2;

    uint64_t h1 = l1 | r1;
    uint64_t h2 = l2 | r2;

    uint64_t moves_mask = (h1 << 16) | (h1 >> 16) | (h2 << 8) | (h2 >> 8);

    uint64_t captures = moves_mask & enemy_occ;
    moves_mask &= ~all_occ;
    return {moves_mask, captures};
}

Color piece_color(Piece p){
    return (p <= Piece::K) ? WHITE : BLACK;
}

uint64_t append_sliding_moves(MoveList &moves, Board &board, Piece piece,
    int dir_start, int dir_end,
    const PinInfo &pins,
    uint64_t legal_mask = ~0ULL) {

Color enemy    = (piece_color(piece) == WHITE) ? BLACK : WHITE;
uint64_t all_occ   = board.occupancy[2];
uint64_t enemy_occ = board.occupancy[enemy];
uint64_t own_occ   = board.occupancy[piece_color(piece)];

PackedMove move_data;
move_data.piece = piece;

uint64_t attack_mask = 0;
uint64_t mask = board.bitboards[piece];

while (mask) {
int from_sq = lsb_index(mask);
move_data.from_square = from_sq;
uint64_t pmask = pins.pin_mask[from_sq];

// ── get attack bitboard via magic lookup ──────────────────────
uint64_t attacks;
if (dir_start == 0 && dir_end == 4)       // rook
attacks = rook_attacks(from_sq, all_occ);
else if (dir_start == 4 && dir_end == 8)  // bishop
attacks = bishop_attacks(from_sq, all_occ);
else                                        // queen
attacks = queen_attacks(from_sq, all_occ);

attacks &= ~own_occ;   // can't capture own pieces
attack_mask |= attacks;

// apply legality masks
uint64_t legal = attacks & legal_mask & pmask;

uint64_t quiets   = legal & ~all_occ;
uint64_t captures = legal & enemy_occ;

while (quiets) {
move_data.to_square      = pop_lsb(quiets);
move_data.captured_piece = 0xFu;
Move move;
move.value = PackedMove::pack(move_data);
moves.push(move);
}
while (captures) {
move_data.to_square      = pop_lsb(captures);
move_data.captured_piece =board.piece_on[move_data.to_square];
Move move;
move.value = PackedMove::pack(move_data);
moves.push(move);
}

mask &= mask - 1;
}

return attack_mask;
}
uint64_t append_knight_moves(MoveList &moves, Board &board, Piece piece,const PinInfo &pins, uint64_t legal_mask = ~0ULL){
    uint64_t all_occ = board.occupancy[2];
    Color enemy = (piece_color(piece) == WHITE) ? BLACK : WHITE;
    uint64_t enemy_occ = board.occupancy[enemy];

    PackedMove move_data;
    move_data.piece = piece;
    move_data.captured_piece = 0xFu;

    uint64_t attack_mask = 0;
    uint64_t mask = board.bitboards[piece];

    while (mask){
        uint64_t lsb = mask & (0ULL - mask);
        move_data.from_square = lsb_index(lsb);
        if (pins.pinned & lsb){
            mask &= mask - 1;
            continue;
        }
        auto pr = knight_attacks(lsb, all_occ, enemy_occ);
        uint64_t moves_mask = pr.first;
        uint64_t captures = pr.second;

        uint64_t piece_attack_mask = moves_mask | captures;

        moves_mask &=legal_mask;
        captures &=legal_mask;

        while (moves_mask){
            int to_square = pop_lsb(moves_mask);
            move_data.to_square = to_square;
            move_data.captured_piece = 0xFu;
            Move m;
            m.value = PackedMove::pack(move_data);
            moves.push(m);
        }
        while (captures){
            int to_square = pop_lsb(captures);
            move_data.to_square = to_square;
            move_data.captured_piece = board.piece_on[to_square];
            Move m;
            m.value = PackedMove::pack(move_data);
            moves.push(m);
        }

        attack_mask |= piece_attack_mask;

        mask &= mask - 1;
    }
    return attack_mask;
}
class MoveGenerator{
public:

    void PawnMoves(Board &board, MoveList &moves, const PinInfo &pins, uint64_t legal_mask = ~0ULL){
        const bool white = board.side_to_move == WHITE;

        Piece pawn_piece = white ? Piece::P : Piece::p;
        Piece enemy_pawn = white ? Piece::p : Piece::P;
        Color enemy = white ? BLACK : WHITE;
        int push = white ? 8 : -8;
        int left_side_shift = white ? 9 : -9;
        int right_side_shift = white ? 7 : -7;
        uint64_t start_rank = white ? RANK_2 : RANK_7;
        uint64_t promo_rank = white ? RANK_8 : RANK_1;
        Piece queen_promo = white ? Piece::Q : Piece::q;
        Piece rook_promo  = white ? Piece::R : Piece::r;
        Piece bishop_promo= white ? Piece::B : Piece::b;
        Piece knight_promo= white ? Piece::N : Piece::n;
        Piece promo_pieces[4] ={queen_promo,rook_promo,bishop_promo,knight_promo};


        uint64_t pawn = board.bitboards[pawn_piece];
        uint64_t single_push_target = white ? ((pawn << 8) & ~board.occupancy[2]) : ((pawn >> 8) & ~board.occupancy[2]);

        uint64_t rank_2_pawns = pawn & start_rank;
        uint64_t first_step = white ? ((rank_2_pawns << 8) & ~board.occupancy[2]) : ((rank_2_pawns >> 8) & ~board.occupancy[2]);

        uint64_t double_push_targets = white ? ((first_step << 8) & ~board.occupancy[2]) : ((first_step >> 8) & ~board.occupancy[2]);
        uint64_t left_side;
        uint64_t right_side;

        if (white){
            left_side  = (pawn & NOT_H_FILE) << 9;
            right_side = (pawn & NOT_A_FILE) << 7;
        }
        else{
            left_side  = (pawn & NOT_A_FILE) >> 9;
            right_side = (pawn & NOT_H_FILE) >> 7;
        }


        uint64_t captures = (left_side | right_side) & board.occupancy[enemy];
        captures &=legal_mask;

        uint64_t mask = single_push_target | double_push_targets;
        mask &= legal_mask;

        mask &= ~promo_rank;
        captures &= ~promo_rank;

        while (mask){
            uint64_t lsb = mask & (0ULL - mask);
            int to_square = lsb_index(lsb);
            int from_square = ((double_push_targets >> to_square) & 1) ? to_square - 2*push : to_square - push;
            
            uint64_t pin_mask = pins.pin_mask[from_square];
            if (!(pin_mask & (1ULL << to_square))){
                mask &= mask - 1;
                continue;
            }

            PackedMove move_data{};
            move_data.piece = pawn_piece;
            move_data.from_square = from_square;
            move_data.to_square = to_square;

            if ((double_push_targets >> to_square) & 1)
                move_data.is_double_push = true;

            Move move;
            move.value = PackedMove::pack(move_data);
            moves.push(move);
            mask &= mask - 1;
        }

        // NE diagonal captures (left_side, from = to - 9) — non-promotion
        {
            uint64_t left_caps = left_side & board.occupancy[enemy] & legal_mask & ~promo_rank;
            while (left_caps) {
                int to_square = pop_lsb(left_caps);
                int from_square = to_square - left_side_shift;
                uint64_t pin_mask = pins.pin_mask[from_square];
                if (!(pin_mask & (1ULL << to_square))) continue;
                PackedMove move_data;
                move_data.piece = pawn_piece;
                move_data.from_square = from_square;
                move_data.to_square = to_square;
                move_data.captured_piece =  board.piece_on[to_square];
                Move move;
                move.value = PackedMove::pack(move_data);
                moves.push(move);
            }
        }
        // NW diagonal captures (right_side, from = to - 7) — non-promotion
        {
            uint64_t right_caps = right_side & board.occupancy[enemy] & legal_mask & ~promo_rank;
            while (right_caps) {
                int to_square = pop_lsb(right_caps);
                int from_square = to_square - right_side_shift;
                uint64_t pin_mask = pins.pin_mask[from_square];
                if (!(pin_mask & (1ULL << to_square))) continue;
                PackedMove move_data;
                move_data.piece = pawn_piece;
                move_data.from_square = from_square;
                move_data.to_square = to_square;
                move_data.captured_piece =  board.piece_on[to_square];
                Move move;
                move.value = PackedMove::pack(move_data);
                moves.push(move);
            }
        }
        // Promotion pushes (non-capture, to rank 8)
        {
            uint64_t promo_push = single_push_target & promo_rank & legal_mask;
            while (promo_push) {
                int to_square = pop_lsb(promo_push);
                int from_square = to_square - push;
                uint64_t pin_mask = pins.pin_mask[from_square];
                if (!(pin_mask & (1ULL << to_square))) continue;
                for (int i = 0; i < 4; i++) {
                    PackedMove move_data{};
                    move_data.piece = pawn_piece;
                    move_data.from_square = from_square;
                    move_data.to_square = to_square;
                    move_data.promotion_piece = promo_pieces[i];
                    Move move;
                    move.value = PackedMove::pack(move_data);
                    moves.push(move);
                }
            }
        }
        // NE promotion captures (left_side to rank 8)
        {
            uint64_t left_promo = left_side & board.occupancy[enemy] & promo_rank & legal_mask;
            while (left_promo) {
                int to_square = pop_lsb(left_promo);
                int from_square = to_square - left_side_shift;
                uint64_t pin_mask = pins.pin_mask[from_square];
                if (!(pin_mask & (1ULL << to_square))) continue;
                for (int i = 0; i < 4; i++) {
                    PackedMove move_data{};
                    move_data.piece = pawn_piece;
                    move_data.from_square = from_square;
                    move_data.to_square = to_square;
                    move_data.promotion_piece = promo_pieces[i];
                    move_data.captured_piece =  board.piece_on[to_square];
                    Move move;
                    move.value = PackedMove::pack(move_data);
                    moves.push(move);
                }
            }
        }
        // NW promotion captures (right_side to rank 8)
        {
            uint64_t right_promo = right_side & board.occupancy[enemy] & promo_rank & legal_mask;
            while (right_promo) {
                int to_square = pop_lsb(right_promo);
                int from_square = to_square - right_side_shift;
                uint64_t pin_mask = pins.pin_mask[from_square];
                if (!(pin_mask & (1ULL << to_square))) continue;
                for (int i = 0; i < 4; i++) {
                    PackedMove move_data{};
                    move_data.piece = pawn_piece;
                    move_data.from_square = from_square;
                    move_data.to_square = to_square;
                    move_data.promotion_piece = promo_pieces[i];
                    move_data.captured_piece =  board.piece_on[to_square];
                    Move move;
                    move.value = PackedMove::pack(move_data);
                    moves.push(move);
                }
            }
        }

        // En passant captures
        if (board.en_passant != 255){
            uint64_t ep_bb = 1ULL << board.en_passant;
            // White pawns that attack the EP square are at ep-7 (not on h-file) or ep-9 (not on a-file)
            uint64_t ep_pawns;


            if (white){
                ep_pawns = board.bitboards[pawn_piece] & (((ep_bb & NOT_H_FILE) >> 7) | ((ep_bb & NOT_A_FILE) >> 9));
            }
            else{
                ep_pawns = board.bitboards[pawn_piece] & (((ep_bb & NOT_A_FILE) << 7) | ((ep_bb & NOT_H_FILE) << 9));
            }
            while (ep_pawns){
                int from_sq = pop_lsb(ep_pawns);

                uint64_t pin_mask = pins.pin_mask[from_sq];
                if (!(pin_mask & (1ULL << board.en_passant)))
                    continue;
                    
                PackedMove ep_move{};
                ep_move.piece          = pawn_piece;
                ep_move.from_square    = from_sq;
                ep_move.to_square      = board.en_passant;
                ep_move.is_en_passant  = true;
                ep_move.captured_piece = enemy_pawn;
                if (!ep_exposes_king(
                    board,
                    from_sq,
                    board.en_passant))
            {
                Move m;
                m.value = PackedMove::pack(ep_move);
                moves.push(m);
            }
            }
        }
    }
    void KnightMoves(Board &board, MoveList &moves, const PinInfo &pins, uint64_t legal_mask = ~0ULL){
        Piece knight = (board.side_to_move == WHITE) ? Piece::N : Piece::n;

        append_knight_moves(moves,board,knight,pins,legal_mask);
    }
    void BishopMoves(Board &board, MoveList &moves, const PinInfo &pins, uint64_t legal_mask = ~0ULL){
        Piece bishop = (board.side_to_move == WHITE) ? Piece::B : Piece::b;

        append_sliding_moves(moves,board,bishop,4,8,pins,legal_mask);
    }
    void RookMoves(Board &board, MoveList &moves, const PinInfo &pins,  uint64_t legal_mask = ~0ULL){
        Piece rook = (board.side_to_move == WHITE) ? Piece::R : Piece::r;

        append_sliding_moves(moves,board,rook,0,4,pins,legal_mask);
    }
    void QueenMoves(Board &board, MoveList &moves, const PinInfo &pins, uint64_t legal_mask = ~0ULL){
        Piece queen = (board.side_to_move == WHITE) ? Piece::Q : Piece::q;

        append_sliding_moves(moves,board,queen,0,8,pins,legal_mask);
    }
    void KingMoves(Board &board, MoveList &moves, uint64_t enemy_attack_mask){

        Piece king_piece = (board.side_to_move == WHITE) ? Piece::K : Piece::k;
        Color enemy = (board.side_to_move == WHITE) ? BLACK : WHITE;

        uint64_t king_bb = board.bitboards[king_piece];
        uint64_t lsb = king_bb & (0ULL - king_bb);
        int from_square = lsb_index(lsb);

        uint64_t east = (lsb & NOT_H_FILE) << 1;
        uint64_t west = (lsb & NOT_A_FILE) >> 1;
        uint64_t r = lsb | east | west;
        uint64_t raw_mask = (((r << 8 | r | r >> 8)) & ~lsb)& ~enemy_attack_mask;

        uint64_t captures_mask = raw_mask & board.occupancy[enemy];
        uint64_t move_mask = raw_mask & ~board.occupancy[2];

        PackedMove move_data;

        move_data.piece = king_piece;
        move_data.from_square = from_square;
        move_data.captured_piece = 0xFu;

        while (move_mask){
            int to_square = pop_lsb(move_mask);
            move_data.to_square = to_square;
            move_data.captured_piece = 0xFu;
            Move m;
            m.value = PackedMove::pack(move_data);
            moves.push(m);
        }
        while (captures_mask){
            int to_square = pop_lsb(captures_mask);
            move_data.to_square = to_square;
            move_data.captured_piece =  board.piece_on[to_square];
            Move m;
            m.value = PackedMove::pack(move_data);
            moves.push(m);
        }

        if (board.side_to_move == WHITE){
            if ((board.castling_rights & WHITE_KING_SIDE) &&
            !(board.occupancy[2] & ((1ULL << 5) | (1ULL << 6))) &&
            !(enemy_attack_mask & ((1ULL << 4) | (1ULL << 5) | (1ULL << 6)))){
            PackedMove castle{};
            castle.piece = king_piece;
            castle.from_square = 4; // e1
            castle.to_square   = 6; // g1
            castle.is_castle   = true;
            Move m;
            m.value = PackedMove::pack(castle);
            moves.push(m);
            }
        // White queen-side castling: b1(1), c1(2), d1(3) must be empty; c1,d1,e1 must not be attacked
            if ((board.castling_rights & WHITE_QUEEN_SIDE) &&
            !(board.occupancy[2] & ((1ULL << 1) | (1ULL << 2) | (1ULL << 3))) &&
            !(enemy_attack_mask & ((1ULL << 2) | (1ULL << 3) | (1ULL << 4)))){
            PackedMove castle{};
            castle.piece = king_piece;
            castle.from_square = 4; // e1
            castle.to_square   = 2; // c1
            castle.is_castle   = true;
            Move m;
            m.value = PackedMove::pack(castle);
            moves.push(m);
            }
        }
        else{
            if ((board.castling_rights & BLACK_KING_SIDE) &&
            !(board.occupancy[2] & ((1ULL << 61) | (1ULL << 62))) &&
            !(enemy_attack_mask & ((1ULL << 60) | (1ULL << 61) | (1ULL << 62)))){
            PackedMove castle{};
            castle.piece = king_piece;
            castle.from_square = 60; // e8
            castle.to_square   = 62; // g8
            castle.is_castle   = true;
            Move m;
            m.value = PackedMove::pack(castle);
            moves.push(m);
            }
        // Black queen-side castling: b8(57), c8(58), d8(59) must be empty; c8,d8,e8 must not be attacked
            if ((board.castling_rights & BLACK_QUEEN_SIDE) &&
            !(board.occupancy[2] & ((1ULL << 57) | (1ULL << 58) | (1ULL << 59))) &&
            !(enemy_attack_mask & ((1ULL << 58) | (1ULL << 59) | (1ULL << 60)))){
            PackedMove castle{};
            castle.piece = king_piece;
            castle.from_square = 60; // e8
            castle.to_square   = 58; // c8
            castle.is_castle   = true;
            Move m;
            m.value = PackedMove::pack(castle);
            moves.push(m);
            }
        }
    }
 
    uint64_t pawnatk(Board &board, uint64_t &checkers){

        // this function generate attack mask to check  danger squares for current (i.e side to move)king  and checkers of current king 

        Piece pawn;
        Piece enemy_king;
        if (board.side_to_move == WHITE){
            pawn = Piece::p;
            enemy_king = Piece::K;
        }
        else{
            pawn = Piece::P;
            enemy_king = Piece::k;
        }

        uint64_t mask = board.bitboards[pawn];
        uint64_t attack_mask = 0;
        while (mask){
            uint64_t lsb = mask & -mask;
            
            uint64_t left_side;
            uint64_t right_side;
            if (pawn == Piece::p){
                left_side = (lsb & NOT_A_FILE) >> 9;
                right_side = (lsb & NOT_H_FILE) >> 7;
            }
            else{
                left_side = (lsb & NOT_A_FILE) << 7;
                right_side = (lsb & NOT_H_FILE) << 9;
            }
            uint64_t attacks = left_side | right_side;
            if (attacks & board.bitboards[enemy_king]){
                checkers |= lsb;
            }
            attack_mask |= attacks;
            mask &= mask - 1;
        }
        return attack_mask;
    }
    
    uint64_t knightatk(Board &board, uint64_t &checkers){
        Piece knight;
        Piece enemy_king;

        if (board.side_to_move == WHITE){
            knight = Piece::n;
            enemy_king = Piece::K;
        }
        else{
            knight = Piece::N;
            enemy_king = Piece::k;
        }

        uint64_t mask = board.bitboards[knight];
        uint64_t attack_mask = 0;

        while (mask){
            uint64_t lsb = mask & -mask;

            uint64_t l1 = (lsb & NOT_H_FILE) << 1;
            uint64_t l2 = (lsb & NOT_GH_FILE) << 2;
            uint64_t r1 = (lsb & NOT_A_FILE) >> 1;
            uint64_t r2 = (lsb & NOT_AB_FILE) >> 2;

            uint64_t h1 = l1 | r1;
            uint64_t h2 = l2 | r2;

            uint64_t attacks =(h1 << 16) | (h1 >> 16) | (h2 << 8) | (h2 >> 8);

            if (attacks & board.bitboards[enemy_king]){
                checkers |= lsb;
            }
            attack_mask |= attacks;
            mask &= mask - 1;
        }

        return attack_mask;
    }

    uint64_t kingatk(Board &board){
        Piece king;
        if (board.side_to_move == WHITE)
            king = Piece::k;
        else
            king = Piece::K;

        uint64_t king_bb = board.bitboards[king];

        uint64_t east = (king_bb & NOT_H_FILE) << 1;
        uint64_t west = (king_bb & NOT_A_FILE) >> 1;
        uint64_t r = king_bb | east | west;

        uint64_t attack_mask =((r << 8) | r | (r >> 8)) & ~king_bb;

        return attack_mask;
    }

    uint64_t sliding_atks(Board &board, Piece piece, int dir_start, int dir_end, uint64_t &checkers) {
        Color enemy  = (piece_color(piece) == WHITE) ? BLACK : WHITE;
        Piece king   = (enemy == WHITE) ? Piece::K : Piece::k;
        uint64_t occ = board.occupancy[2] ^ board.bitboards[king]; // remove king
        uint64_t attack_mask = 0;
        uint64_t mask = board.bitboards[piece];
    
        while (mask) {
            int from_sq = lsb_index(mask);
    
            // ── magic lookup with king removed from occ ───────────────────
            uint64_t attacks;
            if (dir_start == 0 && dir_end == 4)
                attacks = rook_attacks(from_sq, occ);
            else if (dir_start == 4 && dir_end == 8)
                attacks = bishop_attacks(from_sq, occ);
            else
                attacks = queen_attacks(from_sq, occ);
    
            if (attacks & board.bitboards[king])
                checkers |= (1ULL << from_sq);
    
            attack_mask |= attacks;
            mask &= mask - 1;
        }
    
        return attack_mask;
    }
    
    void generate_moves(Board &board, MoveList &moves){
        PinInfo pins = find_pins(board);
        uint64_t attack_mask = 0;
        uint64_t checkers = 0;

        Piece bishop = board.side_to_move == WHITE ? Piece::b : Piece::B;
        Piece rook = board.side_to_move == WHITE ? Piece::r : Piece::R;
        Piece queen = board.side_to_move == WHITE ? Piece::q : Piece::Q;

        attack_mask |= pawnatk(board, checkers);
        attack_mask |= knightatk(board, checkers);
        attack_mask |= sliding_atks(board, bishop, 4, 8, checkers);
        attack_mask |= sliding_atks(board, rook, 0, 4, checkers);
        attack_mask |= sliding_atks(board, queen, 0, 8, checkers);
        attack_mask |= kingatk(board);

        int check_count = __builtin_popcountll(checkers);

        if (check_count == 0){
            uint64_t legal_mask=~0ULL;
            KingMoves(board, moves, attack_mask);
            PawnMoves(board, moves, pins, legal_mask);
            KnightMoves(board, moves, pins, legal_mask);
            BishopMoves(board, moves, pins, legal_mask);
            RookMoves(board, moves, pins, legal_mask);
            QueenMoves(board, moves, pins, legal_mask);
        
        }
        else if(check_count ==1){
            Piece king_piece = board.side_to_move == WHITE ? Piece::K : Piece::k;
            Piece enemy_pawn = board.side_to_move == WHITE ? Piece::p : Piece::P;        
            Piece enemy_knight = board.side_to_move == WHITE ? Piece::n : Piece::N;

            int checker_square =lsb_index(checkers);
            Piece checker_piece =  board.piece_on[checker_square];
            uint64_t legal_mask;
            if (checker_piece == enemy_pawn || checker_piece == enemy_knight){
                legal_mask = checkers; // non-slider: only capture is legal
            }
            else{
                int king_sq = lsb_index(board.bitboards[king_piece]);
                legal_mask = checkers; // default: capture the checker
                for (int dir = 0; dir < 8; dir++){
                    uint64_t ray = get_ray(dir, king_sq);
                    if (ray & (1ULL << checker_square)){
                        // XOR trims the ray to king→checker only (excludes squares past checker)
                        legal_mask = (ray ^ get_ray(dir, checker_square)) | (1ULL << checker_square);
                        break;
                    }
                }
            }
            KingMoves(board, moves, attack_mask);
            PawnMoves(board, moves, pins, legal_mask);
            KnightMoves(board, moves, pins, legal_mask);
            BishopMoves(board, moves, pins, legal_mask);
            RookMoves(board, moves, pins, legal_mask);
            QueenMoves(board, moves, pins, legal_mask);
        
        }

        else{
            KingMoves(board, moves, attack_mask);
        }
    }
};

inline bool Board::is_in_check() {
    uint64_t checkers = 0;
    MoveGenerator mg;
    Piece bishop = side_to_move == WHITE ? Piece::b : Piece::B;
    Piece rook = side_to_move == WHITE ? Piece::r : Piece::R;
    Piece queen = side_to_move == WHITE ? Piece::q : Piece::Q;

    mg.pawnatk(*this, checkers);
    mg.knightatk(*this, checkers);
    mg.sliding_atks(*this, bishop, 4, 8, checkers);
    mg.sliding_atks(*this, rook, 0, 4, checkers);
    mg.sliding_atks(*this, queen, 0, 8, checkers);
    return checkers != 0;
}

inline bool Board::is_insufficient_material() const {
    if (bitboards[Piece::P] || bitboards[Piece::p] ||
        bitboards[Piece::R] || bitboards[Piece::r] ||
        bitboards[Piece::Q] || bitboards[Piece::q]) {
        return false;
    }

    int white_knights = __builtin_popcountll(bitboards[Piece::N]);
    int black_knights = __builtin_popcountll(bitboards[Piece::n]);
    int white_bishops = __builtin_popcountll(bitboards[Piece::B]);
    int black_bishops = __builtin_popcountll(bitboards[Piece::b]);

    int total_pieces = 2 + white_knights + black_knights + white_bishops + black_bishops;

    if (total_pieces == 2) return true;
    if (total_pieces == 3) {
        if (white_bishops == 1 || black_bishops == 1 || white_knights == 1 || black_knights == 1) {
            return true;
        }
    }
    if (total_pieces == 4) {
        if (white_bishops == 1 && black_bishops == 1) {
            int white_bishop_sq = lsb_index(bitboards[Piece::B]);
            int black_bishop_sq = lsb_index(bitboards[Piece::b]);
            bool white_light = ((white_bishop_sq / 8) + (white_bishop_sq % 8)) % 2 != 0;
            bool black_light = ((black_bishop_sq / 8) + (black_bishop_sq % 8)) % 2 != 0;
            if (white_light == black_light) return true;
        }
    }
    return false;
}

inline GameResult Board::get_game_result() {
    if (halfmove_clock >= 150) {
        return GAME_SEVENTY_FIVE_MOVE_DRAW;
    }
    if (halfmove_clock >= 100) {
        return GAME_FIFTY_MOVE_DRAW;
    }
    if (is_insufficient_material()) {
        return GAME_INSUFFICIENT_MATERIAL;
    }

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(*this, moves);

    if (moves.size() == 0) {
        if (is_in_check()) {
            return GAME_CHECKMATE;
        } else {
            return GAME_STALEMATE;
        }
    }

    return GAME_ONGOING;
}

string move_to_uci(Move m) {
    int from = Move::from(m);
    int to = Move::to(m);
    string uci = "";
    uci += (char)('a' + (from % 8));
    uci += (char)('1' + (from / 8));
    uci += (char)('a' + (to % 8));
    uci += (char)('1' + (to / 8));
    if (Move::is_promotion(m)) {
        Piece promo = Move::promotion_piece(m);
        if (promo == Piece::N || promo == Piece::n) uci += 'n';
        else if (promo == Piece::B || promo == Piece::b) uci += 'b';
        else if (promo == Piece::R || promo == Piece::r) uci += 'r';
        else if (promo == Piece::Q || promo == Piece::q) uci += 'q';
    }
    return uci;
}

// ══════════════════════════════════════════════════════════════
// Engine — Static Evaluation + Minimax Search
// ══════════════════════════════════════════════════════════════

class Engine {
public:
    // ── Piece base values (centipawns) ────────────────────────
    static constexpr int PIECE_VALUE[12] = {
        100,  // P
        320,  // N
        330,  // B
        500,  // R
        900,  // Q
        20000,// K
        100,  // p
        320,  // n
        330,  // b
        500,  // r
        900,  // q
        20000 // k
    };

    // ── Piece-square tables (White's perspective, index 0=a1) ─
    // Mirror for Black: mirror_sq = (7 - rank)*8 + file
    static constexpr int PAWN_PST[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
         5, 10, 10,-20,-20, 10, 10,  5,
         5, -5,-10,  0,  0,-10, -5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5,  5, 10, 25, 25, 10,  5,  5,
        10, 10, 20, 30, 30, 20, 10, 10,
        50, 50, 50, 50, 50, 50, 50, 50,
         0,  0,  0,  0,  0,  0,  0,  0,
    };
    static constexpr int KNIGHT_PST[64] = {
       -50,-40,-30,-30,-30,-30,-40,-50,
       -40,-20,  0,  5,  5,  0,-20,-40,
       -30,  5, 10, 15, 15, 10,  5,-30,
       -30,  0, 15, 20, 20, 15,  0,-30,
       -30,  5, 15, 20, 20, 15,  5,-30,
       -30,  0, 10, 15, 15, 10,  0,-30,
       -40,-20,  0,  0,  0,  0,-20,-40,
       -50,-40,-30,-30,-30,-30,-40,-50,
    };
    static constexpr int BISHOP_PST[64] = {
       -20,-10,-10,-10,-10,-10,-10,-20,
       -10,  5,  0,  0,  0,  0,  5,-10,
       -10, 10, 10, 10, 10, 10, 10,-10,
       -10,  0, 10, 10, 10, 10,  0,-10,
       -10,  5,  5, 10, 10,  5,  5,-10,
       -10,  0,  5, 10, 10,  5,  0,-10,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -20,-10,-10,-10,-10,-10,-10,-20,
    };
    static constexpr int ROOK_PST[64] = {
         0,  0,  0,  5,  5,  0,  0,  0,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
         5, 10, 10, 10, 10, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0,
    };
    static constexpr int QUEEN_PST[64] = {
       -20,-10,-10, -5, -5,-10,-10,-20,
       -10,  0,  5,  0,  0,  0,  0,-10,
       -10,  5,  5,  5,  5,  5,  0,-10,
         0,  0,  5,  5,  5,  5,  0, -5,
        -5,  0,  5,  5,  5,  5,  0, -5,
       -10,  0,  5,  5,  5,  5,  0,-10,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -20,-10,-10, -5, -5,-10,-10,-20,
    };
    // King: stay safe in the middlegame
    static constexpr int KING_PST[64] = {
        20, 30, 10,  0,  0, 10, 30, 20,
        20, 20,  0,  0,  0,  0, 20, 20,
       -10,-20,-20,-20,-20,-20,-20,-10,
       -20,-30,-30,-40,-40,-30,-30,-20,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
    };

    // Mirror square for Black (flip rank)
    static int mirror(int sq) {
        int rank = sq / 8, file = sq % 8;
        return (7 - rank) * 8 + file;
    }

    // ── Static evaluation ─────────────────────────────────────
    // Returns score in centipawns from White's perspective.
    // Positive = White is better, Negative = Black is better.
    int evaluate(Board& board) {
        int score = 0;
        for (int sq = 0; sq < 64; sq++) {
            Piece p = board.piece_on[sq];
            if (p == (Piece)0xF) continue; // empty

            int base = PIECE_VALUE[p];
            int pst  = 0;
            bool is_white = (p < 6); // P,N,B,R,Q,K = 0..5

            int table_sq = is_white ? sq : mirror(sq);

            switch (p) {
                case Piece::P: case Piece::p: pst = PAWN_PST[table_sq];   break;
                case Piece::N: case Piece::n: pst = KNIGHT_PST[table_sq]; break;
                case Piece::B: case Piece::b: pst = BISHOP_PST[table_sq]; break;
                case Piece::R: case Piece::r: pst = ROOK_PST[table_sq];   break;
                case Piece::Q: case Piece::q: pst = QUEEN_PST[table_sq];  break;
                case Piece::K: case Piece::k: pst = KING_PST[table_sq];   break;
                default: break;
            }

            if (is_white) score += base + pst;
            else          score -= base + pst;
        }
        return score;
    }

    // ── Minimax with Alpha-Beta pruning ───────────────────────
    // Returns the best score for the side to move.
    // depth   : plies remaining
    // alpha   : best score White can guarantee
    // beta    : best score Black can guarantee
    // maximizing: true when it's White's turn
    int minimax(Board& board, int depth, int alpha, int beta, bool maximizing) {
        // Terminal: depth 0 → static eval
        if (depth == 0) return evaluate(board);

        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        // Terminal: no moves → checkmate or stalemate
        if (moves.size() == 0) {
            if (board.is_in_check()) {
                // Checkmate: prefer faster mates (subtract depth so shallower = worse)
                return maximizing ? (-20000 - depth) : (20000 + depth);
            }
            return 0; // stalemate
        }

        if (maximizing) {
            int best = -100000;
            for (int i = 0; i < moves.size(); i++) {
                MoveList dummy;
                board.make_move(moves.move_list[i], dummy);
                int score = minimax(board, depth - 1, alpha, beta, false);
                board.unmake_move();
                if (score > best) best = score;
                if (best > alpha) alpha = best;
                if (beta <= alpha) break; // Beta cut-off
            }
            return best;
        } else {
            int best = 100000;
            for (int i = 0; i < moves.size(); i++) {
                MoveList dummy;
                board.make_move(moves.move_list[i], dummy);
                int score = minimax(board, depth - 1, alpha, beta, true);
                board.unmake_move();
                if (score < best) best = score;
                if (best < beta) beta = best;
                if (beta <= alpha) break; // Alpha cut-off
            }
            return best;
        }
    }

    // ── Root search: find the best move ──────────────────────
    // Returns the best Move and its score.
    Move best_move(Board& board, int depth) {
        MoveList moves;
        MoveGenerator mg;
        mg.generate_moves(board, moves);

        bool maximizing = (board.side_to_move == WHITE);
        int  best_score = maximizing ? -100000 : 100000;
        Move best;
        best.value = 0;

        for (int i = 0; i < moves.size(); i++) {
            MoveList dummy;
            board.make_move(moves.move_list[i], dummy);
            int score = minimax(board, depth - 1, -100000, 100000, !maximizing);
            board.unmake_move();

            if (maximizing ? (score > best_score) : (score < best_score)) {
                best_score = score;
                best       = moves.move_list[i];
            }
        }
        return best;
    }
};


