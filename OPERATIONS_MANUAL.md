# TigerFish Chess Engine — Operations Manual

> **Audience**: Developers who want to modify, optimise, or extend the engine.
> **Build**: `g++ -O3 -std=c++20 -o game.exe engine/main.cpp`
> **Entry point**: `engine/main.cpp` includes `search.cpp` which includes everything else.

---

## Table of Contents
1. [Architecture Overview](#1-architecture-overview)
2. [Include Hierarchy](#2-include-hierarchy)
3. [Data Conventions](#3-data-conventions)
4. [File: magic_lut.cpp](#4-file-magic_lutcpp)
5. [File: eval_lut.cpp](#5-file-eval_lutcpp)
6. [File: board.cpp & Zobrist Hashing](#6-file-boardcpp--zobrist-hashing)
7. [File: rules.cpp & MVV-LVA Move Ordering](#7-file-rulescpp--mvv-lva-move-ordering)
8. [File: search.cpp & Transposition Table](#8-file-searchcpp--transposition-table)
9. [File: main.cpp & CLI Commands](#9-file-maincpp--cli-commands)
10. [File: lichess_bot.py & lichess_multibot.py — Lichess Bot Bridge Architecture](#10-file-lichess_botpy--lichess_multibotpy--lichess-bot-bridge-architecture)
11. [Performance Milestones & Optimisation Guide](#11-performance-milestones--optimisation-guide)

---

## 1. Architecture Overview

```
Layer 5: Entry Point & CLI
         main.cpp
              |
Layer 4: Search, TT & Evaluation
         search.cpp
              |
Layer 3: Move Generation & MVV-LVA
         rules.cpp
              |
Layer 2: Board State, Move Encoding & Zobrist
         board.cpp  <--- magic_lut.cpp
              |
Layer 1: Lookup Tables
         eval_lut.cpp   magic_lut.cpp
```

Each layer depends only on layers below it. There are zero circular includes and zero forward declarations.

---

## 2. Include Hierarchy

```
main.cpp
  +-- search.cpp
        +-- rules.cpp
        |     +-- board.cpp
        |           +-- magic_lut.cpp
        +-- eval_lut.cpp
```

Every file is included exactly once in the cascade. `#pragma once` is not used and not needed.

---

## 3. Data Conventions

### Square Indexing
Squares are numbered 0 (a1) to 63 (h8):
```
a1=0   b1=1  ... h1=7
a2=8   b2=9  ... h2=15
...
a8=56  b8=57 ... h8=63
```
`sq = rank * 8 + file` where rank 0 = rank 1 and file 0 = a-file.

### Piece Enum (board.cpp)
```cpp
enum Piece { P=0,N=1,B=2,R=3,Q=4,K=5, p=6,n=7,b=8,r=9,q=10,k=11 };
```
- Indices 0-5: White pieces. Indices 6-11: Black pieces.
- Sentinel `0xF` (decimal 15) means "no piece" on a square.
- `p < 6` is true for white pieces.

### Bitboards
- `Board::bitboards[12]` — one 64-bit integer per piece type; bit N set means piece exists on square N.
- `Board::occupancy[3]` — `[0]` = all white squares, `[1]` = all black squares, `[2]` = all occupied squares.

### 32-bit Move Encoding
```
Bits  0-5  : from square  (0-63)
Bits  6-11 : to square    (0-63)
Bits 12-15 : moving piece (Piece enum 0-11)
Bits 16-19 : captured piece (0xF = no capture)
Bits 20-23 : promotion piece (0xF = no promotion)
Bits 24-27 : castling rights BEFORE this move (4-bit snapshot, used by unmake)
Bit  28    : is_castle flag
Bit  29    : is_en_passant flag
Bit  30    : is_double_push flag
Bit  31    : side that made the move (WHITE=0, BLACK=1)
```

### En-Passant Convention
`Board::en_passant` is a `uint8_t`. Value `255` means no active en-passant target. Values `0-63` are the square a pawn can capture into (the empty square behind the double-pushed pawn).

---

## 4. File: `magic_lut.cpp`

**Layer 1 - Precomputed attack lookups for sliding pieces.**

### Global Tables

| Symbol | Type | Size | Purpose |
|--------|------|------|---------|
| `ray_table[8][64]` | `uint64_t` | 512 entries | Ray bitboard for each direction from each square |
| `ROOK_MAGICS[64]` | `const uint64_t` | 64 entries | Hardcoded magic numbers for rook hash |
| `BISHOP_MAGICS[64]` | `const uint64_t` | 64 entries | Hardcoded magic numbers for bishop hash |
| `ROOK_MASK[64]` | `uint64_t` | 64 entries | Blocker mask for rook on each square |
| `BISHOP_MASK[64]` | `uint64_t` | 64 entries | Blocker mask for bishop on each square |
| `ROOK_SHIFT[64]` | `int` | 64 entries | Right-shift = `64 - popcount(mask)` |
| `BISHOP_SHIFT[64]` | `int` | 64 entries | Right-shift for bishop |
| `ROOK_ATTACKS[64][4096]` | `uint64_t` | 262,144 entries | Precomputed rook attacks indexed by magic hash |
| `BISHOP_ATTACKS[64][512]` | `uint64_t` | 32,768 entries | Precomputed bishop attacks indexed by magic hash |

### Functions

#### `void init_rays()`
- **Called by**: `main()` explicitly, as the very first call before anything else.
- Walks all 8 directions from every square and fills `ray_table`.

#### `void init_magics()`
- **Called by**: `main()` explicitly, immediately after `init_rays()`.
- Generates blocker masks, magic shift lookup tables, and attack bitboard precomputations.

---

## 5. File: `eval_lut.cpp`

**Layer 1 - Static evaluation lookup tables.**

All tables are `inline constexpr int[64]`. Indexed by square number (0=a1, 63=h8). Black pieces mirror the table using `Engine::mirror(sq)`.

### Tables

| Symbol | Purpose |
|--------|---------|
| `PIECE_VALUE[12]` | Centipawn base values: P=100, N=320, B=330, R=500, Q=900, K=20000 |
| `PAWN_PST[64]` | Rewards central and advanced pawns |
| `KNIGHT_PST[64]` | Rewards centralised knights, penalises rim knights |
| `BISHOP_PST[64]` | Rewards long diagonals and centre control |
| `ROOK_PST[64]` | Rewards 7th rank |
| `QUEEN_PST[64]` | Slight centralisation reward |
| `KING_PST[64]` | Penalises centralised king (middle-game king safety) |

---

## 6. File: `board.cpp` & Zobrist Hashing

**Layer 2 - Bitboard structures, Board class, and incremental Zobrist Hashing.**

### Zobrist Hash Constants & Tables
Declared globally and initialized at startup via `init_zobrist()` using a deterministic `SplitMix64` PRNG (seeded at `1070372ULL`):

| Symbol | Size | Purpose |
|--------|------|---------|
| `ZOBRIST_PIECE[12][64]` | `uint64_t[12][64]` | Unique 64-bit random keys for every piece on every square |
| `ZOBRIST_SIDE` | `uint64_t` | Key XOR'd when side to move is Black |
| `ZOBRIST_CASTLING[16]` | `uint64_t[16]` | Key indexed by 4-bit castling rights mask |
| `ZOBRIST_EP[65]` | `uint64_t[65]` | Key indexed by EP file (0..7) or 64 if no EP |

### `class Board` Zobrist State
- `uint64_t zobrist_hash`: Holds the exact 64-bit Zobrist key of the active board state.
- `vector<uint64_t> zobrist_history`: Saves `zobrist_hash` across moves for zero-drift stack restoration during `unmake_move()`.

### Incremental XOR Updates in `make_move()` / `unmake_move()`
- **Piece moves & captures**: `zobrist_hash ^= ZOBRIST_PIECE[p][sq]`
- **Side to move**: `zobrist_hash ^= ZOBRIST_SIDE`
- **Castling rights**: `zobrist_hash ^= ZOBRIST_CASTLING[old_rights] ^ ZOBRIST_CASTLING[new_rights]`
- **En-passant file**: `zobrist_hash ^= ZOBRIST_EP[old_ep_file] ^ ZOBRIST_EP[new_ep_file]`
- Tested with 100-move random walk tests: **Zero Hash Drift verified**.

---

## 7. File: `rules.cpp` & MVV-LVA Move Ordering

**Layer 3 - Move Generator and MVV-LVA Capture Ordering.**

### `struct MoveList`
```cpp
struct MoveList {
    uint32_t move_list[218];
    int scores[218];
    int count = 0;
    
    void clear();
    void push(uint32_t m, int score = 0);
    int size() const;
    void sort_mvv_lva(); // Ultra-fast insertion sort on moves array
};
```

### MVV-LVA Scoring Formula
At the end of `MoveGenerator::generate_moves(board, moves)`:
$$\text{MVV-LVA Score} = (\text{Victim Value} \times 10) - \text{Attacker Value} + 10000$$

* $\text{Pawn} \times \text{Queen} \implies \mathbf{18,900}$ (Highest priority)
* $\text{Knight} \times \text{Queen} \implies \mathbf{18,700}$
* $\text{Rook} \times \text{Rook} \implies \mathbf{14,500}$
* Quiet non-captures $\implies \mathbf{0}$

Calls `moves.sort_mvv_lva()` so all legal capture moves are presented to search in descending order of tactical value before quiet moves.

---

## 8. File: `search.cpp` & Transposition Table

**Layer 4 - Minimax with Alpha-Beta Pruning, Transposition Table (TT), and Uniform Evaluation Naming.**

### Standardized Variable Names
Across all of `search.cpp`, variables follow a strictly standardized naming convention:
- **`eval`**: Computed evaluation score.
- **`max_eval`**: Best evaluation score in White (maximizing) branches.
- **`min_eval`**: Best evaluation score in Black (minimizing) branches.
- **`tt_eval`**: Retrieved evaluation score from Transposition Table probe.

### Transposition Table Architecture

#### `TTEntry` Struct (Fixed 16 Bytes)
```cpp
struct TTEntry {
    uint64_t key = 0;       // 8 bytes: Full 64-bit Zobrist key
    uint32_t best_move = 0; // 4 bytes: Best move found
    int16_t  eval = 0;      // 2 bytes: Evaluation score
    int8_t   depth = -1;    // 1 byte : Search depth (-1 = empty)
    TTFlag   flag = TT_EXACT;// 1 byte : Bound flag (TT_EXACT, TT_ALPHA, TT_BETA)
};
```

#### `TranspositionTable` Class (16 MB / 1,048,576 Entries)
- **Indexing**: Bitwise mask `size_t index = key & 0xFFFFF` ($O(1)$ RAM slot lookup).
- **`lookup(key, depth, alpha, beta, eval, tt_move)`**: Probes slot. Extracts `tt_move` for move ordering. Returns `true` if stored entry bound meets cutoff criteria (`TT_EXACT`, `TT_ALPHA` $\le \alpha$, `TT_BETA` $\ge \beta$).
- **`store(key, depth, eval, flag, best_move)`**: Replaces entry if new depth is greater or slot contains a different position.

#### Search Pipeline in `minimax()`:
1. **TT Probe**: Checks `tt.lookup()`. Returns `tt_eval` instantly on hit.
2. **Move Generation**: Calls `mg.generate_moves(board, moves)` (already MVV-LVA sorted).
3. **TT Move Boost**: Swaps `tt_move` to `moves.move_list[0]` (Index 0).
4. **Alpha-Beta Loop**: Iterates through moves, makes move, recurses `depth - 1`, unmakes move.
5. **TT Store**: Classifies bound flag (`TT_EXACT`, `TT_ALPHA`, `TT_BETA`) and saves entry to TT.

---

## 9. File: `main.cpp` & CLI Commands

**Layer 5 - Entry Point and CLI Server.**

- `game.exe interactive`: Starts interactive server for Python Lichess Bot Bridge (`lichess_bot.py`).
- `game.exe move <FEN> <UCI_MOVE> [depth]`: Evaluates and returns best move response in JSON.
- `game.exe perft <FEN> [depth]`: Perft move generation verification tool.

---

## 10. Performance Milestones & Optimisation Guide

### Benchmarked Performance Speedup (v1 Release)

| Feature Stage | Depth 6 Search Time | Nodes Searched | Total Speedup |
| :--- | :--- | :--- | :--- |
| **Baseline (Plain Minimax)** | $46,600\text{ ms}$ ($46.6\text{s}$) | $173,000,000\text{ nodes}$ | $1\times$ |
| **Zobrist Transposition Table** | $350\text{ ms}$ | $1,203,335\text{ nodes}$ | $133\times$ FASTER |
| **TT + MVV-LVA Move Ordering** | **$67\text{ ms}$** | **$542,396\text{ nodes}$** | **$695\times$ FASTER** |

### Next Optimization Targets (v2 Roadmap)
1. **Quiescence Search (Q-Search)**: Continue searching all captures at `depth == 0` to eliminate the Horizon Effect.
2. **Iterative Deepening**: Iteratively search depth 1, 2, 3... to target depth, maximizing TT hit rates.
3. **Killer Move Heuristic**: Store 2 non-capture moves that caused beta cutoffs per ply.


