// ══════════════════════════════════════════════════════════════
// Piece-Square Tables (PST) — from Chess Programming Wiki
// Public domain. Used by Crafty, Fruit, and many others.
// 
// Layout: index 0 = a1 (bottom-left for White)
//         index 63 = h8 (top-right for White)
// For Black: mirror vertically → use pst[56 - sq + (sq % 8) * 2] 
//            or simply flip: mirror_sq = (7 - rank)*8 + file
//
// Values are BONUSES added to the base piece value.
// Piece values (centipawns):
//   Pawn=100, Knight=320, Bishop=330, Rook=500, Queen=900, King=20000
// ══════════════════════════════════════════════════════════════

// ── Helper: mirror a square index for Black ──────────────────
// White uses table[sq], Black uses table[mirror(sq)]
inline int mirror(int sq) {
    return (7 - sq / 8) * 8 + (sq % 8);
}

// ── Pawn ─────────────────────────────────────────────────────
// Encourage central advance, penalise wing pawns early
static const int pawn_pst[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,   // rank 1 (impossible for pawn)
     5, 10, 10,-20,-20, 10, 10,  5,   // rank 2
     5, -5,-10,  0,  0,-10, -5,  5,   // rank 3
     0,  0,  0, 20, 20,  0,  0,  0,   // rank 4  ← e4/d4 bonus
     5,  5, 10, 25, 25, 10,  5,  5,   // rank 5
    10, 10, 20, 30, 30, 20, 10, 10,   // rank 6
    50, 50, 50, 50, 50, 50, 50, 50,   // rank 7  ← near promotion
     0,  0,  0,  0,  0,  0,  0,  0,   // rank 8 (promoted, N/A)
};

// ── Knight ───────────────────────────────────────────────────
// Penalise rim & corner; reward centre and outposts
static const int knight_pst[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,   // rank 1
   -40,-20,  0,  5,  5,  0,-20,-40,   // rank 2
   -30,  5, 10, 15, 15, 10,  5,-30,   // rank 3
   -30,  0, 15, 20, 20, 15,  0,-30,   // rank 4
   -30,  5, 15, 20, 20, 15,  5,-30,   // rank 5
   -30,  0, 10, 15, 15, 10,  0,-30,   // rank 6
   -40,-20,  0,  0,  0,  0,-20,-40,   // rank 7
   -50,-40,-30,-30,-30,-30,-40,-50,   // rank 8
};

// ── Bishop ───────────────────────────────────────────────────
// Reward long diagonals; penalise corners and back rank
static const int bishop_pst[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,   // rank 1
   -10,  5,  0,  0,  0,  0,  5,-10,   // rank 2
   -10, 10, 10, 10, 10, 10, 10,-10,   // rank 3
   -10,  0, 10, 10, 10, 10,  0,-10,   // rank 4
   -10,  5,  5, 10, 10,  5,  5,-10,   // rank 5
   -10,  0,  5, 10, 10,  5,  0,-10,   // rank 6
   -10,  0,  0,  0,  0,  0,  0,-10,   // rank 7
   -20,-10,-10,-10,-10,-10,-10,-20,   // rank 8
};

// ── Rook ─────────────────────────────────────────────────────
// Reward 7th rank and open files; keep off a/h files early
static const int rook_pst[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,   // rank 1
    -5,  0,  0,  0,  0,  0,  0, -5,   // rank 2
    -5,  0,  0,  0,  0,  0,  0, -5,   // rank 3
    -5,  0,  0,  0,  0,  0,  0, -5,   // rank 4
    -5,  0,  0,  0,  0,  0,  0, -5,   // rank 5
    -5,  0,  0,  0,  0,  0,  0, -5,   // rank 6
     5, 10, 10, 10, 10, 10, 10,  5,   // rank 7  ← the "7th rank" bonus
     0,  0,  0,  0,  0,  0,  0,  0,   // rank 8
};

// ── Queen ────────────────────────────────────────────────────
// Avoid early development; slight central preference
static const int queen_pst[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,   // rank 1
   -10,  0,  5,  0,  0,  0,  0,-10,   // rank 2
   -10,  5,  5,  5,  5,  5,  0,-10,   // rank 3
     0,  0,  5,  5,  5,  5,  0, -5,   // rank 4
    -5,  0,  5,  5,  5,  5,  0, -5,   // rank 5
   -10,  0,  5,  5,  5,  5,  0,-10,   // rank 6
   -10,  0,  0,  0,  0,  0,  0,-10,   // rank 7
   -20,-10,-10, -5, -5,-10,-10,-20,   // rank 8
};

// ── King — Middlegame ─────────────────────────────────────────
// Castle and hide behind pawns; punish centralisation
static const int king_mg_pst[64] = {
    20, 30, 10,  0,  0, 10, 30, 20,   // rank 1  ← castled positions safe
    20, 20,  0,  0,  0,  0, 20, 20,   // rank 2
   -10,-20,-20,-20,-20,-20,-20,-10,   // rank 3
   -20,-30,-30,-40,-40,-30,-30,-20,   // rank 4
   -30,-40,-40,-50,-50,-40,-40,-30,   // rank 5
   -30,-40,-40,-50,-50,-40,-40,-30,   // rank 6
   -30,-40,-40,-50,-50,-40,-40,-30,   // rank 7
   -30,-40,-40,-50,-50,-40,-40,-30,   // rank 8
};

// ── King — Endgame ────────────────────────────────────────────
// In endgame, king should be active and central
static const int king_eg_pst[64] = {
   -50,-30,-30,-30,-30,-30,-30,-50,   // rank 1
   -30,-30,  0,  0,  0,  0,-30,-30,   // rank 2
   -30,-10, 20, 30, 30, 20,-10,-30,   // rank 3
   -30,-10, 30, 40, 40, 30,-10,-30,   // rank 4
   -30,-10, 30, 40, 40, 30,-10,-30,   // rank 5
   -30,-10, 20, 30, 30, 20,-10,-30,   // rank 6
   -30,-20,-10,  0,  0,-10,-20,-30,   // rank 7
   -50,-40,-30,-20,-20,-30,-40,-50,   // rank 8
};

// ══════════════════════════════════════════════════════════════
// Usage in evaluate():
//
//   int piece_value(Piece p, int sq, Color c, bool endgame) {
//       int s = (c == WHITE) ? sq : mirror(sq);
//       switch(p) {
//           case PAWN:   return 100 + pawn_pst[s];
//           case KNIGHT: return 320 + knight_pst[s];
//           case BISHOP: return 330 + bishop_pst[s];
//           case ROOK:   return 500 + rook_pst[s];
//           case QUEEN:  return 900 + queen_pst[s];
//           case KING:   return endgame ? king_eg_pst[s] : king_mg_pst[s];
//           default:     return 0;
//       }
//   }
//
//   int evaluate(Board& board) {
//       int score = 0;
//       bool eg = is_endgame(board); // e.g. no queens, or low material
//       for (int sq = 0; sq < 64; sq++) {
//           Piece p = board.piece_on[sq];
//           Color c = board.color_on[sq];
//           if (p == NONE) continue;
//           int val = piece_value(p, sq, c, eg);
//           score += (c == WHITE) ? val : -val;
//       }
//       return score; // positive = White ahead
//   }
// ══════════════════════════════════════════════════════════════
