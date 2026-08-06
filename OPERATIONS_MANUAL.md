# TigerFish Chess Engine & Lichess Bridge — Operations Manual

> **Audience**: Developers maintaining, optimizing, or extending the engine or Lichess bot bridge.  
> **Build**: `g++ -O2 -std=c++20 -o game.exe engine/main.cpp`  
> **Entry Point**: `engine/main.cpp` includes `search.cpp` which cascades down to all rules and lookup tables.

---

## Table of Contents
1. [Architecture & Layer Hierarchy](#1-architecture--layer-hierarchy)
2. [Include Cascade & Dependencies](#2-include-cascade--dependencies)
3. [Data Representations & Encoding Conventions](#3-data-representations--encoding-conventions)
4. [File: magic_lut.cpp — Sliding Piece Magic Hash Tables](#4-file-magic_lutcpp--sliding-piece-magic-hash-tables)
5. [File: eval_lut.cpp — Static Evaluation Lookup Tables](#5-file-eval_lutcpp--static-evaluation-lookup-tables)
6. [File: board.cpp — Board State, Zobrist Hashing, Make/Unmake](#6-file-boardcpp--board-state-zobrist-hashing-makeunmake)
7. [File: rules.cpp — Legal Move Generator & Rule Verification](#7-file-rulescpp--legal-move-generator--rule-verification)
8. [File: search.cpp — Minimax, Transposition Table (32 MB), & Evaluation](#8-file-searchcpp--minimax-transposition-table-32-mb--evaluation)
9. [File: main.cpp — CLI Engine Commands & Interactive Protocol](#9-file-maincpp--cli-engine-commands--interactive-protocol)
10. [Lichess Bot Bridge (`lichess_multibot.py` & `lichess_bot.py`)](#10-lichess-bot-bridge-lichess_multibotpy--lichess_botpy)
11. [Performance Profile, Assumptions & Future Optimisation Roadmap](#11-performance-profile-assumptions--future-optimisation-roadmap)

---

## 1. Architecture & Layer Hierarchy

```
Layer 5: CLI & Interactive Protocol
         engine/main.cpp
               |
Layer 4: Search, Minimax & Transposition Table (32 MB)
         engine/search.cpp
               |
Layer 3: Legal Move Generator & Rule Verification
         engine/rules.cpp
               |
Layer 2: Board State, Zobrist Hash & Move Encoding
         engine/board.cpp  <--- engine/magic_lut.cpp
               |
Layer 1: Precomputed Magic & Evaluation Lookup Tables
         engine/eval_lut.cpp   engine/magic_lut.cpp
```

### Architectural Principles
- **Strict One-Way Dependency**: Every file depends strictly on layers below it. No circular includes, no forward declarations needed.
- **Zero Heavy External Libraries**: Standard C++20 standard library only (`<cstdint>`, `<vector>`, `<string>`, `<chrono>`).
- **Single Process / Persistent IPC Mode**: Supports both single-shot CLI execution and persistent stdin/stdout IPC streams.

---

## 2. Include Cascade & Dependencies

```
engine/main.cpp
  +-- engine/search.cpp
        +-- engine/rules.cpp
        |     +-- engine/board.cpp
        |           +-- engine/magic_lut.cpp
        +-- engine/eval_lut.cpp
```

### Implications
- Compiling `engine/main.cpp` compiles the entire engine into a single output executable (`game.exe`).
- Header guards (`#pragma once`) are not used because every module is included exactly once in this linear chain.

---

## 3. Data Representations & Encoding Conventions

### Square Indexing (0 to 63)
Squares are indexed 0 (a1) to 63 (h8):
```
a1=0   b1=1   c1=2   d1=3   e1=4   f1=5   g1=6   h1=7
a2=8   b2=9   ...
...
a8=56  b8=57  c8=58  d8=59  e8=60  f8=61  g8=62  h8=63
```
Formula: `sq = rank * 8 + file` (rank 0 = rank 1, file 0 = a-file).

### Piece Encoding (`enum Piece`)
```cpp
enum Piece { P=0, N=1, B=2, R=3, Q=4, K=5, p=6, n=7, b=8, r=9, q=10, k=11 };
```
- **Indices 0–5**: White pieces (`P`, `N`, `B`, `R`, `Q`, `K`).
- **Indices 6–11**: Black pieces (`p`, `n`, `b`, `r`, `q`, `k`).
- **Sentinel `0xF`** (decimal 15): Indicates an empty square.
- **Side Test**: `piece < 6` evaluates to `true` for White, `false` for Black.

### 32-bit Move Bitfield Format (`uint32_t`)
Moves are packed into a single 32-bit unsigned integer:
```
Bits  0–5  : from_square (0–63)
Bits  6–11 : to_square (0–63)
Bits 12–15 : moving_piece (Piece enum 0–11)
Bits 16–19 : captured_piece (0xF = no capture)
Bits 20–23 : promotion_piece (0xF = no promotion)
Bits 24–27 : castling_rights BEFORE this move (4-bit snapshot for unmake)
Bit  28    : is_castle flag (1 = castling move)
Bit  29    : is_en_passant flag (1 = en-passant capture)
Bit  30    : is_double_push flag (1 = 2-square pawn jump)
Bit  31    : side_to_move (0 = White, 1 = Black)
```

### En-Passant State (`Board::en_passant`)
- Type: `uint8_t`.
- **Value `255`**: No active en-passant target square.
- **Value `0–63`**: The empty square behind a double-pushed pawn where an enemy pawn can capture.

---

## 4. File: `magic_lut.cpp` — Sliding Piece Magic Hash Tables

**Layer 1 — Precomputed attack bitboards for Rooks and Bishops.**

### Key Structures & Global Lookup Arrays
- `ROOK_ATTACKS[64][4096]` (`uint64_t`): Precomputed rook attack masks indexed by `[square][magic_hash]`. Size ~2 MB.
- `BISHOP_ATTACKS[64][512]` (`uint64_t`): Precomputed bishop attack masks indexed by `[square][magic_hash]`. Size ~256 KB.
- `ROOK_MAGICS[64]` / `BISHOP_MAGICS[64]`: Hardcoded 64-bit magic multiplication factors.
- `ROOK_MASK[64]` / `BISHOP_MASK[64]`: Bitmasks of relevant blocker squares (board edges excluded).
- `ROOK_SHIFT[64]` / `BISHOP_SHIFT[64]`: Right-shift constants equal to `64 - popcount(mask)`.

### Function Workings & Implications
- `init_rays()`: Walks all 8 compass directions (`NORTH`, `SOUTH`, `EAST`, `WEST`, `NE`, `SE`, `NW`, `SW`) from every square. Must be called once at startup.
- `init_magics()`: Iterates through all blocker subsets via carry-ripple subtraction `(sub - 1) & mask`, computes attack bitboards, hashes them with the magic constant, and populates `ROOK_ATTACKS` / `BISHOP_ATTACKS`.
- **Assumption**: Magic factors produce zero collisions across all valid occupancy subsets for each square.

---

## 5. File: `eval_lut.cpp` — Static Evaluation Lookup Tables

**Layer 1 — Piece-Square Tables (PST) and Piece Centipawn Values.**

### Table Arrays
- `PIECE_VALUE[12]`: Base centipawn material values:
  - Pawn: 100
  - Knight: 320
  - Bishop: 330
  - Rook: 500
  - Queen: 900
  - King: 20000
- `PAWN_PST[64]`, `KNIGHT_PST[64]`, `BISHOP_PST[64]`, `ROOK_PST[64]`, `QUEEN_PST[64]`, `KING_PST[64]`: Centipawn positional bonuses indexed by square 0–63 (from White's perspective).

### Implication
- Black piece scores mirror White's square using `Engine::mirror(sq) = (7 - rank) * 8 + file`.

---

## 6. File: `board.cpp` — Board State, Zobrist Hashing, Make/Unmake

**Layer 2 — State container and incremental move application.**

### Low-Level Bit Manipulation Primitives
- **`lsb_index(uint64_t mask)`**: Uses `std::countr_zero(mask)` (compiles to x86-64 single-cycle `BSF` / `TZCNT` instructions). Returns index of the least significant set bit (0–63).
- **`msb_index(uint64_t mask)`**: Uses `63 - std::countl_zero(mask)` (`BSR` / `LZCNT` on x86-64). Returns index of the most significant set bit.
- **`pop_lsb(uint64_t& mask)`**: Extracts `lsb_index(mask)` and clears that bit in-place using Kernighan's trick `mask &= mask - 1`. Used for fast $O(K)$ iteration over set bits:
  ```cpp
  while (mask) {
      int sq = pop_lsb(mask);
      // Process square sq
  }
  ```

### Bitwise File Guard Masks
To prevent bit shifts from wrapping across opposite board edges (e.g. `a`-file shifting left to `h`-file):
- `NOT_A_FILE` (`0xFEFEFEFEFEFEFEFEULL`): Clears File A.
- `NOT_H_FILE` (`0x7F7F7F7F7F7F7F7FULL`): Clears File H.
- `NOT_AB_FILE` (`0xFCFCFCFCFCFCFCFCULL`): Clears Files A and B (for knight -2 shifts).
- `NOT_GH_FILE` (`0x3F3F3F3F3F3F3F3FULL`): Clears Files G and H (for knight +2 shifts).

### `Board` Class Workings
- `bitboards[12]` (`uint64_t`): 12 piece bitboards (P, N, B, R, Q, K, p, n, b, r, q, k).
- `occupancy[3]` (`uint64_t`): `[0]` = White occupied, `[1]` = Black occupied, `[2]` = All occupied (`[0] | [1]`).
- `piece_on[64]` (`Piece`): Mailbox array providing O(1) piece lookup for any square (sentinel `0xF` = empty).
- `zobrist_hash` (`uint64_t`): 64-bit Zobrist key representing the exact position.
- **`SplitMix64` Deterministic PRNG**: Seeding `1070372ULL` initializes `ZOBRIST_PIECE[12][64]`, `ZOBRIST_SIDE`, `ZOBRIST_CASTLING[16]`, and `ZOBRIST_EP[65]` deterministically on startup.
- `make_move(uint32_t move)`:
  1. Saves current castling rights, en-passant square, halfmove clock, and Zobrist key into history vectors (`move_history`, `ep_history`, `halfmove_history`, `zobrist_history`).
  2. Updates piece bitboards, mailbox `piece_on`, and Zobrist key incrementally ($O(1)$ delta XOR operations).
  3. Handles promotions, captures, en-passant pawn removal, and rook movement for castling.
  4. Swaps `side_to_move` and updates `occupancy[]`.
- `unmake_move()`:
  1. Pops the last move from `move_history`, `ep_history`, `halfmove_history`, `zobrist_history`.
  2. Reverses piece placements and restores castling rights, en-passant square, and side to move with zero Zobrist key drift.

---

## 7. File: `rules.cpp` — Legal Move Generator & Rule Verification

**Layer 3 — Legal move generation, pin detection, check verification.**

### Key Workings & Tactical Generator States
- `MoveList`: Stack-allocated container storing up to 218 moves (`uint32_t move_list[218]`, `int scores[218]`).
- `find_pins(Board& board)`: Scans rays outward from the friendly king to detect pinned friendly pieces and store their allowable ray movement masks (`PinInfo`).
- `ep_exposes_king(Board& board, int from_sq, int ep_sq)`: Verifies rare horizontal rank pin exposures when an en-passant capture removes two pawns from the same rank simultaneously.
- **Legal Move Generation Flow** (`generate_moves`):
  1. **Double Check ($\ge 2$ checkers)**: Only King moves are legal.
  2. **Single Check ($1$ checker)**: Non-king moves must either capture the checker or block the check ray (`legal_mask`). King moves must move to non-attacked squares.
  3. **Normal State ($0$ checkers)**: All legal moves are generated. `PinInfo` masks prevent pinned pieces from leaving their pin ray.
- **MVV-LVA (Most Valuable Victim - Least Valuable Attacker) Scoring**:
  $$\text{MVV-LVA Score} = (\text{Victim Value} \times 10) - \text{Attacker Value} + 10000$$
  Calls `moves.sort_mvv_lva()` ($O(N)$ insertion sort) so all legal capture moves are presented to search in descending order of tactical value before quiet moves.
- `get_game_result(Board& board)`:
  - Evaluates terminal conditions: `GAME_CHECKMATE`, `GAME_STALEMATE`, `GAME_FIFTY_MOVE_DRAW` (100 halfmoves), `GAME_SEVENTY_FIVE_MOVE_DRAW` (150 halfmoves), `GAME_INSUFFICIENT_MATERIAL`.

---

## 8. File: `search.cpp` — Minimax, Transposition Table (32 MB), & Evaluation

**Layer 4 — Search engine, evaluation function, and Transposition Table.**

### 32 MB Transposition Table (`TranspositionTable`)
- **Table Size**: `2,097,152` entries ($2^{21}$ entries $\times$ 16 bytes = **32 MB**).
- **Masking**: `size_t index = key & (TABLE_SIZE - 1)` (instant O(1) bitwise AND index).
- `TTEntry` Struct:
  ```cpp
  struct TTEntry {
      uint64_t key = 0;        // 8 bytes: 64-bit Zobrist key
      uint32_t best_move = 0;  // 4 bytes: Best move found at node
      int16_t  eval = 0;       // 2 bytes: Evaluation score
      int8_t   depth = -1;     // 1 byte : Search depth (-1 = empty)
      TTFlag   flag = TT_EXACT;// 1 byte : Bound flag (TT_EXACT=0, TT_ALPHA=1, TT_BETA=2)
  };
  ```
- **Bound Classifications**:
  - `TT_EXACT` (0): Evaluation score was exact (trapped strictly between $\alpha$ and $\beta$).
  - `TT_ALPHA` (1): Upper bound score ($\text{eval} \le \alpha$). Branch failed low.
  - `TT_BETA` (2): Lower bound score ($\text{eval} \ge \beta$). Branch failed high (caused beta cutoff).
- **Replacement Policy**: Depth-preferred replacement (`if (entry.key != key || depth >= entry.depth)`).

### Search Workings & TT Move Ordering
1. **TT Lookup**: On entering `minimax()`, `tt.lookup()` checks if the current Zobrist key exists in the table. If stored depth $\ge$ search depth and bound flags match (EXACT, ALPHA upper bound, BETA lower bound), the cached evaluation is returned immediately.
2. **TT Move Ordering**: If a `tt_move` exists for the position, `minimax()` swaps it to position 0 in `MoveList` so the best move is searched first, maximizing alpha-beta pruning cutoffs.
3. **Alpha-Beta Pruning**: Alpha and Beta bounds prune branches as soon as a refutation is found.
4. **Mate & Draw Evaluations**: Checkmate evaluation scores $\pm (20000 + \text{depth})$ prioritizing faster mates. Draws evaluate to 0 centipawns.
5. **Root Candidate Selection (`best_move`)**: Evaluates root moves at search depth, filters moves within a 50-centipawn threshold of the maximum score, and randomly selects among the top candidates for opening variety.

---

## 9. File: `main.cpp` — CLI Engine Commands & Interactive Protocol

**Layer 5 — Executable entry point and IPC server mode.**

### Available CLI Commands

#### 1. `game.exe interactive` (Persistent Session — Used by Bots)
Starts a persistent stdin/stdout session. One `game.exe` process runs for the entire duration of a game, preserving the **32 MB Transposition Table** across all turns.
- **Protocol Commands**:
  - `newgame [fen]` : Initializes board state (TT is **retained** to accumulate search knowledge). Returns `{"status": "ready"}`.
  - `apply <uci_move>` : Applies opponent move to the internal board without searching. Returns `{"fen": "...", "status": "ongoing"}`.
  - `best <depth>` : Searches current board to given depth, applies the best move internally, and returns `{"best_move": "uci", "fen": "...", "status": "ongoing"}`.
  - `quit` / `exit` : Terminates the process cleanly.
- **Sentinel**: Every JSON response is followed by `===READY===` on a new line.

#### 2. `game.exe apply <fen> <uci_move>`
Single-shot command to validate and apply a UCI move to a FEN, returning the resulting FEN without performing search.

#### 3. `game.exe best <fen> [depth]`
Single-shot command to search a position FEN at target depth (default 7) and return JSON `{"best_move": "..."}`.

#### 4. `game.exe moves <fen>`
Generates all legal moves for a FEN and prints full JSON state.

#### 5. `game.exe play [depth]`
Interactive terminal chess mode for human vs engine play.

---

## 10. Lichess Bot Bridge (`lichess_multibot.py` & `lichess_bot.py`)

### Architecture
- **`lichess_multibot.py`**: Multi-game bot farmer (handles up to 10 concurrent parallel games on Lichess).
- **`lichess_bot.py`**: Single-game Lichess bot bridge.

### Workings & Features
- **Persistent Engine Process (`GameEngineProcess`)**: Each active Lichess game spawns its own `game.exe interactive` process. The 32 MB TT inside `game.exe` remains active for the full match.
- **Non-blocking Event Loop (`asyncio` + `aiohttp`)**: Streams Lichess events and game states asynchronously without blocking incoming challenges or other active games.
- **Dynamic Account Info**: Queries `/api/account` on startup to detect the bot's username dynamically.
- **Auto-Seeker & Rejection Memory**:
  - Automatically seeks matches against online bots across 11 time controls (bullet, blitz, rapid).
  - Maintains `bot_blacklist` for bots rejecting bot-vs-bot games.
  - Maintains `bot_tc_rejections` to avoid re-offering rejected time controls to specific bots.
- **Graceful Shutdown**: Pressing `q` + Enter or sending `Ctrl+C` halts new challenges and allows all active games to finish cleanly before exiting.

---

## 11. Performance Profile, Assumptions & Future Optimisation Roadmap

### Current Performance Characteristics
- **Move Generation**: Perft speed ~20–35 million nodes/sec (Mnps) on multi-threaded perft harness.
- **Search Depth**: Comfortably searches Depth 7–8 in 1–3 seconds per move during blitz/rapid games.
- **Memory Footprint**: 
  - Engine binary: ~150 KB.
  - RAM per active game process: ~34 MB (32 MB TT + stack/buffers).

### Future Optimisation Roadmap
1. **Quiescence Search**: Search captures at depth 0 until position resolves to eliminate horizon effects.
2. **Iterative Deepening**: Search depth 1, 2, ..., N with time management allocation.
3. **Incremental Evaluation**: Update centipawn material/PST score delta-style in `make_move`/`unmake_move` instead of 64-square full scans at leaf nodes.
4. **History Heuristic & Killer Moves**: Track non-capture moves causing beta cutoffs to further boost move ordering.
