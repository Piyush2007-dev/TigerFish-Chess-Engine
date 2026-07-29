#include <cstdint>
#include <array>

using namespace std;

// ray_table[direction][square] — precomputed ray bitboards for all 8 dirs from every square; built once by init_rays()
array<array<uint64_t,64>,8> ray_table;

// init_rays — populates the ray_table for all squares and directions
inline void init_rays(){
    static bool initialized = false;
    if(initialized) return;
    initialized = true;
    
    int DR[8]={1,-1,0,0,1,-1,1,-1};
    int DF[8]={0,0,1,-1,1,1,-1,-1};
    
    for(int sq=0;sq<64;++sq){
        int rank=sq/8;
        int file=sq%8;
        
        for(int d=0;d<8;++d){
            uint64_t bb=0;
            int r=rank+DR[d],f=file+DF[d];
            
            while(r>=0&&r<8&&f>=0&&f<8){
                bb|=1ULL<<(r*8+f);
                r+=DR[d];
                f+=DF[d];
            }
            
            ray_table[d][sq]=bb;
        }
    }
}

// ROOK_MAGICS / BISHOP_MAGICS — one hardcoded magic number per square; acts as a perfect hash: (blocker*MAGIC)>>SHIFT gives a unique index
const uint64_t ROOK_MAGICS[64] = {
    0x8a80104000800020ULL,0x140002000100040ULL,0x2801880a0017001ULL,
    0x100081001000420ULL,0x200020010080420ULL,0x3001c0002010008ULL,
    0x8480008002000100ULL,0x2080088004402900ULL,0x800098204000ULL,
    0x2024401000200040ULL,0x100802000801000ULL,0x120800800801000ULL,
    0x208808088000400ULL,0x2802200800400ULL,0x2200800100020080ULL,
    0x801000060821100ULL,0x80044006422000ULL,0x100808020004000ULL,
    0x12108a0010204200ULL,0x140848010000802ULL,0x481828014002800ULL,
    0x8094004002004100ULL,0x4010040010010802ULL,0x20008806104ULL,
    0x100400080208000ULL,0x2040002120081000ULL,0x21200680100081ULL,
    0x20100080080080ULL,0x2000a00200410ULL,0x20080800400ULL,
    0x80088400100102ULL,0x80004600042881ULL,0x4040008040800020ULL,
    0x440003000200801ULL,0x4200011004500ULL,0x188020010100100ULL,
    0x14800401802800ULL,0x2080040080800200ULL,0x124080204001001ULL,
    0x200046502000484ULL,0x480400080088020ULL,0x1000422010034000ULL,
    0x30200100110040ULL,0x100021010009ULL,0x2002080100110004ULL,
    0x202008004008002ULL,0x20020004010100ULL,0x2048440040820001ULL,
    0x101002200408200ULL,0x40802000401080ULL,0x4008142004410100ULL,
    0x2060820c0120200ULL,0x1001004080100ULL,0x20c020080040080ULL,
    0x2935610830022400ULL,0x44440041009200ULL,0x280001040802101ULL,
    0x2100190040002085ULL,0x80c0084100102001ULL,0x4024081001000421ULL,
    0x20030a0244872ULL,0x12001008414402ULL,0x2006104900a0804ULL,
    0x1004081002402ULL
};

const uint64_t BISHOP_MAGICS[64] = {
    0x40040844404084ULL,0x2004208a004208ULL,0x10190041080202ULL,
    0x108060845042010ULL,0x581104180800210ULL,0x2112080446200010ULL,
    0x1080820820060210ULL,0x3c0808410220200ULL,0x4050404440404ULL,
    0x21001420088ULL,0x24d0080801082102ULL,0x1020a0a020400ULL,
    0x40308200402ULL,0x4011002100800ULL,0x401484104104005ULL,
    0x801010402020200ULL,0x400210c3880100ULL,0x404022024108200ULL,
    0x810018200204102ULL,0x4002801a02003ULL,0x85040820080400ULL,
    0x810102c808880400ULL,0xe900410884800ULL,0x8002020480840102ULL,
    0x220200865090201ULL,0x2010100a02021202ULL,0x152048408022401ULL,
    0x20080002081110ULL,0x4001001021004000ULL,0x800040400a011002ULL,
    0xe4004081011002ULL,0x1c004001012080ULL,0x8004200962a00220ULL,
    0x8422100208500202ULL,0x2000402200300c08ULL,0x8646020080080080ULL,
    0x80020a0200100808ULL,0x2010004880111000ULL,0x623000a080011400ULL,
    0x42008c0340209202ULL,0x209188240001000ULL,0x400408a884001800ULL,
    0x110400a6080400ULL,0x1840060a44020800ULL,0x90080104000041ULL,
    0x201011000808101ULL,0x1a2208080504f080ULL,0x8012020600211212ULL,
    0x500861011240000ULL,0x180806108200800ULL,0x4000020e01040044ULL,
    0x300000261044000aULL,0x802241102020002ULL,0x20906061210001ULL,
    0x5a84841004010310ULL,0x4010801011c04ULL,0xa010109502200ULL,
    0x4a02012000ULL,0x500201010098b028ULL,0x8040002811040900ULL,
    0x28000010020204ULL,0x6000020202d0240ULL,0x8918844842082200ULL,
    0x4010011029020020ULL
};

// ROOK_SHIFT / BISHOP_SHIFT[sq] — right-shift = 64 - popcount(mask); compresses magic-multiply result into compact index range
int ROOK_SHIFT[64];
int BISHOP_SHIFT[64];

// ROOK_MASK / BISHOP_MASK[sq] — bitboard of squares that matter for blocking on that square; edges excluded to save index bits
uint64_t ROOK_MASK[64];
uint64_t BISHOP_MASK[64];

// ROOK_ATTACKS[sq][idx] — precomputed attack bitboard for rook on sq given blocker pattern hashing to idx; 4096 = 2^12 worst case
uint64_t ROOK_ATTACKS[64][4096];

// BISHOP_ATTACKS[sq][idx] — same as ROOK_ATTACKS but for bishop diagonals; 512 = 2^9 worst case
uint64_t BISHOP_ATTACKS[64][512];

// compute_rook_attacks — slides outward N/S/E/W from sq, stops AT first blocker (inclusive); slow, only called during init_magics()
static uint64_t compute_rook_attacks(int sq, uint64_t blockers){
    uint64_t attacks=0;
    int r=sq/8,f=sq%8;
    for(int i=r+1;i<8;i++){ attacks|=(1ULL<<(i*8+f)); if(blockers&(1ULL<<(i*8+f)))break; } // north
    for(int i=r-1;i>=0;i--){ attacks|=(1ULL<<(i*8+f)); if(blockers&(1ULL<<(i*8+f)))break; } // south
    for(int i=f+1;i<8;i++){ attacks|=(1ULL<<(r*8+i)); if(blockers&(1ULL<<(r*8+i)))break; } // east
    for(int i=f-1;i>=0;i--){ attacks|=(1ULL<<(r*8+i)); if(blockers&(1ULL<<(r*8+i)))break; } // west
    return attacks;
}

// compute_bishop_attacks — same as compute_rook_attacks but slides diagonally NE/NW/SE/SW; only called during init_magics()
static uint64_t compute_bishop_attacks(int sq, uint64_t blockers){
    uint64_t attacks=0;
    int r=sq/8,f=sq%8;
    for(int i=r+1,j=f+1;i<8&&j<8;i++,j++){ attacks|=(1ULL<<(i*8+j)); if(blockers&(1ULL<<(i*8+j)))break; } // NE
    for(int i=r+1,j=f-1;i<8&&j>=0;i++,j--){ attacks|=(1ULL<<(i*8+j)); if(blockers&(1ULL<<(i*8+j)))break; } // NW
    for(int i=r-1,j=f+1;i>=0&&j<8;i--,j++){ attacks|=(1ULL<<(i*8+j)); if(blockers&(1ULL<<(i*8+j)))break; } // SE
    for(int i=r-1,j=f-1;i>=0&&j>=0;i--,j--){ attacks|=(1ULL<<(i*8+j)); if(blockers&(1ULL<<(i*8+j)))break; } // SW
    return attacks;
}

// next_subset — carry-ripple trick: (sub-1)&mask steps downward through all 2^n subsets of mask bits; returns 0 after last subset
static uint64_t next_subset(uint64_t sub, uint64_t mask){
    return (sub-1)&mask;
}

// init_magics — fills ROOK_ATTACKS and BISHOP_ATTACKS; called once at startup; for each sq enumerates all blocker subsets and stores precomputed attacks at their magic hash index
inline void init_magics(){
    static bool initialized = false;
    if(initialized) return;
    initialized = true;

    for(int sq=0;sq<64;sq++){
        int r=sq/8,f=sq%8;

        uint64_t rmask=0; // blocker mask for rook on sq — rank+file squares, edges excluded
        for(int i=r+1;i<7;i++)rmask|=(1ULL<<(i*8+f)); // north
        for(int i=r-1;i>0;i--)rmask|=(1ULL<<(i*8+f)); // south
        for(int i=f+1;i<7;i++)rmask|=(1ULL<<(r*8+i)); // east
        for(int i=f-1;i>0;i--)rmask|=(1ULL<<(r*8+i)); // west
        ROOK_MASK[sq]=rmask;
        ROOK_SHIFT[sq]=64-__builtin_popcountll(rmask); // shift so (occ*MAGIC)>>SHIFT fits in [0, 2^popcount-1]

        uint64_t bmask=0; // blocker mask for bishop on sq — diagonal squares, edges excluded
        for(int i=r+1,j=f+1;i<7&&j<7;i++,j++)bmask|=(1ULL<<(i*8+j)); // NE
        for(int i=r+1,j=f-1;i<7&&j>0;i++,j--)bmask|=(1ULL<<(i*8+j)); // NW
        for(int i=r-1,j=f+1;i>0&&j<7;i--,j++)bmask|=(1ULL<<(i*8+j)); // SE
        for(int i=r-1,j=f-1;i>0&&j>0;i--,j--)bmask|=(1ULL<<(i*8+j)); // SW
        BISHOP_MASK[sq]=bmask;
        BISHOP_SHIFT[sq]=64-__builtin_popcountll(bmask);

        uint64_t rsub=0; // current rook blocker subset being processed
        do{
            int idx=(int)((rsub*ROOK_MAGICS[sq])>>ROOK_SHIFT[sq]); // magic hash → compact table index
            ROOK_ATTACKS[sq][idx]=compute_rook_attacks(sq,rsub);
            rsub=next_subset(rsub,rmask); // advance to next blocker combination
        }while(rsub!=0);

        uint64_t bsub=0; // current bishop blocker subset being processed
        do{
            int idx=(int)((bsub*BISHOP_MAGICS[sq])>>BISHOP_SHIFT[sq]);
            BISHOP_ATTACKS[sq][idx]=compute_bishop_attacks(sq,bsub);
            bsub=next_subset(bsub,bmask);
        }while(bsub!=0);
    }
}

