# TigerFish Engine — Complete Operations Manual

> **If you forgot everything about this codebase, read this document.** It explains every file, every function, every data structure, every algorithm, every design decision, and how to modify any part of the engine.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Build & Run](#2-build--run)
3. [Include Cascade & Compilation Model](#3-include-cascade--compilation-model)
4. [Layer 1: magic_lut.cpp — Sliding Piece Attack Tables](#4-layer-1-magic_lutcpp)
5. [Layer 2: pst_tables.h — Piece-Square Tables](#5-layer-2-pst_tablesh)
6. [Layer 3: board.cpp — Board Representation & State](#6-layer-3-boardcpp)
7. [Layer 4: rules.cpp — Legal Move Generation & Game Rules](#7-layer-4-rulescpp)
8. [Layer 5: eval.cpp — Position Evaluation](#8-layer-5-evalcpp)
9. [Layer 6: search.cpp — Alpha-Beta Search Engine](#9-layer-6-searchcpp)
10. [Layer 7: main.cpp — CLI, UCI, and API Entry Points](#10-layer-7-maincpp)
11. [Data Flow: How a Move Gets Found](#11-data-flow-how-a-move-gets-found)
12. [Multi-Threading Architecture](#12-multi-threading-architecture)
13. [Key Constants & Tuning Parameters](#13-key-constants--tuning-parameters)
14. [How to Modify Each Component](#14-how-to-modify-each-component)
15. [Testing & Verification](#15-testing--verification)

---

## 1. Architecture Overview

TigerFish is a single-translation-unit C++20 chess engine. All code compiles through one linear `#include` cascade starting from `main.cpp`. There are **no separate object files or link steps** — `g++` compiles `main.cpp` and recursively includes everything.

```
main.cpp          ← CLI/UCI/API dispatcher (entry point, the ONLY file you compile)
  └── search.cpp  ← Alpha-beta search, transposition table, move ordering
        └── eval.cpp    ← Evaluation function (tapered PST + mobility + king safety)
              └── rules.cpp   ← Legal move generation, check detection, SEE, game result
                    └── board.cpp   ← Board state, bitboards, make/unmake move, Zobrist hashing
                          ├── pst_tables.h  ← Piece-square tables (compile-time constants)
                          └── magic_lut.cpp ← Magic bitboard attack lookup tables
```

**Dependencies flow downward only.** A lower layer never `#include`s a higher layer. `board.cpp` knows nothing about search or evaluation.

### Why Single Translation Unit?

- Zero linking overhead — the compiler sees everything and can inline aggressively
- LTO (Link-Time Optimization) is free — already one compilation unit
- Simpler build: one `g++` command, no Makefile needed
- Trade-off: full recompilation on any change (~8 seconds)

---

## 2. Build & Run

### Compile

```bash
# Production build (uses all CPU features of the host machine)
g++ -O3 -std=c++20 -pthread -march=native engine/main.cpp -o game.exe

# Portable build (runs on any x86-64 CPU)
g++ -O3 -std=c++20 -pthread engine/main.cpp -o game.exe
```

`-march=native` enables hardware `POPCNT`, `TZCNT`, `BLSR`, `LZCNT` intrinsics. Without it, the engine uses portable `__builtin_*` fallbacks — slightly slower but works everywhere.

### Run

```bash
# UCI protocol (default — for Lichess, Arena, CuteChess)
./game.exe
./game.exe uci

# One-shot analysis (returns JSON)
./game.exe analyze "<FEN>" <depth> [--threads <N>] [--nodes <limit>] [--time <ms>]

# Interactive JSON session (for web backends)
./game.exe interactive

# List legal moves (returns JSON)
./game.exe moves "<FEN>"

# Make a move and return new state (returns JSON)
./game.exe make "<FEN>" "<uci_move>"

# Get best move (returns JSON with bot_move field)
./game.exe best "<FEN>" [depth]
```

---

## 3. Include Cascade & Compilation Model

Each file starts with `#pragma once` and `#include`s the file below it:

| File | Includes | What It Adds |
|---|---|---|
| `magic_lut.cpp` | `<cstdint>, <array>` | Ray tables, magic numbers, precomputed slider attacks |
| `pst_tables.h` | `<cstdint>` | Compile-time piece-square value tables |
| `board.cpp` | `pst_tables.h`, `magic_lut.cpp` | Board class, move encoding, make/unmake, Zobrist |
| `rules.cpp` | `board.cpp` | MoveList, MoveGenerator, check detection, SEE, game result |
| `eval.cpp` | `rules.cpp` | `evaluate()` — tapered eval with all features |
| `search.cpp` | `eval.cpp` | Engine class, TT, alpha-beta, quiescence, threading |
| `main.cpp` | `search.cpp` | `main()`, UCI loop, CLI commands, `analyze()` API |

**Rule**: Never add a reverse dependency. If `eval.cpp` needs something from `search.cpp`, that's a design error.

---

## 4. Layer 1: magic_lut.cpp

**Purpose**: Precomputes all possible attack bitboards for sliding pieces (rooks, bishops, queens) at startup.

### How Magic Bitboards Work

The problem: given a rook on square `e4` with pieces on `e6` and `e2`, what squares can it attack? Calculating this at runtime requires scanning each direction until hitting a blocker — too slow for millions of calls per second.

The solution: **precompute every possible answer** and store it in a lookup table, indexed by a "magic hash" of the blocker configuration.

### Key Data Structures

```cpp
array<array<uint64_t,64>,8> ray_table;     // ray_table[direction][square] = ray bitboard
uint64_t ROOK_MAGICS[64];                  // Hardcoded magic multiplier per square
uint64_t BISHOP_MAGICS[64];                // Same for bishops
int ROOK_SHIFT[64];                        // Right-shift amount = 64 - popcount(mask)
int BISHOP_SHIFT[64];
uint64_t ROOK_MASK[64];                    // Relevant blocker squares (edges excluded)
uint64_t BISHOP_MASK[64];

// Dense flat attack tables — saves ~1.5 MB vs fixed 2D arrays
// Rook: 102,400 entries (800 KB) vs old 262,144 (2 MB). Bishop: 5,248 (41 KB) vs old 32,768 (256 KB).
inline uint64_t ROOK_TABLE[102400];
inline uint64_t BISHOP_TABLE[5248];

// Per-square pointers into flat tables — ROOK_ATTACKS[sq][idx] syntax preserved
inline uint64_t* ROOK_ATTACKS[64];
inline uint64_t* BISHOP_ATTACKS[64];
```

### Key Functions

| Function | Purpose |
|---|---|
| `init_rays()` | Fills `ray_table[8][64]` — a ray bitboard for each of 8 directions from each square |
| `init_magics()` | For each square: computes blocker mask, maps pointers into dense tables, enumerates all 2^N blocker subsets via carry-ripple `next_subset()`, and stores attack bitboards (using PEXT when `__BMI2__` is defined, or magic hash otherwise) |
| `compute_rook_attacks(sq, blockers)` | Slow reference impl — slides N/S/E/W, stops at first blocker (inclusive). Only used during init |
| `compute_bishop_attacks(sq, blockers)` | Same but for diagonals (NE/NW/SE/SW) |
| `next_subset(sub, mask)` | Carry-ripple trick `(sub-1) & mask` — iterates all subsets of a bitmask |

### The Lookup Formula (used millions of times per second)

```cpp
// In rules.cpp — dual-path execution:
inline uint64_t rook_attacks(int sq, uint64_t occ) {
#if defined(__BMI2__)
    return ROOK_ATTACKS[sq][_pext_u64(occ, ROOK_MASK[sq])];                // Hardware PEXT: 1 instruction
#else
    occ &= ROOK_MASK[sq];
    return ROOK_ATTACKS[sq][(occ * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]];   // Magic hash: 3 instructions
#endif
}

inline uint64_t bishop_attacks(int sq, uint64_t occ) {
#if defined(__BMI2__)
    return BISHOP_ATTACKS[sq][_pext_u64(occ, BISHOP_MASK[sq])];
#else
    occ &= BISHOP_MASK[sq];
    return BISHOP_ATTACKS[sq][(occ * BISHOP_MAGICS[sq]) >> BISHOP_SHIFT[sq]];
#endif
}
```

**Runtime cost**:
- **With BMI2 (`-march=native` on modern x86)**: 1 PEXT + 1 TABLE LOAD ≈ 1–2 cycles.
- **Generic fallback**: 1 AND + 1 MULTIPLY + 1 SHIFT + 1 TABLE LOAD ≈ 3–4 cycles.

### Direction Encoding

```
Direction 0 = NORTH (+1 rank),  1 = SOUTH (-1 rank)
Direction 2 = EAST  (+1 file),  3 = WEST  (-1 file)
Direction 4 = NE,  5 = SE,  6 = NW,  7 = SW
```

Even-numbered directions go "forward" (toward higher bit indices), odd-numbered go "backward." This determines whether we use `lsb_index()` or `msb_index()` when scanning rays for blockers.

### How to Modify

- **Adding new magic numbers**: Replace the hardcoded arrays. You'd write a magic-number finder that brute-force tests random numbers until one produces no index collisions.
- **Dense allocation**: Each square occupies exactly `1 << popcount(mask)` entries contiguous in `ROOK_TABLE` / `BISHOP_TABLE`. Total entries sum to exactly 102,400 for rooks and 5,248 for bishops.

---

## 5. Layer 2: pst_tables.h

**Purpose**: Defines piece-square tables (PSTs) — bonus/penalty values for each piece type on each square.

### What PSTs Do

A knight on `d4` (center) is worth more than a knight on `a1` (corner). PSTs encode this positional knowledge as a signed integer array. The engine adds the PST value to the piece's base material value to get a more accurate positional score.

### Tables Defined

Each table is `static inline constexpr int TABLE[64]`. Squares are laid out rank-by-rank from rank 1 (index 0–7) to rank 8 (index 56–63):

| Table | What It Encourages |
|---|---|
| `PAWN_PST_MG[64]` | Center pawns, advanced pawns (rank 6–7 get +50) |
| `KNIGHT_PST_MG[64]` | Centralization (+20 for d4/d5/e4/e5), rim punishment (-50 for corners) |
| `BISHOP_PST_MG[64]` | Long diagonals, avoid edges |
| `ROOK_PST_MG[64]` | 7th rank bonus (+10), back rank neutral |
| `QUEEN_PST_MG[64]` | Slightly centralized, avoid edges |
| `KING_PST_MG[64]` | **Castled position** (g1/c1 = +30), center = terrible (-50) |
| `KING_PST_EG[64]` | **Centralization** (d4/d5/e4/e5 = +30), opposite of middlegame |
| All `*_PST_EG[64]` | Endgame variants of each table |

### The Flattened Accumulator Table (`FlatPstTable`)

To enable O(1) incremental PST evaluation, a `constexpr` struct pre-computes a flattened 2D array indexed by `[piece 0..11][square 0..63]`:

```cpp
struct FlatPstTable {
    int mg[12][64];  // Middlegame PST values
    int eg[12][64];  // Endgame PST values
};
inline constexpr FlatPstTable FLAT_PST{};
#define PIECE_MG_PST FLAT_PST.mg
#define PIECE_EG_PST FLAT_PST.eg
```

**Key detail**: White pieces (0–5) store positive values at the natural square. Black pieces (6–11) store **negative** values at the **mirrored** square (`mirror_square(sq) = (7 - sq/8) * 8 + sq%8`). This means `board.mg_pst` is always from White's perspective and can be read directly without per-color branching.

### How to Modify

- **Change positional values**: Edit the raw `*_PST_MG` / `*_PST_EG` arrays. The `FlatPstTable` constructor automatically picks them up at compile time.
- **Add new game phases**: Add a third set of tables and a third accumulator in `Board`.

---

## 6. Layer 3: board.cpp

**Purpose**: The board representation — bitboards, piece placement, make/unmake move, Zobrist hashing, FEN parsing.

### Board Representation (Dual Representation)

The `Board` class stores the position in **two redundant formats** for speed:

```cpp
array<uint64_t, 12> bitboards;   // One 64-bit mask per piece type (P=0..k=11)
array<uint64_t, 3>  occupancy;   // [WHITE]=union of white pieces, [BLACK]=union of black, [2]=all
Piece piece_on[64];              // Mailbox: piece_on[sq] = which piece is on that square (0xF = empty)
```

**Why both?**
- **Bitboards** are fast for scanning (find all white knights: `bitboards[N]`), attack generation, and bulk operations.
- **Mailbox** is fast for "what piece is on e4?" lookups (needed for capture detection, MVV-LVA scoring).

### Piece Encoding

```cpp
enum Piece { P=0, N=1, B=2, R=3, Q=4, K=5,   // White pieces
             p=6, n=7, b=8, r=9, q=10, k=11 }; // Black pieces
// 0xF (15) = empty square
```

To get piece type (0–5): `type = piece % 6`
To get color: `color = piece < 6 ? WHITE : BLACK`

### Square Indexing

Squares are numbered 0–63 in **Little-Endian Rank-File (LERF)** order:

```
a1=0, b1=1, ..., h1=7
a2=8, b2=9, ..., h2=15
...
a8=56, b8=57, ..., h8=63
```

`rank = sq / 8`, `file = sq % 8`.

### 16-bit Move Encoding

Every move is packed into a `uint16_t`:

```
Bits  0-5:  from_square (0-63)
Bits  6-11: to_square   (0-63)
Bits 12-15: flags
```

**Flag encoding**:

| Flags | Meaning | Bit 15 (Promo?) | Bit 14 (Capture?) |
|:---:|---|:---:|:---:|
| `0x0` | Quiet move | 0 | 0 |
| `0x1` | Double pawn push | 0 | 0 |
| `0x2` | King-side castle | 0 | 0 |
| `0x3` | Queen-side castle | 0 | 0 |
| `0x4` | Capture | 0 | 1 |
| `0x5` | En passant capture | 0 | 1 |
| `0x8` | Promote to knight | 1 | 0 |
| `0x9` | Promote to bishop | 1 | 0 |
| `0xA` | Promote to rook | 1 | 0 |
| `0xB` | Promote to queen | 1 | 0 |
| `0xC` | Capture + promote knight | 1 | 1 |
| `0xD` | Capture + promote bishop | 1 | 1 |
| `0xE` | Capture + promote rook | 1 | 1 |
| `0xF` | Capture + promote queen | 1 | 1 |

**Helper functions**: `move_from(m)`, `move_to(m)`, `move_flags(m)`, `move_is_capture(m)`, `move_is_promotion(m)`, `move_is_castle(m)`, `move_is_en_passant(m)`, `move_is_double_push(m)`, `pack_move(from, to, flags)`, `move_to_uci(m)`.

### Zobrist Hashing

Every position has a 64-bit hash used for the transposition table and repetition detection.

```cpp
uint64_t ZOBRIST_PIECE[12][64];  // Random key for each (piece, square) pair
uint64_t ZOBRIST_SIDE;           // XOR'd in when Black to move
uint64_t ZOBRIST_CASTLING[16];   // One key per 4-bit castling rights combination
uint64_t ZOBRIST_EP[65];         // One key per EP square (index 64 = no EP)
```

Generated deterministically from seed `1070372` via SplitMix64 PRNG (`rng64()`), so hashes are identical across all builds.

**Incremental update**: `make_move()` XORs out old piece/square, XORs in new piece/square, XORs castling changes, XORs EP changes, XORs side-to-move. This is O(1) — no full recomputation.

### make_move() — The Most Critical Function

`Board::make_move(uint16_t move)` applies a move to the board. It:

1. **Saves undo state** to `history_stack[history_ply]` (move, captured piece, castling, EP, halfmove, material, phase, mg_pst, eg_pst, zobrist hash)
2. **Updates Zobrist hash** incrementally (XOR out from-square, XOR in to-square)
3. **Moves the piece** in bitboards + mailbox
4. **Updates PST accumulators** incrementally: `mg_pst += PST[piece][to] - PST[piece][from]`
5. **Handles special moves**:
   - **Promotion**: removes pawn bitboard from to-square, adds promoted piece
   - **En passant**: removes captured pawn from capture square (to±8)
   - **Castling**: moves rook from corner to castled position
   - **Capture**: removes captured piece from enemy bitboard, updates material/phase
6. **Updates castling rights** (king move clears both sides; rook move/capture clears one side)
7. **Sets en passant** square on double pawn pushes, clears otherwise
8. **Flips side to move** and increments fullmove number

### unmake_move() — Exact Reverse

`Board::unmake_move()` pops the undo state:

1. Restores `castling_rights`, `en_passant`, `halfmove_clock`, `material_score`, `phase_score`, `mg_pst`, `eg_pst`, `zobrist_hash` directly from `UndoState` (O(1) — no recalculation)
2. Flips side to move back
3. Reverses piece movement in bitboards + mailbox
4. Restores captured piece (if any)
5. Reverses castling rook movement (if castle)

**Critical invariant**: After `make_move()` + `unmake_move()`, the board state is bit-for-bit identical to the original. The Zobrist hash is restored from the saved copy, not recalculated.

### is_repetition()

Checks the Zobrist hash history for threefold repetition:

```cpp
bool is_repetition(int count = 3) const {
    // Only check positions within the irreversible move window (halfmove_clock)
    // Step by 2 (same side to move) comparing saved Zobrist hashes
}
```

### Hardware Bit Intrinsics

Defined at the top of `board.cpp` with compile-time `#ifdef` fallbacks:

| Function | With `-march=native` | Without (portable) |
|---|---|---|
| `popcount64(mask)` | `_mm_popcnt_u64` (1 cycle) | `__builtin_popcountll` or `std::popcount` |
| `lsb_index(mask)` | `_tzcnt_u64` (1 cycle) | `__builtin_ctzll` or `std::countr_zero` |
| `msb_index(mask)` | `63 - _lzcnt_u64` (1 cycle) | `63 - __builtin_clzll` or `std::countl_zero` |
| `pop_lsb(mask)` | `_tzcnt_u64` + `_blsr_u64` (2 cycles) | `ctz` + `mask &= mask - 1` |

These are called millions of times per second. On modern x86, each compiles to a single CPU instruction.

---

## 7. Layer 4: rules.cpp

**Purpose**: Legal move generation, attack map computation, check detection, pin handling, Static Exchange Evaluation (SEE), and game result determination.

### MoveList

```cpp
struct MoveList {
    uint16_t move_list[218];  // Max 218 legal moves in any chess position
    int scores[218];          // Ordering scores (higher = try first)
    int count = 0;            // Number of moves stored
    void push(uint16_t m, int score = 0);
    void sort_mvv_lva();      // Insertion sort by score (descending)
};
```

### Slider Attack Lookups (O(1))

These are the hot-path functions called from move generation and evaluation:

```cpp
uint64_t rook_attacks(int sq, uint64_t occ);   // Magic bitboard lookup
uint64_t bishop_attacks(int sq, uint64_t occ);  // Magic bitboard lookup
uint64_t queen_attacks(int sq, uint64_t occ);   // rook | bishop
```

### Precomputed Non-Slider Tables

```cpp
uint64_t KNIGHT_ATTACKS_TABLE[64];   // All knight moves from each square
uint64_t PAWN_ATTACKS_TABLE[2][64];  // Pawn capture squares [WHITE/BLACK][square]
```

Built once by `init_rules_luts()`.

### PinInfo — How Pinned Pieces Are Tracked

A "pin" means a piece that cannot move off its ray because doing so would expose the king to check.

```cpp
struct PinInfo {
    uint64_t pinned = 0;           // Bitboard of all pinned pieces
    uint64_t pin_mask[64];         // For each square: the mask of squares it CAN move to
    PinInfo() { memset(pin_mask, 0xFF, sizeof(pin_mask)); } // Default: can move anywhere
};
```

`find_pins(board)` scans all 8 rays outward from the king:
1. Find the first blocker on the ray — if it's friendly, it might be pinned
2. Find the second blocker — if it's an enemy slider aligned with the ray direction, the first piece IS pinned
3. Set `pin_mask[pinned_sq]` to the full ray (the piece can only move along the pin line)

**Slider alignment check** (why `type % 6`):
```cpp
int type = piece % 6;  // 0=P, 1=N, 2=B, 3=R, 4=Q, 5=K
bool is_slider = (dir < 4) ? (type == 3 || type == 4)    // Orthogonal: Rook or Queen
                            : (type == 2 || type == 4);   // Diagonal: Bishop or Queen
```

### MoveGenerator — The Main Legal Move Generator

`MoveGenerator::generate_moves(board, moves)` is the entry point. It:

1. Calls `find_pins(board)` to get pin information
2. Builds the **enemy attack map** by calling:
   - `pawnatk()` — all enemy pawn attack squares, detects pawn checks
   - `knightatk()` — all enemy knight attack squares, detects knight checks
   - `sliding_atks(bishop)` — enemy bishop attacks (computed with king removed from occupancy so rays extend through king)
   - `sliding_atks(rook)` — enemy rook attacks
   - `sliding_atks(queen)` — enemy queen attacks
   - `kingatk()` — enemy king attack squares
3. Counts checkers (`popcount64(checkers)`)
4. Dispatches:
   - **No check (check_count == 0)**: Generate all piece moves with `legal_mask = ~0ULL` (any destination)
   - **Single check (check_count == 1)**: Compute `legal_mask` = squares that block or capture the checker; only king and interposing moves are legal
   - **Double check (check_count >= 2)**: Only king moves are legal

### Piece Move Generation Functions

| Function | Piece | How It Works |
|---|---|---|
| `PawnMoves()` | Pawns | Bitboard shift for pushes/captures, checks pin_mask for each pawn, handles double pushes, promotions (4 pieces × capture/quiet), en passant with `ep_exposes_king()` |
| `KnightMoves()` | Knights | Strips pinned knights upfront (pinned knight can NEVER move), uses `knight_attacks()` for L-shape moves |
| `BishopMoves()` | Bishops | Calls `append_sliding_moves()` with `dir_start=4, dir_end=8` (diagonal) |
| `RookMoves()` | Rooks | Calls `append_sliding_moves()` with `dir_start=0, dir_end=4` (orthogonal) |
| `QueenMoves()` | Queens | Calls `append_sliding_moves()` with `dir_start=0, dir_end=8` (all) |
| `KingMoves()` | King | 8 adjacent squares, filtered by enemy attack map. Plus castling (checks for path clear, no attack through path, rook present) |

### generate_captures() — Quiescence Search Optimization

A specialized capture-only generator that skips quiet moves. Key optimization: if `known_not_in_check` is true AND the king has no adjacent enemy pieces, it **completely skips** building the enemy attack map (saves 6 function calls ≈ 15% of quiescence time).

### is_in_check() — Fast Check Detection

Instead of building the full attack map, tests outward from the king square using precomputed tables:

```cpp
// 1. Pawn attacks: PAWN_ATTACKS_TABLE[side][king_sq] & enemy_pawns
// 2. Knight attacks: KNIGHT_ATTACKS_TABLE[king_sq] & enemy_knights
// 3. Bishop+Queen diagonal: bishop_attacks(king_sq, occ) & (enemy_bishops | enemy_queens)
// 4. Rook+Queen orthogonal: rook_attacks(king_sq, occ) & (enemy_rooks | enemy_queens)
```

4 checks with early return — much faster than full attack map generation.

### Static Exchange Evaluation (SEE)

`see(board, move)` — fast approximation: compares attacker and victim values.
`see_full(board, move)` — full iterative capture simulation:

1. Start with `gain[0] = victim_value`
2. Find least valuable enemy recapturer → `gain[1] = attacker_value - gain[0]`
3. Find our least valuable re-recapturer → repeat
4. Negamax the gain array backward

Used in move ordering (lossy captures get sorted to the bottom) and quiescence pruning.

### get_game_result() — Terminal State Detection

```cpp
GameResult get_game_result(board):
  1. Generate all legal moves
  2. If 0 moves: checkmate (if in check) or stalemate
  3. If halfmove_clock >= 150: seventy-five move draw
  4. If halfmove_clock >= 100: fifty move draw
  5. If insufficient material (K vs K, K+B vs K, K+N vs K, same-color bishops): draw
  6. If threefold repetition: draw
  7. Otherwise: ongoing
```

---

## 8. Layer 5: eval.cpp

**Purpose**: Evaluate a chess position and return a score in centipawns from White's perspective.

### The Evaluation Pipeline

`evaluate(Board& board)` calls five sub-evaluators in sequence, accumulating separate middlegame (mg) and endgame (eg) scores, then blends them:

```
1. eval_tapered()                → Material + PST (O(1), read from Board accumulators)
2. eval_bishop_pair()            → ±40 MG / ±50 EG for having two bishops
3. eval_pawn_structure()         → Doubled, isolated, passed pawn bonuses/penalties
4. eval_mobility_and_king_safety() → Piece mobility + pawn shield + attacker count
5. Tapered blend:  score = (mg × phase + eg × (24 - phase)) / 24
```

### 1. Tapered Eval — O(1) Material + PST

```cpp
mg = board.material_score + board.mg_pst;
eg = board.material_score + board.eg_pst;
```

These accumulators are maintained incrementally in `make_move()` / restored in `unmake_move()`. **Zero loop, zero bitboard scan.** This was the #1 optimization — the old version scanned all 12 bitboards every eval call.

### 2. Bishop Pair

If a side has ≥2 bishops: +40 MG, +50 EG. Bishops are stronger together on opposite colors.

### 3. Pawn Structure

- **Doubled pawns**: Count pawns per file; extra pawns on same file get -12 penalty each
- **Isolated pawns**: Pawns with no friendly pawn on adjacent files get -15 penalty
- **Passed pawns**: Pawns with no enemy pawns ahead on same/adjacent files get bonus scaled by rank (higher rank = bigger bonus; EG values are much larger than MG)
- **Connected passed pawns**: Adjacent passed pawns get +12 MG / +24 EG bonus

### 4. Mobility & King Safety

For each piece (knights, bishops, rooks, queens):
1. Compute attack bitboard using magic lookups
2. Count safe squares (attacks & ~own_occupancy)
3. Add mobility score from `MOBILITY_MG/EG[count]` (clamped to 0–8)
4. If attacks hit the enemy **king zone** (8 squares around king), increment attacker count

King safety (MG only):
- **Pawn shield**: +8 per friendly pawn in front of king (2 ranks ahead, 3 files wide)
- **Open files near king**: -20 per file adjacent to king with no friendly pawn
- **Attacker penalty**: Quadratic scaling from 0 to -230 based on number of pieces attacking king zone

### 5. Tapered Blend

The game phase (0–24) determines the MG/EG blend ratio:

```cpp
int phase = min(board.phase_score, 24);  // Knight=1, Bishop=1, Rook=2, Queen=4
int score = (mg * phase + eg * (24 - phase)) / 24;
```

Phase 24 = opening (all pieces), phase 0 = pure endgame (just kings + pawns).

### Return Value Convention

`evaluate()` returns the score **from White's perspective** (positive = White is better).

The search function `evaluate_for_side()` negates this for Black:
```cpp
int evaluate_for_side(board) {
    return board.side_to_move == WHITE ? evaluate(board) : -evaluate(board);
}
```

---

## 9. Layer 6: search.cpp

**Purpose**: The alpha-beta search engine — finds the best move by exploring the game tree.

### Transposition Table (TT)

A global flat array shared across all threads:

```cpp
static constexpr size_t TT_SIZE = 2097152;  // 2^21 entries ≈ 64 MB
static TranspositionEntry TT_GLOBAL[TT_SIZE];
```

Each entry is 16 bytes with atomic fields for lockless concurrent access:

```cpp
struct TranspositionEntry {
    atomic<uint64_t>  key;         // Zobrist hash (for collision detection)
    atomic<int32_t>   evaluation;  // Score at this position
    atomic<uint32_t>  meta;        // Packed: age(7b) | depth(7b) | flag(2b)
    atomic<uint16_t>  best_move;   // Best move found
};
```

**Probe** (`probe(key, depth, alpha, beta, result, tt_move, ply)`):
1. Index: `key & (TABLE_SIZE - 1)`
2. Verify key match (double-read with `acquire` fence for thread safety)
3. Return TT move even if depth is insufficient
4. Return score only if `stored_depth >= depth` AND flag allows cutoff:
   - `TT_EXACT`: always usable
   - `TT_ALPHA` (upper bound): usable if `score <= alpha`
   - `TT_BETA` (lower bound): usable if `score >= beta`

**Store** (`store(key, depth, score, flag, best_move, ply)`):
- Always-replace policy with age-based priority: overwrite if different key, deeper search, or old generation
- Mate scores are adjusted relative to root: `score_to_transposition_table()` adds `ply` to mate scores so they're stored correctly for any search path

### Engine Class

```cpp
class Engine {
    TranspositionTable tt;              // Local TT (or shared via pointer)
    uint64_t nodes_searched;            // Total nodes visited
    uint64_t nodes_limit;               // Stop after this many nodes (0 = unlimited)
    int time_limit_ms;                  // Stop after this many ms (0 = unlimited)
    int thread_count;                   // Number of search threads
    int thread_id;                      // This thread's ID (0 = main)
    TranspositionTable* shared_tt;      // Points to shared TT in multi-threaded mode
    const atomic<bool>* external_stop_flag; // UCI "stop" signal
    bool search_stopped;                // Set when any stop condition triggers
    int completed_depth;                // Deepest fully completed iteration
    vector<uint16_t> root_moves;        // Best moves per iteration
    vector<int> root_scores;            // Best scores per iteration
    uint16_t killer_moves[128][2];      // Quiet moves that caused beta cutoffs
    int history_table[2][64][64];       // History heuristic scores
    uint64_t node_check_mask;           // How often to check stop conditions
};
```

### should_stop() — Time & Node Management

Called every `node_check_mask` nodes (1024 for blitz, 4096 for standard, 65536 for fixed-depth). Checks:
1. External stop flag (UCI "stop" command)
2. Node limit exceeded
3. Time limit exceeded

### Move Ordering — order_moves()

Move ordering is critical for alpha-beta efficiency. Moves are scored:

| Priority | Score | Source |
|---|---|---|
| **TT move** | 20,000,000 | Always tried first |
| **Winning/equal captures** | 10,000+ | MVV-LVA: `victim_val * 10 - attacker_val` |
| **Losing captures** (SEE < 0) | -5,000+ | Full SEE simulation |
| **Killer moves** | +1000/+500 | Quiet moves that caused cutoffs at same ply |
| **History heuristic** | 0–16384 | Accumulated from previous cutoffs at [side][from][to] |

Sorted by insertion sort (optimal for small N, good cache behavior).

### search() — The Main Alpha-Beta Function

```
search(board, depth, alpha, beta, ply, pv_node) → int score
```

**Flow**:

1. **Node counting and stop check**
2. **Draw detection**: repetition or 50-move rule → return 0
3. **Mate distance pruning**: tighten alpha/beta based on theoretical fastest mate
4. **TT probe**: if hit with sufficient depth and right flag, return cached score
5. **Check detection**: `is_in_check(board)`
6. **Depth ≤ 0 and not in check**: drop into `quiescence()`
7. **Static eval**: for pruning decisions
8. **Reverse futility pruning**: if eval is way above beta at depth ≤ 3, return immediately
9. **Null move pruning**: skip our turn; if we're still above beta (and have non-pawn material), prune. Reduction: `R = 3 + depth / 4`
10. **Generate all legal moves** — if zero, return mate or stalemate score
11. **Order moves** using TT move, MVV-LVA, SEE, killers, history
12. **Search each move**:
    - **Futility pruning**: at depth ≤ 2, skip quiet moves if eval is far below alpha
    - **Late move pruning**: at depth ≤ 2, skip quiet moves beyond `6 + depth * 2`
    - **Late move reduction (LMR)**: after the first few moves at depth ≥ 3, reduce quiet move depth by 1–2
    - **Principal Variation Search (PVS)**: first move gets full window; later moves get null window, re-search on fail-high
    - **TT prefetch**: `_mm_prefetch` the child position's TT entry into L1 cache right after `make_move()`
13. **Store result in TT** with appropriate flag (EXACT/ALPHA/BETA)
14. **Update killer moves and history** for quiet beta-cutoff moves

### quiescence() — Captures-Only Horizon Search

Prevents "horizon effect" by searching all captures and promotions until the position is quiet:

1. **Stand-pat**: evaluate position; if ≥ beta, return (the position is already too good)
2. **Delta pruning**: skip captures where the captured piece value + margin can't reach alpha
3. **SEE pruning**: skip captures where SEE is negative (losing trade)
4. **Recurse**: for each capture, search `-quiescence(-beta, -alpha)`
5. In check: generate ALL evasion moves, return mate score if no legal moves

### search_depth() — Root Search with Aspiration Windows

```
search_depth(board, depth) → uint16_t best_move
```

At the root level:
1. Generates all moves, sorts by MVV-LVA
2. Prioritizes the previous iteration's best move to the front
3. Uses **aspiration windows** at depth ≥ 4: starts with a narrow ±25 window around previous score, widens on fail-high/fail-low
4. Uses PVS: first move gets full window, rest get null window

### best_move() — Iterative Deepening with Multi-Threading

```
best_move(board, depth) → uint16_t
```

1. Detects hardware thread count (fixes MinGW 8-thread bug via `GetActiveProcessorCount`)
2. Initializes TT, clears killer/history tables
3. If single-threaded: iterate `search_depth()` from depth 1 to target
4. If multi-threaded: spawn `thread_count - 1` worker threads, each running independent `worker_search()`, while the main thread also searches. All share the same TT.
5. After search completes, aggregate worker node counts

### Killer Moves

```cpp
uint16_t killer_moves[MAX_SEARCH_PLY][2];
```

When a quiet move causes a beta cutoff:
- Move slot [1] to slot [0]
- Store new killer in slot [1]

Killers get +1000/+500 bonus in move ordering — they're "brother moves" that were good in sibling positions at the same ply.

### History Heuristic

```cpp
int history_table[2][64][64];  // [side][from_sq][to_sq]
```

Updated when a quiet move causes a beta cutoff:
```cpp
int bonus = min(16384, depth * depth);
value += bonus - value * bonus / 16384;  // Gravity formula: prevents explosion
```

Decayed by `>>= 1` (halved) at the start of each iterative deepening iteration.

---

## 10. Layer 7: main.cpp

**Purpose**: Entry points — parses command-line arguments and routes to UCI mode, interactive mode, or one-shot CLI commands.

### main() Dispatch

```cpp
int main(int argc, char* argv[]) {
    init_rays();    // Build ray tables
    init_magics();  // Build magic attack tables

    if (argc == 1 || argv[1] == "uci")  → run_uci()
    if (argv[1] == "interactive")        → run_interactive()
    if (argv[1] == "analyze")            → analyze() → print JSON
    if (argv[1] == "moves")              → generate moves → print JSON
    if (argv[1] == "make")               → apply move → print JSON
    if (argv[1] == "best")               → best_move() → print JSON
}
```

### SearchOptions & AnalysisResult

```cpp
struct SearchOptions {
    int depth_limit = 5;
    uint64_t nodes_limit = 0;
    int time_limit_ms = 0;
    int multi_pv = 1;
    int thread_count = 1;
    atomic<bool>* stop_flag = nullptr;
};

struct AnalysisResult {
    string best_move;     // UCI format ("e2e4")
    float evaluation;     // Centipawn / 100 (or ±100 for mate)
    int eval_cp;          // Raw centipawn score
    int depth;            // Completed depth
    uint64_t nodes;       // Total nodes searched
    int time_ms;          // Wall-clock time
    uint64_t nps;         // Nodes per second
    uint64_t tt_hits/misses;
    int thread_count;
    vector<string> pv;    // Principal variation (best line)
    string to_json();     // Serialize to JSON
};
```

### analyze() — One-Shot Analysis API

Creates an `Engine`, configures search limits, calls `best_move()`, measures time, packages results into `AnalysisResult`.

### run_uci() — UCI Protocol Loop

Implements the Universal Chess Interface for GUIs and bot bridges:

| Command | Action |
|---|---|
| `uci` | Print engine ID, available options, `uciok` |
| `isready` | Print `readyok` |
| `ucinewgame` | Clear TT, reset board |
| `setoption name Threads value N` | Set thread count (1–256) |
| `position startpos [moves ...]` | Set position and apply moves |
| `position fen <FEN> [moves ...]` | Set position from FEN |
| `go depth N` / `go movetime N` / `go nodes N` | Start async search |
| `stop` | Stop search, print bestmove |
| `quit` | Exit |

Search runs on a **separate thread** (`async_thread`) so the main thread can still receive `stop`. Uses `UciThreadGuard` RAII to guarantee clean thread joining on any exit path.

### run_interactive() — JSON Session for Web Backend

Persistent stdin/stdout JSON session:

| Command | Response |
|---|---|
| `newgame [FEN]` | `{"status": "ready"}` |
| `apply <uci_move>` | `{"fen": "...", "status": "ongoing"}` |
| `best [depth]` | `{"best_move": "e2e4", "fen": "...", "status": "ongoing"}` |
| `quit` / `exit` | Exit |

Each response ends with `===READY===` sentinel for the backend to know when output is complete.

---

## 11. Data Flow: How a Move Gets Found

Here's the complete path from user request to best move:

```
1. User runs:  ./game.exe analyze "<FEN>" 12 --threads 4

2. main()  →  parse args into SearchOptions  →  analyze(fen, options)

3. analyze()  →  Engine engine
               →  engine.configure_search(nodes, time, stop_flag)
               →  engine.best_move(board, 12)

4. best_move()  →  Detect hardware threads (GetActiveProcessorCount on Windows)
                →  Initialize TT  →  new_search() (increment age)
                →  If threads > 1: start_workers() (spawn N-1 WorkerThreads)
                →  Iterative deepening: depth 1, 2, ..., 12
                     └── search_depth(board, depth)
                           └── For each root move:
                                 └── board.make_move(move)
                                 └── -search(board, depth-1, -beta, -alpha, 1, pv)
                                       └── TT probe → if hit, return
                                       └── Pruning (null move, futility, etc.)
                                       └── generate_moves(board, moves)
                                             └── find_pins(board)
                                             └── Build attack map
                                             └── PawnMoves, KnightMoves, etc.
                                       └── order_moves(moves, tt_move, ...)
                                       └── For each child move:
                                             └── board.make_move(child)
                                             └── -search(board, depth-2, ...)
                                                   └── ... recursive ...
                                                   └── depth 0: quiescence()
                                                         └── generate_captures()
                                                         └── evaluate_for_side()
                                                               └── evaluate(board)
                                                                     └── eval_tapered()
                                                                     └── eval_pawn_structure()
                                                                     └── eval_mobility_and_king_safety()
                                             └── board.unmake_move()
                                       └── TT store
                                 └── board.unmake_move()
                →  stop_and_collect_workers()  →  sum node counts

5. analyze()  →  Package into AnalysisResult  →  to_json()

6. main()  →  cout << result.to_json()
```

---

## 12. Multi-Threading Architecture

TigerFish uses **Lazy SMP** (Shared Memory Parallelism):

### How It Works

1. The **main thread** runs normal iterative deepening search
2. `thread_count - 1` **worker threads** each run independent `worker_search()` on copies of the board
3. All threads share the **same TT** (the static `TT_GLOBAL` array with atomic entries)
4. Each thread has its own `Engine` instance with independent killer/history tables
5. Threads explore different parts of the tree due to natural search instability
6. TT sharing means one thread's discoveries benefit all others

### Thread Coordination

```cpp
class WorkerPool {
    vector<unique_ptr<WorkerThread>> workers;
    void resize(int count, TranspositionTable* tt);
};

struct WorkerThread {
    Engine engine;              // Independent engine instance
    thread thr;                 // OS thread
    mutex mtx;                  // Synchronization
    condition_variable cv_work; // Signal to start work
    condition_variable cv_done; // Signal work complete
    bool has_work, is_done, terminate;
    Board work_board;
    int work_depth, work_time_ms;
    atomic<bool>* stop_flag;
};
```

Workers are long-lived threads (created once, reused across searches). Work is dispatched via condition variables. Workers are stopped by setting the shared `stop_flag`.

### Windows CPU Detection Bug

`std::thread::hardware_concurrency()` in MinGW returns **8** on Intel Raptor Lake (24 logical cores) due to processor group affinity masks. Fixed by:

```cpp
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetActiveProcessorCount(unsigned short);
// ...
unsigned long win_procs = GetActiveProcessorCount(0xFFFF);  // ALL_PROCESSOR_GROUPS
```

---

## 13. Key Constants & Tuning Parameters

### Material Values (centipawns)

| Piece | Value |
|---|---|
| Pawn | 100 |
| Knight | 320 |
| Bishop | 330 |
| Rook | 500 |
| Queen | 900 |
| King | 20000 |

### Search Parameters

| Parameter | Value | Where | What It Controls |
|---|---|---|---|
| `MATE_SCORE` | 100,000 | search.cpp | Sentinel for checkmate scores |
| `MATE_THRESHOLD` | 90,000 | search.cpp | Score above this = mate found |
| `MAX_SEARCH_PLY` | 128 | search.cpp | Maximum search depth |
| `MAX_QSEARCH_DEPTH` | 8 | search.cpp | Maximum quiescence depth |
| `MAX_HISTORY_SCORE` | 16,384 | search.cpp | History score clamp |
| `TT_SIZE` | 2,097,152 | search.cpp | Transposition table entries (2^21) |
| `DELTA_MARGIN` | 1,000 | search.cpp | Quiescence delta pruning margin |
| Aspiration delta | 25 | search.cpp | Initial aspiration window half-width |
| Null move reduction | 3 + depth/4 | search.cpp | Null move depth reduction |
| Futility margin | 200 × depth | search.cpp | Futility pruning margin per depth |
| Reverse futility margin | 120 × depth | search.cpp | Static null move margin per depth |
| LMR threshold | 5 (PV) / 3 (non-PV) | search.cpp | Moves before LMR kicks in |

### Evaluation Parameters

| Parameter | MG | EG | File |
|---|---|---|---|
| Bishop pair | +40 | +50 | eval.cpp |
| Doubled pawn | -12 | -12 | eval.cpp |
| Isolated pawn | -15 | -15 | eval.cpp |
| Passed pawn (rank 6) | +25 | +55 | eval.cpp |
| Passed pawn (rank 7) | +40 | +90 | eval.cpp |
| Connected passed bonus | +12 | +24 | eval.cpp |
| Pawn shield bonus | +8 | — | eval.cpp |
| Open file near king | -20 | — | eval.cpp |
| Mobility (max) | +24 | +18 | eval.cpp |
| Attacker penalty (8+) | -230 | — | eval.cpp |

---

## 14. How to Modify Each Component

### Add a New Evaluation Feature

1. Add constants in `eval.cpp` (e.g., `ROOK_ON_OPEN_FILE_BONUS`)
2. Create `eval_new_feature(board, mg, eg)` function
3. Call it from `evaluate()` after existing features
4. Run regression tests: `python Test/regression/run_all_tests.py`

### Change Piece Values

1. Update `PIECE_VALUE[12]` in `eval.cpp`
2. Update `piece_values[12]` in `board.cpp` `set_fen()` and `make_move()` (these must match!)
3. Rebuild and test

### Add a New Search Technique

1. Add it inside `search()` between TT probe and move generation
2. Guard with conditions (`!pv_node`, `!in_check`, depth threshold)
3. Make sure it preserves alpha-beta semantics (any pruned subtree must be provably safe)

### Modify Move Generation

1. Edit the relevant function in `MoveGenerator` (e.g., `PawnMoves()`)
2. Run perft depth 5 to verify: should produce exactly **4,865,609** nodes from startpos
3. Run full regression suite

### Add a New CLI Command

1. Add a new `if (command == "newcommand")` block in `main()` (after line 508)
2. Parse arguments, create Board, call engine functions
3. Output JSON to stdout
4. Update usage message

### Add a New UCI Option

1. Add to the `uci` response: `cout << "option name X type spin default D min M max X" << endl;`
2. Handle in `setoption` block: `if (opt_name == "X") { ... }`

---

## 15. Testing & Verification

### Regression Suite

```bash
python Test/regression/run_all_tests.py
```

Runs 73 checks across 8 categories:
- **Perft** (6 tests): Legal move count at various depths
- **Rules** (8 tests): Special move handling (castling, en passant, promotion)
- **Evaluation** (4 tests): Material balance, PST correctness
- **Eval Advanced** (17 tests): Pawn structure, mobility, king safety
- **Rules Advanced** (25 tests): Edge cases in move generation
- **Search Advanced** (19 tests): Tactical positions, mate finding
- **Tactics** (3 tests): Known puzzles the engine must solve
- **UCI/CLI** (2 tests): Handshake and JSON output format

### Manual Verification Commands

```bash
# Perft depth 5 (must = 4,865,609)
# Run via test suite or custom test

# Single-threaded analysis
./game.exe analyze "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 12 --threads 1

# Multi-threaded analysis
./game.exe analyze "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 14 --threads 24

# UCI handshake test
echo "uci" | ./game.exe
# Should output: id name TigerFish ... uciok
```

### Performance Benchmarking

Current measured speeds (Intel Raptor Lake, `-march=native`):

| Configuration | NPS |
|---|---|
| 1 Thread | 3.2–4.1M NPS |
| 8 Threads | 20.8M NPS |
| 12 Threads | 22.4M NPS |
| 16 Threads | 22.9M NPS |
| 24 Threads | 16.3–22.6M NPS |

---

*Last updated: 2026-09-08. Covers engine/main.cpp, engine/search.cpp, engine/eval.cpp, engine/rules.cpp, engine/board.cpp, engine/pst_tables.h, engine/magic_lut.cpp.*
