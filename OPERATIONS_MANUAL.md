# TigerFish Chess Engine — Operations Manual

> **Audience**: Developers who want to modify, optimise, or extend the engine.
> **Build**: `g++ -O2 -std=c++20 -o game.exe engine/main.cpp`
> **Entry point**: `engine/main.cpp` includes `search.cpp` which includes everything else.

---

## Table of Contents
1. [Architecture Overview](#1-architecture-overview)
2. [Include Hierarchy](#2-include-hierarchy)
3. [Data Conventions](#3-data-conventions)
4. [File: magic_lut.cpp](#4-file-magic_lutcpp)
5. [File: eval_lut.cpp](#5-file-eval_lutcpp)
6. [File: board.cpp](#6-file-boardcpp)
7. [File: rules.cpp](#7-file-rulescpp)
8. [File: search.cpp](#8-file-searchcpp)
9. [File: main.cpp](#9-file-maincpp)
10. [Performance Bottlenecks and Optimisation Guide](#10-performance-bottlenecks-and-optimisation-guide)

---

## 1. Architecture Overview

```
Layer 5: Entry Point & CLI
         main.cpp
              |
Layer 4: Search & Evaluation
         search.cpp
              |
Layer 3: Move Generation & Rules
         rules.cpp
              |
Layer 2: Board State & Move Encoding
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
| `ROOK_MAGICS[64]` | `const uint64_t` | 64 entries | Hardcoded magic numbers for rook hash (do not change) |
| `BISHOP_MAGICS[64]` | `const uint64_t` | 64 entries | Hardcoded magic numbers for bishop hash (do not change) |
| `ROOK_MASK[64]` | `uint64_t` | 64 entries | Blocker mask for rook on each square (edges excluded) |
| `BISHOP_MASK[64]` | `uint64_t` | 64 entries | Blocker mask for bishop on each square (edges excluded) |
| `ROOK_SHIFT[64]` | `int` | 64 entries | Right-shift = `64 - popcount(mask)` |
| `BISHOP_SHIFT[64]` | `int` | 64 entries | Right-shift for bishop |
| `ROOK_ATTACKS[64][4096]` | `uint64_t` | 262,144 entries | Precomputed rook attacks indexed by magic hash |
| `BISHOP_ATTACKS[64][512]` | `uint64_t` | 32,768 entries | Precomputed bishop attacks indexed by magic hash |

### Functions

#### `void init_rays()`
- **Called by**: `main()` explicitly, as the very first call before anything else.
- **What it does**: Walks all 8 directions from every square and fills `ray_table`.
- **Direction indices**: NORTH=0, SOUTH=1, EAST=2, WEST=3, NE=4, SE=5, NW=6, SW=7.
- **Guards**: Uses a `static bool initialized` so it runs exactly once even if called again.
- **Optimisation note**: Cannot be parallelised; runs in microseconds at startup.

#### `static uint64_t compute_rook_attacks(int sq, uint64_t blockers)`
- **Called by**: `init_magics()` only - never called at search time.
- **What it does**: Slides north/south/east/west from `sq`, stops AT the first set bit in `blockers` (inclusive).
- **Performance**: Slow O(N) loop - acceptable only during one-time initialisation.

#### `static uint64_t compute_bishop_attacks(int sq, uint64_t blockers)`
- Same as `compute_rook_attacks` but for NE/NW/SE/SW diagonals.

#### `static uint64_t next_subset(uint64_t sub, uint64_t mask)`
- **Carry-ripple trick**: `(sub - 1) & mask` steps downward through all 2^N subsets of the bits set in `mask`.
- Returns `0` after the last subset, terminating the `do { } while(rsub != 0)` loop.

#### `void init_magics()`
- **Called by**: `main()` explicitly, immediately after `init_rays()` - both are called once at program start before any `Board` is constructed.
- **What it does**: For each of 64 squares:
  1. Computes the blocker mask (inner squares only, edges excluded).
  2. Sets `ROOK_SHIFT[sq] = 64 - popcount(mask)`.
  3. Iterates over all 2^N blocker subsets using `next_subset`.
  4. Hashes each subset: `idx = (subset * MAGIC[sq]) >> SHIFT[sq]`.
  5. Stores `compute_rook_attacks(sq, subset)` at `ROOK_ATTACKS[sq][idx]`.
  6. Repeats identically for bishops.
- **Guards**: `static bool initialized` - runs exactly once.
- **Memory**: ROOK_ATTACKS uses ~2MB, BISHOP_ATTACKS uses ~256KB.

### How to Optimise magic_lut.cpp
- The magic numbers are hardcoded perfect-hash constants found offline. Do not change them.
- For smaller tables: use "fancy magic bitboards" (variable-size per-square tables).
- For fastest lookup on modern x86 (Intel Haswell+, AMD Zen 3+): replace the multiply-shift hash with `_pext_u64(occ, MASK[sq])` (BMI2 instruction) - eliminates the magic multiply entirely.

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

### How to Optimise eval_lut.cpp
- Add a separate `KING_ENDGAME_PST` - centralised king is strong in endgames.
- Add mobility bonus: count legal moves per piece and add `mobility_weight * count`.
- Add passed pawn detection: if no enemy pawns are in front of a pawn on its file or adjacent files, add a large bonus.
- Add king safety: count pawn shield squares and attackers near the king.

---

## 6. File: `board.cpp`

**Layer 2 and 3 - Enums, bitboard utilities, move encoding, and Board class.**

### Enums

| Enum | Values | Purpose |
|------|--------|---------|
| `Direction` | NORTH=0..SW=7 | Index into `ray_table` |
| `Piece` | P=0..k=11 | Piece identity and index into `bitboards[]` |
| `Color` | WHITE=0, BLACK=1 | Side and index into `occupancy[]` |
| `CastleRights` | bit flags 1,2,4,8 | Castling availability bitmask (K=1, Q=2, k=4, q=8) |
| `GameResult` | GAME_ONGOING..GAME_INSUFFICIENT_MATERIAL | Terminal state codes |

### File Mask Constants
```
NOT_A_FILE   0xFEFE... - all squares except A-file; prevents wrap-around shifts
NOT_H_FILE   0x7F7F... - all squares except H-file
NOT_AB_FILE  0xFCFC... - knight left-2 shift guard
NOT_GH_FILE  0x3F3F... - knight right-2 shift guard
RANK_1, RANK_2, RANK_7, RANK_8 - horizontal rank masks
```

### Bitboard Utility Functions

#### `int lsb_index(uint64_t mask)`
- Returns index of the lowest set bit using `std::countr_zero` (compiles to single BSF instruction on x86).
- Usage: Get the square of the first piece in a bitboard.

#### `int msb_index(uint64_t mask)`
- Returns index of the highest set bit using `63 - std::countl_zero`.
- Usage: Needed for ray intersection in SOUTH/WEST/SE/SW directions.

#### `int pop_lsb(uint64_t& mask)`
- Returns `lsb_index(mask)` then clears that bit with `mask &= mask - 1`.
- Standard "iterate over all set bits" pattern:
  ```cpp
  while(mask) { int sq = pop_lsb(mask); /* process sq */ }
  ```

#### `void print_bitboard(uint64_t bb)`
- Debug helper - prints a bitboard as an 8x8 grid to stdout.

### Move Accessor Free Functions (all `inline`, all take `uint32_t m`)

| Function | Bits | Returns |
|----------|------|---------|
| `move_from(m)` | 0-5 | `int` from-square |
| `move_to(m)` | 6-11 | `int` to-square |
| `move_piece(m)` | 12-15 | `Piece` moving piece |
| `move_captured_piece(m)` | 16-19 | `Piece` (0xF = no capture) |
| `move_promotion_piece(m)` | 20-23 | `Piece` (0xF = no promotion) |
| `move_is_capture(m)` | 16-19 | `bool` true if captured piece != 0xF |
| `move_is_promotion(m)` | 20-23 | `bool` true if promo piece != 0xF |
| `move_castling_rights(m)` | 24-27 | `uint8_t` castling rights snapshot |
| `move_is_castle(m)` | 28 | `bool` |
| `move_is_en_passant(m)` | 29 | `bool` |
| `move_is_double_push(m)` | 30 | `bool` |
| `move_side(m)` | 31 | `Color` side that made this move |

#### `uint32_t pack_move(from, to, piece, captured=0xF, promo=0xF, is_castle=false, is_ep=false, is_dbl=false)`
- Packs all move fields into a 32-bit integer.
- Bits 24-27 (castling rights) and bit 31 (side) are NOT set here - they are OR'd in by `make_move` before pushing to `move_history`.

#### `string move_to_uci(uint32_t m)`
- Converts a packed move to UCI notation string (e.g. "e2e4", "e7e8q").
- Appends promotion character (n, b, r, q) if `move_is_promotion(m)` is true.

### `class Board`

**Single instance per game. Holds all mutable board state.**

#### Member Variables

| Variable | Type | Purpose |
|----------|------|---------|
| `bitboards[12]` | `uint64_t[12]` | One bitboard per piece type |
| `occupancy[3]` | `uint64_t[3]` | [WHITE], [BLACK], [ALL] occupancy |
| `piece_on[64]` | `Piece[64]` | Piece type on each square; 0xF = empty |
| `side_to_move` | `Color` | Whose turn it is |
| `enemy_color` | `Color` | Always opposite of `side_to_move` |
| `castling_rights` | `uint8_t` | 4-bit bitmask |
| `en_passant` | `uint8_t` | EP target square or 255 if none |
| `halfmove_clock` | `int` | Moves since last capture or pawn push |
| `fullmove_number` | `int` | Full move counter |
| `move_history` | `vector<uint32_t>` | Packed undo-moves for `unmake_move()` |
| `ep_history` | `vector<uint8_t>` | En-passant state history |
| `halfmove_history` | `vector<uint8_t>` | Halfmove clock history |

#### `Board()` constructor
- Reserves 4096 entries in all three history vectors (prevents re-allocation during search).
- Calls `set_fen(START_FEN)` to initialise to the starting position.

#### `void set_fen(string fen_string)`
- Clears all state and parses a FEN string into the board.
- Parses 6 space-separated fields: piece placement, side, castling rights, EP square, halfmove clock, fullmove number.
- `c - '0'` converts digit characters to integers (count of empty squares to skip).
- `c - 'a'` and `c - '1'` convert algebraic coordinates to file/rank indices.
- Calls `update_occupancy()` at the end.

#### `string get_fen()`
- Reconstructs the FEN string from current board state. Inverse of `set_fen`.

#### `void update_occupancy()`
- Recomputes `occupancy[WHITE]`, `occupancy[BLACK]`, and `occupancy[2]` from `bitboards[]`.
- Called at the end of `set_fen` and `make_move`.
- O(12) bit-OR operations - very fast.

#### `void print_board()`
- Prints the board to stdout in ASCII, rank 8 at the top.

#### `void verify_board(Board& board)` (debug only)
- Checks that `occupancy[]` matches `bitboards[]`. Prints mismatch messages to stdout.
- Not called in any production path.

#### `void make_move(uint32_t move)`

Step-by-step execution:

1. **Build undo record**: Packs a copy of the move with `castling_rights` (bits 24-27) and `side_to_move` (bit 31) OR'd in. Pushes to `move_history`, `ep_history`, `halfmove_history`.
2. **Halfmove clock**: Increments by 1. Resets to 0 on pawn move or capture.
3. **Basic piece move**: XOR the piece bitboard with `from_mask | to_mask`. Update `piece_on`.
4. **Promotion**: If promoted, XOR the pawn off the promotion square, XOR the promotion piece on.
5. **Capture handling**:
   - Normal capture: XOR the captured piece off `to_square`, update enemy occupancy.
   - En-passant capture: Derives the captured pawn square as `to +/- 8` and removes it there. The flag `is_en_passant` tells `make_move` not to look at `to_square` for the captured piece.
6. **Castling**: Moves the rook in addition to the king using hardcoded rook from/to squares (to=6 is white kingside, to=2 is white queenside, to=62 is black kingside, to=58 is black queenside).
7. **Castling rights update**: Clears rights when king or rook moves, or when a rook is captured on its starting square.
8. **En-passant update**: Sets `en_passant` to the square behind the double-pushed pawn (`to +/- 8`). Otherwise resets to `255`.
9. **Side swap**: Flips `side_to_move` and `enemy_color`. Increments `fullmove_number` if black just moved.
10. **Occupancy**: Calls `update_occupancy()`.

**Optimisation opportunity**: Replace the final `update_occupancy()` call with pure incremental XOR updates throughout the function. All the information to do this incrementally is already available at each step.

#### `void unmake_move()`
- Exact inverse of `make_move`. Pops from `move_history`, `ep_history`, `halfmove_history`.
- Restores: `castling_rights`, `en_passant`, `halfmove_clock`, `side_to_move`, `fullmove_number`.
- Uses `move_side(move)` (bit 31) to determine which side made the move being undone.
- Reverses all bitboard changes in reverse order.
- Does NOT call `update_occupancy()` - instead adjusts `occupancy[]` incrementally.
- En-passant, halfmove, and castling state are restored directly from history - not re-derived.

---

## 7. File: `rules.cpp`

**Layer 3 - Legal move generation and game result detection.**

### `struct MoveList`
- Fixed-size array: `uint32_t move_list[218]` - 218 is the maximum legal moves in any chess position.
- `int scores[218]` - for future move ordering (not yet used for sorting in search).
- `push(uint32_t m, int score=0)` - appends a move.
- `size()` - returns current count.
- `clear()` - resets count to 0 without zeroing memory.

### Magic Wrapper Functions

#### `uint64_t rook_attacks(int sq, uint64_t occ)`
- O(1) rook attack lookup.
- Steps: `occ &= ROOK_MASK[sq]`, then `return ROOK_ATTACKS[sq][(occ * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]]`.

#### `uint64_t bishop_attacks(int sq, uint64_t occ)`
- Same pattern as `rook_attacks` but for diagonals.

#### `uint64_t queen_attacks(int sq, uint64_t occ)`
- Returns `rook_attacks(sq, occ) | bishop_attacks(sq, occ)`.

### `struct PinInfo`
- `uint64_t pinned` - bitboard of all friendly pinned pieces.
- `uint64_t pin_mask[64]` - for each pinned square, the set of squares the pinned piece is allowed to move to (the ray between the king and the attacking slider, inclusive of both endpoints).
- All `pin_mask` entries initialise to `~0ULL` (all squares allowed) so unpinned pieces are not restricted.

### `PinInfo find_pins(Board& board)`
- Examines all 8 rays from the friendly king.
- For each ray: finds the first occupying piece. If it is friendly, finds the second. If the second is an enemy slider aligned with that direction, the first piece is pinned.
- Stores the full ray (both directions through the king) as `pin_mask` so the pinned piece may still move along the pin line.

### `bool ep_exposes_king(Board& board, int from_sq, int ep_sq)`
- Handles the rare horizontal-pin case: after an en-passant capture, both the capturing pawn AND the captured pawn disappear from the same rank, potentially exposing the king to a rook or queen on that rank.
- Constructs a hypothetical occupancy with both pawns removed, then checks all rank rays from the king for enemy rooks/queens.

### `Color piece_color(Piece p)`
- Returns `WHITE` if `p <= Piece::K`, else `BLACK`.

### `pair<uint64_t,uint64_t> knight_attacks(uint64_t lsb, uint64_t all_occ, uint64_t enemy_occ)`
- Computes all knight attacks from a single knight bitboard.
- Uses shift + file-mask pairs to avoid wrap-around.
- Returns `{quiet_squares, capture_squares}`.

### `uint64_t append_sliding_moves(MoveList& moves, Board& board, Piece piece, int dir_start, int dir_end, const PinInfo& pins, uint64_t legal_mask)`
- Handles rooks (dir_start=0, dir_end=4), bishops (4,8), and queens (0,8) all in one function.
- For each piece: gets magic attack bitboard, filters by pin mask and legal mask, splits into quiets and captures, pushes to `MoveList`.
- Returns the full attack mask for the piece type.

### `uint64_t append_knight_moves(MoveList& moves, Board& board, Piece piece, const PinInfo& pins, uint64_t legal_mask)`
- Iterates over all knights, skips pinned ones (pinned knights can never move legally), calls `knight_attacks`.

### `class MoveGenerator`

#### `void PawnMoves(Board& board, MoveList& moves, const PinInfo& pins, uint64_t legal_mask)`
Handles all pawn move types:
1. **Single push**: Shift pawns forward, mask against empty squares.
2. **Double push**: From start rank, two forward shifts both masked against empty squares.
3. **Captures left/right**: Shift with file guards, intersect with enemy occupancy.
4. **Promotions**: Any push or capture landing on rank 8 (white) or rank 1 (black) emits 4 moves (Q, R, B, N).
5. **En-passant**: Checks `board.en_passant != 255`, finds pawns that can capture into the EP square, calls `ep_exposes_king` as extra guard.

All move types check `pin_mask[from_square]` before adding a move.

#### `void KnightMoves / BishopMoves / RookMoves / QueenMoves`
- Each delegates to the appropriate helper function (`append_knight_moves` or `append_sliding_moves`).

#### `void KingMoves(Board& board, MoveList& moves, uint64_t enemy_attack_mask)`
- Computes all 8 surrounding squares using bitshift arithmetic (no lookup table needed).
- Filters out squares covered by `enemy_attack_mask` (king cannot move into check).
- Emits castling moves if: rights available, squares between king and rook are empty, and none of the king-path squares are attacked.

#### Attack-only functions (used inside `generate_moves` to build the enemy attack mask)

| Function | Computes |
|----------|---------|
| `pawnatk(board, checkers)` | Enemy pawn attack squares; sets `checkers` if any pawn attacks the king |
| `knightatk(board, checkers)` | Enemy knight attacks; sets `checkers` |
| `sliding_atks(board, piece, dir_start, dir_end, checkers)` | Enemy slider attacks (X-rays through king); sets `checkers` |
| `kingatk(board)` | Enemy king attack squares |

**Note on `sliding_atks`**: Uses `occ ^ king_bb` (king removed from occupancy) when computing slider attacks. This prevents the king from blocking its own retreat squares behind a sliding check.

#### `void generate_moves(Board& board, MoveList& moves)`
Top-level legal move generator. Three branches:

1. **No check** (check_count == 0): `legal_mask = ~0ULL`. All pieces may move anywhere legal. Calls all 6 piece move generators.
2. **Single check** (check_count == 1):
   - If checker is a pawn or knight: `legal_mask = checkers` (must capture the checker).
   - If checker is a slider: `legal_mask = ray between king and checker inclusive` (must block or capture).
   - King may still move to any non-attacked square.
3. **Double check** (check_count >= 2): Only king moves are legal.

### Game Rule Functions

#### `bool is_in_check(Board& board)`
- Instantiates a `MoveGenerator`, calls the 5 attack functions (not king-attack), returns `checkers != 0`.

#### `bool is_insufficient_material(const Board& board)`
Checks for draws by insufficient mating material:
- If any pawns, rooks, or queens exist: sufficient material, return false.
- K vs K: draw.
- K + single minor vs K: draw.
- K + B vs K + B with same-coloured bishops: draw.

#### `GameResult get_game_result(Board& board)`
Returns one of: `GAME_ONGOING`, `GAME_CHECKMATE`, `GAME_STALEMATE`, `GAME_FIFTY_MOVE_DRAW`, `GAME_SEVENTY_FIVE_MOVE_DRAW`, `GAME_INSUFFICIENT_MATERIAL`.

Priority order:
1. 75-move draw (halfmove_clock >= 150).
2. 50-move draw (halfmove_clock >= 100).
3. Insufficient material.
4. Generate moves. If none and in check: checkmate. If none and not in check: stalemate.
5. Otherwise: ongoing.

---

## 8. File: `search.cpp`

**Layer 4 - Minimax search with alpha-beta pruning and static evaluation.**

### `class Engine`

#### `static int mirror(int sq)`
- Converts a square index from white's perspective to black's for PST lookup.
- Formula: `(7 - rank) * 8 + file`.
- Black pieces use `mirror(sq)` so they share white's PST values symmetrically.

#### `int evaluate(Board& board)`
- Pure static evaluator. Loops over all 64 squares.
- For each piece: adds `PIECE_VALUE[p] + PST[table_sq]` for white, subtracts for black.
- Returns centipawns: positive = white advantage, negative = black advantage.
- **Optimisation note**: Called at every leaf node. Currently O(64). Can be made O(1) by maintaining an incremental score in `make_move`/`unmake_move`.

#### `int minimax(Board& board, int depth, int alpha, int beta, bool maximizing)`
- Recursive alpha-beta minimax.
- **Base case**: `depth == 0` calls `evaluate(board)`.
- **Terminal detection**: If no legal moves and in check, return `+/-20000 +/- depth` (mate score). The depth term makes shallower mates score higher.
- **Maximising** (white): Tries to maximise score. Updates `alpha`. Prunes if `beta <= alpha`.
- **Minimising** (black): Tries to minimise score. Updates `beta`. Prunes if `beta <= alpha`.
- **No move ordering**: Moves are evaluated in generator order. Adding MVV-LVA ordering here would significantly improve alpha-beta cutoff rates.

#### `uint32_t best_move(Board& board, int depth)`
- Root-level search. Generates all legal moves and calls `minimax` for each.
- Stores the score for each root move in `moves.scores[]`.
- Finds the best score across all root moves.
- **Randomisation**: Collects all moves within 50 centipawns of the best score into `candidates`. Picks randomly among the top 5. This gives variety while remaining tactically sound.
- Returns `0` if no legal moves exist.

---

## 9. File: `main.cpp`

**Layer 5 - CLI entry point and command dispatch.**

### CLI Commands

#### `game.exe moves <FEN>`
- Generates all legal moves and returns a JSON response.
- Output: FEN, side to move, game status string, in-check flag, 64-char grid string, array of UCI move strings.

#### `game.exe move <FEN> <UCI_MOVE> [depth]`
- Applies a player move to the FEN, then automatically runs the engine at the given depth (default 6).
- Output: Updated FEN, bot's response move, game status.

#### `game.exe best <FEN> [depth]`
- Runs the engine on the given FEN and returns the best move in JSON.

#### `game.exe play [depth]`
- Starts an interactive terminal game. Human plays white, engine plays black.

#### `game.exe perft <FEN> [depth]`
- Counts leaf nodes at the given depth for move generation correctness testing.

#### `game.exe server [depth]`
- Starts a persistent server loop over stdin/stdout.
- Reads commands line by line: `MOVE <FEN> <UCI_MOVE>` or `BEST <FEN>`.
- Responds with JSON then prints `===READY===` to signal readiness for the next command.
- Used by `server.js` to communicate with the web frontend.

### Matched Move Logic
In all interactive commands, user input (UCI string) is matched against the legal move list:
```cpp
uint32_t matched_move = 0;
for (int i = 0; i < moves.size(); i++) {
    if (move_to_uci(moves.move_list[i]) == user_input) {
        matched_move = moves.move_list[i];
        found = true;
        break;
    }
}
```
If not found, returns error JSON. If found, calls `board.make_move(matched_move)`.

---

## 10. Performance Bottlenecks and Optimisation Guide

### Current Bottlenecks (ordered by impact)

| Bottleneck | Location | Fix |
|------------|----------|-----|
| No move ordering | `search.cpp minimax()` | Add MVV-LVA: sort captures by captured_value minus moving_value. Dramatically improves alpha-beta cutoffs. |
| Evaluate from scratch every leaf | `search.cpp evaluate()` | Maintain incremental score in `make_move`/`unmake_move`. Removes 64-square loop per node. |
| `update_occupancy()` rebuilds from scratch | `board.cpp make_move()` | Remove the final call and do full incremental XOR updates throughout `make_move`. |
| No transposition table | `search.cpp` | Add a Zobrist-hashed transposition table to avoid re-searching seen positions. |
| No quiescence search | `search.cpp minimax()` | At depth 0, search all captures until a quiet position is reached (prevents horizon effect). |
| No iterative deepening | `search.cpp best_move()` | Search depth 1, 2, 3... up to target depth with time management. |
| Randomisation at root | `search.cpp best_move()` | The `rand() % limit` pick can play non-optimal moves. Remove for maximum strength. |

### Quick Wins (Low effort, high impact)
1. **MVV-LVA move ordering**: Sort moves so captures of high-value pieces by low-value pieces are searched first. Implement by scoring each move in `MoveList.scores[]` before the search loop.
2. **Incremental evaluation**: Store a score in `Board`, update it delta-style in `make_move`/`unmake_move`. At leaf nodes just return `board.eval_score`.
3. **Killer moves**: Store 2 non-capture moves that caused beta cutoffs at each ply, try them first.

### Medium Effort
4. **Quiescence search**: After depth 0, continue searching only captures until a quiet position. This prevents the engine from evaluating positions mid-combination.
5. **Zobrist hashing + transposition table**: Hash the board state, store `{score, depth, flag}` in a hash table. On revisit, if stored depth >= remaining depth, use stored score.
6. **Null move pruning**: Give the opponent a free move. If still failing high, prune the branch.

### High Effort
7. **PEXT magic bitboards**: Replace `(occ * MAGIC) >> SHIFT` with `_pext_u64(occ, MASK)` on BMI2-capable CPUs. Eliminates the magic multiply entirely.
8. **Parallel search**: Use `std::thread` to search multiple root moves in parallel. Requires careful handling of the transposition table.
