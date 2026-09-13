// size_report.cpp — Reports exact sizes of Board and all lookup tables
#include "../../engine/rules.cpp"
#include <iostream>

int main() {
    init_rays();
    init_magics();

    Board b;

    // ── Board struct fields ──
    size_t bb   = sizeof(b.bitboards);          // array<uint64_t,12>
    size_t occ  = sizeof(b.occupancy);          // array<uint64_t,3>
    size_t pon  = sizeof(b.piece_on);           // Piece[64]
    size_t stm  = sizeof(b.side_to_move);
    size_t ec   = sizeof(b.enemy_color);
    size_t cr   = sizeof(b.castling_rights);
    size_t ep   = sizeof(b.en_passant);
    size_t hmc  = sizeof(b.halfmove_clock);
    size_t fmn  = sizeof(b.fullmove_number);
    size_t sf   = sizeof(b.START_FEN);          // std::string (SSO heap)
    size_t mh   = sizeof(b.move_history);       // vector metadata
    size_t eph  = sizeof(b.ep_history);
    size_t hmh  = sizeof(b.halfmove_history);

    size_t board_struct = sizeof(Board);

    // Heap allocations (reserved 4096 entries each)
    size_t mh_heap  = 4096 * sizeof(uint32_t);
    size_t eph_heap = 4096 * sizeof(uint8_t);
    size_t hmh_heap = 4096 * sizeof(uint8_t);

    // ── Lookup tables (globals in magic_lut / board.cpp) ──
    size_t ray_t   = sizeof(ray_table);                    // [8][64] uint64_t
    size_t rmask   = sizeof(ROOK_MASK);                    // [64] uint64_t
    size_t bmask   = sizeof(BISHOP_MASK);                  // [64] uint64_t
    size_t rshift  = sizeof(ROOK_SHIFT);                   // [64] int
    size_t bshift  = sizeof(BISHOP_SHIFT);                 // [64] int
    size_t ratk    = sizeof(ROOK_ATTACKS);                 // [64][4096] uint64_t
    size_t batk    = sizeof(BISHOP_ATTACKS);               // [64][512] uint64_t

    auto kb = [](size_t b){ return b / 1024.0; };
    auto mb = [](size_t b){ return b / (1024.0*1024.0); };

    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║         TigerFish Memory Layout Report             ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";

    std::cout << "── BOARD STRUCT (Stack, 1 object per game) ────────────\n";
    std::cout << "  bitboards[12]      : " << bb   << " bytes  (" << kb(bb)   << " KB)\n";
    std::cout << "  occupancy[3]       : " << occ  << " bytes\n";
    std::cout << "  piece_on[64]       : " << pon  << " bytes\n";
    std::cout << "  side_to_move       : " << stm  << " bytes\n";
    std::cout << "  enemy_color        : " << ec   << " bytes\n";
    std::cout << "  castling_rights    : " << cr   << " bytes\n";
    std::cout << "  en_passant         : " << ep   << " bytes\n";
    std::cout << "  halfmove_clock     : " << hmc  << " bytes\n";
    std::cout << "  fullmove_number    : " << fmn  << " bytes\n";
    std::cout << "  START_FEN (string) : " << sf   << " bytes  (SSO metadata)\n";
    std::cout << "  move_history       : " << mh   << " bytes  (vector metadata)\n";
    std::cout << "  ep_history         : " << eph  << " bytes\n";
    std::cout << "  halfmove_history   : " << hmh  << " bytes\n";
    std::cout << "  ─────────────────────────────────────────────────────\n";
    std::cout << "  sizeof(Board)      : " << board_struct << " bytes  (" << kb(board_struct) << " KB)\n";
    std::cout << "\n── BOARD HEAP ALLOCATIONS (vector data, pre-reserved) ─\n";
    std::cout << "  move_history heap  : " << mh_heap  << " bytes  (" << kb(mh_heap)  << " KB)  [4096 × uint32_t]\n";
    std::cout << "  ep_history heap    : " << eph_heap << " bytes  (" << kb(eph_heap) << " KB)  [4096 × uint8_t]\n";
    std::cout << "  halfmove heap      : " << hmh_heap << " bytes  (" << kb(hmh_heap) << " KB)  [4096 × uint8_t]\n";
    size_t total_board_heap = mh_heap + eph_heap + hmh_heap;
    std::cout << "  Total heap         : " << total_board_heap << " bytes  (" << kb(total_board_heap) << " KB)\n";
    std::cout << "  TOTAL Board+Heap   : " << board_struct + total_board_heap << " bytes  (" 
              << kb(board_struct + total_board_heap) << " KB)\n";

    std::cout << "\n── LOOKUP TABLES (BSS/Data segment, process-global) ───\n";
    std::cout << "  ray_table[8][64]         : " << ray_t  << " bytes  (" << kb(ray_t)  << " KB)\n";
    std::cout << "  ROOK_MASK[64]            : " << rmask  << " bytes  (" << kb(rmask)  << " KB)\n";
    std::cout << "  BISHOP_MASK[64]          : " << bmask  << " bytes  (" << kb(bmask)  << " KB)\n";
    std::cout << "  ROOK_SHIFT[64]           : " << rshift << " bytes\n";
    std::cout << "  BISHOP_SHIFT[64]         : " << bshift << " bytes\n";
    std::cout << "  ROOK_ATTACKS[64][4096]   : " << ratk   << " bytes  (" << mb(ratk)   << " MB)\n";
    std::cout << "  BISHOP_ATTACKS[64][512]  : " << batk   << " bytes  (" << kb(batk)   << " KB)\n";
    size_t total_lut = ray_t + rmask + bmask + rshift + bshift + ratk + batk;
    std::cout << "  ─────────────────────────────────────────────────────\n";
    std::cout << "  TOTAL LUTs         : " << total_lut << " bytes  (" << mb(total_lut) << " MB)\n";

    std::cout << "\n── CACHE RESIDENCE ANALYSIS ───────────────────────────\n";
    std::cout << "  L1 cache (typical) : ~32 KB per core\n";
    std::cout << "  L2 cache (typical) : ~256 KB per core\n";
    std::cout << "  L3 cache (typical) : ~8–32 MB shared\n\n";

    std::cout << "  Board struct only  : " << kb(board_struct) << " KB  → ";
    if (board_struct <= 32*1024) std::cout << "FITS IN L1\n";
    else std::cout << "TOO LARGE FOR L1\n";

    std::cout << "  Board + Heap       : " << kb(board_struct + total_board_heap) << " KB  → ";
    if (board_struct + total_board_heap <= 32*1024) std::cout << "FITS IN L1\n";
    else if (board_struct + total_board_heap <= 256*1024) std::cout << "FITS IN L2\n";
    else std::cout << "NEEDS L3\n";

    std::cout << "  BISHOP_ATTACKS     : " << kb(batk) << " KB  → ";
    if (batk <= 32*1024) std::cout << "FITS IN L1\n";
    else if (batk <= 256*1024) std::cout << "FITS IN L2\n";
    else std::cout << "NEEDS L3\n";

    std::cout << "  ROOK_ATTACKS       : " << mb(ratk) << " MB   → ";
    if (ratk <= 32*1024) std::cout << "FITS IN L1\n";
    else if (ratk <= 256*1024) std::cout << "FITS IN L2\n";
    else std::cout << "NEEDS L3 (2MB)\n";

    std::cout << "══════════════════════════════════════════════════════════\n";
    return 0;
}
