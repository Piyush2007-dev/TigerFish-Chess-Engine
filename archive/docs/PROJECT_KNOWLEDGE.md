# Ultimate Technical Reference Manual: TigerFish Chess Engine

This document represents the absolute reference manual for the TigerFish Chess Engine. It captures every technical design, file structure, bitwise calculation logic, algorithmic optimization, and the long-term ELO-enhancement roadmap.

---

## 1. Directory Structure & File Inventory

The workspace is organized to isolate core logic, Node.js interfaces, local test suites, and gitignored developer utilities.

```
chess_engine_v0/
├── tigerfish/                  # 🟢 CORE ENGINE (tracked by Git)
│   ├── engine/                 # Modular C++ Source Code
│   │   ├── chess.cpp           # Board representation, state structures, bottom-includes
│   │   ├── move_generator.cpp  # Legal/pseudo-legal move generation
│   │   ├── move_finder.cpp     # Engine class: evaluate() & minimax search
│   │   ├── magic_lut.cpp       # Magics, sliding attack masks, and ray tables
│   │   ├── pst_lut.cpp         # Piece values and static Piece-Square Tables
│   │   └── game.cpp            # Entry point & CLI (spawns under server.js)
│   ├── server.js               # Node.js API server (CORS enabled, Port 8080)
│   └── index.html              # Web Interface for playing against TigerFish
│
├── benchmark/                  # 🔴 LOCAL-ONLY (gitignored)
│   ├── bench.cpp / benchv2.cpp # C++ speed testing harnesses (multi-threaded)
│   └── stockfish-windows-x86-64-avx2.exe
│
├── perft/                      # 🔴 LOCAL-ONLY (gitignored)
│   ├── perft.cpp               # Standard perft test
│   ├── perft_split.cpp         # Perft move-split output
│   ├── perft_compare.cpp       # Parallel perft comparison tool vs Stockfish
│   ├── perft_compare_dynamic.cpp # Dynamic work-stealing, TT-based perft comparator
│   └── compare_perft.py        # Python automation script
│
├── archive/                    # 🔴 LOCAL-ONLY (gitignored)
│   ├── chess.cpp (old)         # Original monolithic file
│   ├── chess.py / chess.cpp    # Python bindings trial files
│   └── pst_tables.cpp (orphan) # Reference PST table definitions
│
├── tools/                      # 🔴 LOCAL-ONLY (gitignored)
│   └── test_game_ends.cpp      # Verification test suite for terminal states
│
├── .gitignore                  # Git tracking rules
├── pyproject.toml              # uv Python project configurations
└── .venv/                      # Python virtual environment (gitignored)
```

---

## 2. CLI Entry Point & Initialization (`game.cpp`)

The C++ backend is compiled and executed as a standalone command-line process:
* **Get legal moves:** `./game.exe moves "<fen>"`
* **Make a move:** `./game.exe make "<fen>" "<uci_move>"`
* **Find best move:** `./game.exe best "<fen>" <depth>`

When run, the engine parses these arguments, instantiates a `Board` object, and parses the FEN string into memory.

### FEN String Parsing (`Board::set_fen`)
A FEN string consists of 6 space-separated fields: `[Placement] [ActiveColor] [Castling] [EnPassant] [Halfmove] [Fullmove]`.

1. **Board Cleared:** 
   * All 12 bitboards are reset to `0ULL` (unsigned long long zero).
   * The flat `piece_on` array is filled with `0xF` (the empty square indicator).
   * The undo history stacks (`move_history`, `ep_history`, and `halfmove_history`) are cleared.
2. **Placement Field Parsing:**
   * Iterates from rank 7 (8th rank) down to rank 0, and file 0 to file 7.
   * If a character is a digit (e.g., `'8'`), the file pointer skips that many squares.
   * If it is a piece character (e.g., `'P'`, `'n'`), the engine maps the character to a `Piece` enum value:
     * **White:** `P = 0`, `N = 1`, `B = 2`, `R = 3`, `Q = 4`, `K = 5`
     * **Black:** `p = 6`, `n = 7`, `b = 8`, `r = 9`, `q = 10`, `k = 11`
   * Sets the corresponding bit: `bitboards[p] |= (1ULL << sq)` and updates the flat tracker: `piece_on[sq] = p`.
3. **Active Color:** Sets `side_to_move = WHITE` (0) if the field is `'w'`, else `BLACK` (1).
4. **Castling Rights:** Checks chars `'K'`, `'Q'`, `'k'`, `'q'` and updates `castling_rights` (a 4-bit bitmask: White King-side = `1`, White Queen-side = `2`, Black King-side = `4`, Black Queen-side = `8`).
5. **En Passant Square:** If the field is not `"-"`, it converts a coordinate like `"e3"` to a square index (file $4$, rank $2$ $\rightarrow$ index $20$). Otherwise, sets `en_passant = 255`.
6. **Occupancy Generation:** Combines bitboards to generate summary occupancies:
   * `occupancy[WHITE] = bitboards[P] | bitboards[N] | bitboards[B] | bitboards[R] | bitboards[Q] | bitboards[K]`
   * `occupancy[BLACK] = bitboards[p] | bitboards[n] | bitboards[b] | bitboards[r] | bitboards[q] | bitboards[k]`
   * `occupancy[ALL] = occupancy[WHITE] | occupancy[BLACK]`

---

## 3. How the Chess Board is Stored in C++ Memory

### A. What is a Bitboard?
A **bitboard** is a 64-bit unsigned integer (`uint64_t`). Because a chess board has exactly 64 squares, each bit in the integer maps directly to a square on the board:
* If a bit is set to `1`, a piece is present on that square.
* If a bit is set to `0`, the square is empty.
* Square $0$ is `a1` (bottom-left from White's perspective), square $7$ is `h1`, square $56$ is `a8`, and square $63$ is `h8`.

```
       A8  B8  C8  D8  E8  F8  G8  H8      ◄─── Index 56 to 63 (Black Rank 8)
       A7  B7  C7  D7  E7  F7  G7  H7
       ..  ..  ..  ..  ..  ..  ..  ..
       A2  B2  C2  D2  E2  F2  G2  H2
       A1  B1  C1  D1  E1  F1  G1  H1      ◄─── Index 0 to 7 (White Rank 1)
```

### B. Mappings and Core Arrays (`Board` Class)
Inside the `Board` class in [chess.cpp](file:///d:/PROJECTS/chess_engine_v0/tigerfish/engine/chess.cpp), the board state is tracked using three main memory structures:

#### 1. Piece Bitboards (`array<uint64_t, 12> bitboards`)
The engine tracks 12 distinct piece types using a `Piece` enum. Each index represents a bitboard of where those specific pieces stand:
* **White:** `P = 0` (Pawns), `N = 1` (Knights), `B = 2` (Bishops), `R = 3` (Rooks), `Q = 4` (Queens), `K = 5` (King).
* **Black:** `p = 6` (Pawns), `n = 7` (Knights), `b = 8` (Bishops), `r = 9` (Rooks), `q = 10` (Queens), `k = 11` (King).

#### 2. Occupancy Bitboards (`array<uint64_t, 3> occupancy`)
These are pre-calculated combinations of the piece bitboards to speed up move generation collision checks:
* `occupancy[0]` (WHITE): A bitwise `OR` of White's bitboards (indices $0$ to $5$).
* `occupancy[1]` (BLACK): A bitwise `OR` of Black's bitboards (indices $6$ to $11$).
* `occupancy[2]` (ALL): `occupancy[0] | occupancy[1]`. Shows every occupied square on the board.

#### 3. Flat Piece Tracker (`Piece piece_on[64]`)
Scanning bitboards to identify a piece occupies extra CPU cycles. To optimize this, a flat array stores the piece type occupying each square directly. Empty squares are set to the value `0xF` ($15$ in decimal, or `1111` in binary).

#### 4. State Flags & History Stacks
In addition to piece positions, the engine tracks game-state parameters:
* **`side_to_move` & `enemy_color`**: Can be `WHITE` ($0$) or `BLACK` ($1$).
* **`en_passant`**: Holds the square index where a pawn can be captured via en passant (e.g., $16$–$23$ for White, $40$–$47$ for Black). Set to `255` if en passant is invalid.
* **`castling_rights`**: A 4-bit bitmask tracking which castle moves remain eligible:
  * Bit 0 (`0x1`): White can castle King-side (WK).
  * Bit 1 (`0x2`): White can castle Queen-side (WQ).
  * Bit 2 (`0x4`): Black can castle King-side (BK).
  * Bit 3 (`0x8`): Black can castle Queen-side (BQ).
* **Clocks**: 
  * `halfmove_clock`: Tracks plies since the last capture or pawn move to enforce the 50-move draw rule.
  * `fullmove_number`: Starts at 1 and increments after Black plays.
* **History Stacks**: 
  To support search backtracking, three dynamic vectors act as undo stacks: `move_history` (stores previous packed moves), `ep_history` (stores previous en passant squares), and `halfmove_history` (stores previous halfmove clock values).

---

## 4. Binary Move Encoding (`class Move`)

Moves are packed into a single 32-bit unsigned integer (`uint32_t`) inside `class Move`:

```
 31 30 29 28 27      24 23      20 19      16 15      12 11       6 5        0
 ┌─┬─┬─┬─┬────────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
 │S│D│E│C│CastlingRts │ PromoPc  │ CapturPc │  Piece   │ ToSquare │FromSquare│
 └─┴─┴─┴─┴────────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
```

* **Bit Mappings:**
  * `0 to 5` (6 bits): Origin square index ($0$ to $63$).
  * `6 to 11` (6 bits): Destination square index ($0$ to $63$).
  * `12 to 15` (4 bits): Enum value of the piece being moved ($0$ to $11$).
  * `16 to 19` (4 bits): Enum value of the captured piece ($0$ to $11$, or `0xF` if none).
  * `20 to 23` (4 bits): Enum value of the promotion piece ($0$ to $11$, or `0xF` if none).
  * `24 to 27` (4 bits): Castling rights mask at the moment the move is played.
  * `28` (1 bit): Castling flag ($1$ if castled, $0$ otherwise).
  * `29` (1 bit): En Passant flag ($1$ if en passant capture, $0$ otherwise).
  * `30` (1 bit): Double Push flag ($1$ if a pawn pushed forward two squares, $0$ otherwise).
  * `31` (1 bit): Color side of the moving player ($0$ for White, $1$ for Black).

To reconstruct a move on the fly before executing, the `PackedMove` helper struct registers attributes and compiles them:
```cpp
struct PackedMove {
    Piece piece;
    int from_square;
    int to_square;
    uint32_t captured_piece = 0xFu;
    uint32_t promotion_piece = 0xFu;
    bool is_castle = false;
    bool is_en_passant = false;
    bool is_double_push = false;
    
    static uint32_t pack(PackedMove d) {
        uint32_t m = 0;
        m = ((uint32_t)d.from_square & 0x3Fu);
        m |= (((uint32_t)d.to_square & 0x3Fu) << 6);
        m |= (((uint32_t)d.piece & 0xFu) << 12);
        m |= ((d.captured_piece & 0xFu) << 16);
        m |= ((d.promotion_piece & 0xFu) << 20);
        if (d.is_castle) m |= 1u << 28;
        if (d.is_en_passant) m |= 1u << 29;
        if (d.is_double_push) m |= 1u << 30;
        return m;
    }
};
```

---

## 5. Modular Code Compilation Scheme

To avoid compiler loop errors while ensuring autocomplete definitions resolve in VS Code/IDE, TigerFish uses a **Circular Bottom-Inclusion Protocol**:

```
                    magic_lut.cpp
                          │ (includes)
                          ▼
   move_generator.cpp ──► chess.cpp ◄── move_finder.cpp
        │ (includes)       │           (includes)
        └──────────────────┼───────────┘
                           │ (bottom-includes)
                           ▼
                 [ move_generator.cpp ]
                 [   move_finder.cpp  ]
```

1. **`chess.cpp`**: Declares board enums, `Move` getters/setters, `Board` state structure, and includes `magic_lut.cpp` + `pst_lut.cpp` at the top. At the **very bottom**, it includes `move_generator.cpp` and `move_finder.cpp` to complete compilation.
2. **`move_generator.cpp`**: Houses en passant, check detection, and pin masks calculations. It includes `#include "chess.cpp"` at the top so that the IDE resolves the `Board` and `Move` structures.
3. **`move_finder.cpp`**: Houses `class Engine` minimax and search. It includes `#include "chess.cpp"` and `#include "pst_lut.cpp"` at the top.
4. **`#pragma once`**: Declared at the head of every file. This prevents loop errors during compilation, meaning the entire system compiles cleanly by running `g++ game.cpp`.

---

## 6. The Move Generation Pipeline (`move_generator.cpp`)

When `generate_moves(board, moves)` is called, the generator constructs only mathematically legal moves using a four-step pipeline.

```
                          find_pins(board)
                                 │
                                 ▼
                     Calculate Pinned Pieces Mask
                                 │
                                 ▼
                 Accumulate Checker Attacks Mask
            (pawns, knights, sliding piece attack paths)
                                 │
                                 ▼
                      Evaluate check_count
                     /         │          \
         check_count == 0      │      check_count >= 2
                │              │              │
                ▼              ▼              ▼
         legal_mask = ~0   check_count == 1   Only King moves generated
                               │
                               ▼
                   legal_mask = check path
                               │
                               ▼
        Generate moves for Pawns, Knights, Sliders, and King
            (Filtered against pin and legal masks)
```

---

### Step 1: Detailed Pin Calculations (`find_pins`)

Pins are calculated by casting rays outward from the active player's King:
1. **Locate the King:** Identifies the King's square index using `lsb_index` on the King's bitboard: `king_sq = lsb_index(board.bitboards[king])`.
2. **Scan the 8 Ray Directions:**
   Iterates through directions $0$ to $7$:
   * Orthogonal: North ($0$), South ($1$), East ($2$), West ($3$).
   * Diagonal: North-East ($4$), South-West ($5$), North-West ($6$), South-East ($7$).
3. **Isolate Ray Blockers:**
   Fetches the precomputed ray mask and intersects it with occupied squares:
   `blockers = ray & occupancy[ALL]`. If `blockers == 0`, no pins can exist in this direction; the loop skips.
4. **Find the First Blocker (LSB vs. MSB):**
   Depending on the direction, the engine reads either the lowest or highest bit index:
   ```cpp
   int first = (dir % 2 == 0) ? lsb_index(blockers) : msb_index(blockers);
   ```
   * **LSB Search (`dir % 2 == 0`):** Used for North, East, North-East, and North-West (where square indices increase away from the King). The blocker closest to the King has the **lowest** bit index, retrieved using `lsb_index`.
   * **MSB Search (`dir % 2 == 1`):** Used for South, West, South-West, and South-East (where square indices decrease away from the King). The blocker closest to the King has the **highest** bit index, retrieved using `msb_index`.
5. **Verify Friendly Piece:**
   Converts the blocker index to a bitmask: `first_bb = 1ULL << first`.
   It checks: `if (!(first_bb & friendly)) continue;`. If the closest blocker is an enemy piece, it cannot be pinned. It blockades the ray, and the loop skips.
6. **Find the Second Blocker:**
   Clears the friendly piece from the blockers mask: `blockers &= ~first_bb`. If `blockers == 0`, no enemy pieces exist along this line.
   Otherwise, it scans for the second blocker behind the friendly piece:
   `int second = (dir % 2 == 0) ? lsb_index(blockers) : msb_index(blockers);`.
7. **Verify Enemy Slider Attacker:**
   Retrieves the piece type `p` at the second blocker's index: `Piece p = board.piece_on[second]`.
   * Verifies the piece belongs to the enemy color: `if (((p <= Piece::K) ? WHITE : BLACK) == board.side_to_move) continue;`.
   * Verifies the piece is a slider matching the ray direction:
     * Orthogonal (`dir < 4`): Attacker must be a Rook (`R`/`r`) or Queen (`Q`/`q`).
     * Diagonal (`dir >= 4`): Attacker must be a Bishop (`B`/`b`) or Queen (`Q`/`q`).
8. **Store Pin Results:**
   If a valid enemy slider is found, the friendly piece is marked as pinned:
   `pins.pinned |= first_bb`.
   The allowed movement path is set by combining the ray and its opposite ray:
   `pins.pin_mask[first] = ray | get_ray(opposite_dir[dir], king_sq);` where `opposite_dir[8] = {1, 0, 3, 2, 7, 6, 5, 4}`. The pinned piece can only move to squares within this mask (i.e. capturing the attacker or sliding along the pin ray).

---

### Step 2: Pawn Move Generation Details (`PawnMoves`)

Pawn move calculations use bitwise shifts and wrap-around filters:
* **Single Push:**
  Shifts pawn bitboards vertically: `pawn << 8` for White, `pawn >> 8` for Black. It intersects the output with empty squares: `& ~occupancy[ALL]`.
* **Double Push:**
  Pawn must be on its starting rank (`RANK_2` for White, `RANK_7` for Black) and the intermediate square must be empty. The engine shifts the single push bitboard forward by another rank and intersects with empty squares.
* **Diagonal Captures (Wrap-Around Protection):**
  When pawns capture, they shift diagonally. To prevent pawns on the A-file or H-file from wrapping around to the opposite edge of the board, the engine applies file masks before shifting:
  * White capture left: `(pawn & NOT_H_FILE) << 9`
  * White capture right: `(pawn & NOT_A_FILE) << 7`
  * Black capture left: `(pawn & NOT_A_FILE) >> 9`
  * Black capture right: `(pawn & NOT_H_FILE) >> 7`
* **Promotions:**
  If a push or capture reaches the promotion rank (`RANK_8` for White, `RANK_1` for Black), the engine generates 4 separate moves, packing promotion pieces (Queen, Rook, Bishop, Knight) into the move value.
* **En Passant & King Exposure Check (`ep_exposes_king`):**
  An en passant capture removes two pawns from the board (the capturing pawn's origin and the captured pawn's square) and places a pawn on the destination square. If the King is on the same rank as these pawns, this capture can open up a horizontal attack path for an enemy Rook or Queen.
  
  To prevent illegal moves, the engine runs `ep_exposes_king()` to simulate the en passant state:
  ```cpp
  uint64_t occ = board.occupancy[2];
  occ &= ~(1ULL << from_sq);      // Remove capturing pawn from origin
  occ &= ~(1ULL << captured_sq);  // Remove captured pawn from its square
  occ |= (1ULL << ep_sq);        // Place capturing pawn on EP destination
  ```
  It then casts orthogonal and diagonal rays from the King's square using the simulated occupancy `occ` to verify if any enemy sliding check is revealed. If so, the en passant move is discarded.

---

### Step 3: Magic Bitboard Sliding Attacks

To calculate rook, bishop, and queen moves instantly, the engine uses **Magic Bitboards**:
```cpp
inline uint64_t rook_attacks(int sq, uint64_t occ) {
    occ &= ROOK_MASK[sq];
    return ROOK_ATTACKS[sq][(occ * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]];
}
```
1. **Blocker Masking:**
   The occupancy bitboard `occ` is masked with `ROOK_MASK[sq]` or `BISHOP_MASK[sq]`. These masks exclude the outer edges of the board. Because blockers on the outer edges cannot change whether a slider reaches the edge itself, excluding them keeps lookup sizes small.
2. **Index Hashing:**
   The masked occupancy is multiplied by a 64-bit magic multiplier (`ROOK_MAGICS[sq]` or `BISHOP_MAGICS[sq]`). The result is shifted right by `ROOK_SHIFT[sq]` or `BISHOP_SHIFT[sq]` to yield a compact index.
3. **Table Lookup:**
   The index maps directly to the pre-calculated attack bitboard in `ROOK_ATTACKS` or `BISHOP_ATTACKS`.
4. **Magic Table Initialization:**
   During startup, the engine uses the **Carry-Rippler** algorithm inside `init_magics()` to iterate over all possible blocker sub-permutations:
   ```cpp
   uint64_t rsub = 0;
   do {
       int idx = (int)((rsub * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]);
       ROOK_ATTACKS[sq][idx] = compute_rook_attacks(sq, rsub);
       rsub = (rsub - 1) & rmask; // Carry-rippler step
   } while (rsub != 0);
   ```
   `compute_rook_attacks` and `compute_bishop_attacks` run manual ray-casting loops to determine sliding blocker boundaries, caching the results inside the lookup tables.

---

### Step 4: King Moves & Castling Verification

The King moves to adjacent squares according to precomputed attack offsets:
* **Castling Rights Verification:**
  Castling requires that:
  1. The King and the chosen Rook have not moved (verified via `board.castling_rights`).
  2. The squares between them are empty (verified by checking `board.occupancy[2]`).
  3. The King is not in check, and does not pass through check (verified by checking if the intermediate squares are attacked by `enemy_attack_mask`).
* **Castling Coordinates and Bits:**
  * **White King-Side (WK):**
    * Rights bit check: `board.castling_rights & WHITE_KING_SIDE` ($1$).
    * Empty square check: Squares $5$ (`f1`) and $6$ (`g1`) must be empty: `!(board.occupancy[2] & ((1ULL<<5) | (1ULL<<6)))`.
    * Attack check: Squares $4$ (`e1`), $5$ (`f1`), and $6$ (`g1`) must not be under attack: `!(enemy_attack_mask & ((1ULL<<4) | (1ULL<<5) | (1ULL<<6)))`.
  * **White Queen-Side (WQ):**
    * Rights bit check: `board.castling_rights & WHITE_QUEEN_SIDE` ($2$).
    * Empty square check: Squares $1$ (`b1`), $2$ (`c1`), and $3$ (`d1`) must be empty.
    * Attack check: Squares $2$ (`c1`), $3$ (`d1`), and $4$ (`e1`) must not be under attack. (Note: square $1$/`b1` is allowed to be attacked).
  * **Black King-Side (BK):**
    * Rights bit check: `board.castling_rights & BLACK_KING_SIDE` ($4$).
    * Empty square check: Squares $61$ (`f8`) and $62$ (`g8`) must be empty.
    * Attack check: Squares $60$ (`e8`), $61$ (`f8`), and $62$ (`g8`) must not be under attack.
  * **Black Queen-Side (BQ):**
    * Rights bit check: `board.castling_rights & BLACK_QUEEN_SIDE` ($8$).
    * Empty square check: Squares $57$ (`b8`), $58$ (`c8`), and $59$ (`d8`) must be empty.
    * Attack check: Squares $58$ (`c8`), $59$ (`d8`), and $60$ (`e8`) must not be under attack.

---

### Step 5: Checkers Identification

When the active player is in check, the engine identifies the checker pieces to construct check-evasion masks:
* **Pawn Attackers (`pawnatk`):**
  Constructs pawn attacks from the King's square. If they intersect with enemy pawns, those pawn squares are added to `checkers`.
* **Knight Attackers (`knightatk`):**
  Constructs knight attacks from the King's square. If they intersect with enemy knights, those knight squares are added to `checkers`.
* **Sliding Attackers (`sliding_atks`):**
  Uses Magic Bitboard lookups to find sliding attacks. In order to trace the attack paths past the King (e.g. to identify squares directly behind the King that he cannot move to), the King's square is excluded from the occupancy bitboard during ray evaluation:
  ```cpp
  uint64_t occ = board.occupancy[2] ^ board.bitboards[king];
  ```
  If these rays intersect with enemy sliding pieces, their squares are added to `checkers`.

---

## 8. State Transitions (Playing and Undoing Moves)

### A. Playing a Move (`Board::make_move`)
1. **Preserve Undo State:**
   * Packs the moving piece, origin, destination, capture, promotion, and castling flags into a 32-bit `Move` object.
   * Encodes the current `castling_rights` and the `side_to_move` into the move's upper bits.
   * Pushes the move to `move_history`, the `en_passant` square to `ep_history`, and the `halfmove_clock` to `halfmove_history`.
2. **Move the Piece:**
   * Clears the bit at the origin and sets the bit at the destination on the moving piece's bitboard: `bitboards[piece] ^= (from_mask | to_mask)`.
   * Updates the flat `piece_on` array.
3. **Handle Promotions:**
   * If the move is a promotion, it clears the pawn's bit at the destination and turns on the chosen promotion piece's bit (Knight/Bishop/Rook/Queen).
4. **Handle Captures:**
   * *Normal Capture:* Looks up `Move::captured_piece()`, clears its bit at the destination square on the enemy bitboard, and updates the enemy occupancy mask.
   * *En Passant:* Identifies the captured pawn's square (`to_square - 8` for White, `to_square + 8` for Black) and clears it from the enemy pawn bitboard and occupancy.
5. **Handle Castling (Rook Shifts):**
   * *White King-side (target e1g1):* Moves Rook from square $7$ to $5$.
   * *White Queen-side (target e1c1):* Moves Rook from square $0$ to $3$.
   * *Black King-side (target e8g8):* Moves Rook from square $63$ to $61$.
   * *Black Queen-side (target e8c8):* Moves Rook from square $56$ to $59$.
6. **Update State Flags:**
   * Resets `halfmove_clock` to `0` if a pawn moved or a capture occurred; otherwise, increments it.
   * Clears castling permissions if a King or Rook moves, or if an enemy Rook is captured on its starting corner square.
   * If a pawn double-pushed, sets `en_passant` to the skipped square behind it; otherwise, sets it to `255`.
   * Flips `side_to_move` and `enemy_color`.
   * Updates occupancy maps.
   * **The Move List Clear (Critical Fix):** `make_move()` calls `moves.clear()` at the end. When running search, we must pass a throwaway `MoveList dummy` object to absorb this clear, otherwise our main iteration list would be wiped out after the first move.

```cpp
// Correct search call passing dummy list
MoveList dummy;
board.make_move(moves.move_list[i], dummy);
```

### B. Undoing a Move (`Board::unmake_move`)
1. Pops the last packed move from `move_history` and unpacks it.
2. Restores `castling_rights`, `en_passant` square, and `halfmove_clock` from their history stacks.
3. Swaps `side_to_move` and `enemy_color`. If restoring Black's turn, decrements `fullmove_number`.
4. Moves the piece back: sets the origin square and clears the destination square on the piece's bitboard and the flat `piece_on` array.
5. If it was a promotion, deletes the promotion piece and restores the pawn.
6. If it was a castle, moves the Rook back to its starting corner.
7. If a capture occurred, restores the captured piece (or pawn if en passant) to its destination square, setting its bit on the enemy bitboard and updating occupancy masks.

---

## 9. Search and Evaluation Loop (`move_finder.cpp`)

Search determines the best candidate move by traversing future states recursively and evaluating the resulting board positions.

### A. Static Evaluation (`Engine::evaluate`)
At the end of a search branch (`depth == 0`), the engine evaluates the position and returns a score in centipawns:
$$\text{Score} = \sum (\text{White Material} + \text{White PST}) - \sum (\text{Black Material} + \text{Black PST})$$

* Loops through squares $0$ to $63$. If occupied, it reads the piece type $p$.
* Retrieves base value: `PIECE_VALUE[p]` (e.g. `Pawn = 100, Knight = 320, Bishop = 330, Rook = 500, Queen = 900, King = 20000`).
* Retrieves positional bonus from Piece-Square Tables (PSTs). White uses the square index directly. Black uses mirror(sq) to flip the coordinate vertically, ensuring evaluation symmetry:
  ```cpp
  static int mirror(int sq) {
      int rank = sq / 8;
      int file = sq % 8;
      return (7 - rank) * 8 + file;
  }
  ```

#### Piece-Square Table Bonuses (`pst_lut.cpp`)
Positional bonuses are added to base piece values:

```cpp
inline constexpr int PAWN_PST[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    5, 10, 10,-20,-20, 10, 10, 5,
    5,-5,-10, 0, 0,-10,-5, 5,
    0, 0, 0, 20, 20, 0, 0, 0,
    5, 5, 10, 25, 25, 10, 5, 5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
    0, 0, 0, 0, 0, 0, 0, 0
};
// Similarly defined: KNIGHT_PST, BISHOP_PST, ROOK_PST, QUEEN_PST, KING_PST
```

---

### B. Alpha-Beta Minimax Search (`Engine::minimax`)
The search traverses the game tree recursively, using `alpha` (the lower limit of White's guaranteed score) and `beta` (the upper limit of Black's guaranteed score) to prune branches:

```cpp
int minimax(Board& board, int depth, int alpha, int beta, bool maximizing) {
    if (depth == 0) return evaluate(board);

    MoveList moves;
    MoveGenerator mg;
    mg.generate_moves(board, moves);

    // Terminal state handling
    if (moves.size() == 0) {
        if (board.is_in_check()) {
            // Checkmate: return score scaled by depth to prioritize faster checkmates
            return maximizing ? (-20000 - depth) : (20000 + depth);
        }
        return 0; // Stalemate
    }

    if (maximizing) {
        int best = -100000;
        for (int i = 0; i < moves.size(); i++) {
            MoveList dummy;
            board.make_move(moves.move_list[i], dummy);
            int score = minimax(board, depth - 1, alpha, beta, false);
            board.unmake_move();
            
            best = max(best, score);
            alpha = max(alpha, best);
            if (beta <= alpha) break; // Pruning cutoff
        }
        return best;
    } else {
        int best = 100000;
        for (int i = 0; i < moves.size(); i++) {
            MoveList dummy;
            board.make_move(moves.move_list[i], dummy);
            int score = minimax(board, depth - 1, alpha, beta, true);
            board.unmake_move();
            
            best = min(best, score);
            beta = min(beta, best);
            if (beta <= alpha) break; // Pruning cutoff
        }
        return best;
    }
}
```

---

## 10. Parallel Perft Testing Engine (`perft_compare_dynamic.cpp`)

To validate move generator correctness, we built a parallel perft comparison tool. This tool achieved a throughput of **5.04 Billion NPS** at Depth 9 by solving CPU bottlenecks.

### Parallel Work-Stealing Loop
Instead of assigning a thread per root move, which leaves threads idle when lighter moves complete, it expands the board down to depth 3, generating **8,902 independent sub-tasks**. 
```cpp
struct Task {
    int root_move_idx;
    Board board;
    int remaining_depth;
};
```
Threads steal tasks atomically via an atomic index tracker:
```cpp
int idx = next_task.fetch_add(1, memory_order_relaxed);
if (idx >= num_tasks) break;
const Task& task = tasks[idx];
```

### Partitioned Global Transposition Table
To prevent thread lock contention under high concurrency, mutexes are partitioned into **65,536 buckets**:
```cpp
struct TTEntry {
    uint64_t key = 0;
    uint64_t nodes = 0;
    int8_t depth = 0;
    uint64_t white_occ = 0;
    uint64_t black_occ = 0;
    uint8_t castling_rights = 0;
    uint8_t en_passant = 0;
    uint8_t side_to_move = 0;
};

struct GlobalTT {
    vector<TTEntry> table;
    mutex* locks;
    size_t size_mask;

    GlobalTT(size_t size) {
        table.resize(size);
        locks = new mutex[65536];
        size_mask = size - 1;
    }
    // lookup() and store() lock via locks[key & 65535]
};
```

---

## 11. Game-End & Draw Determination Logic (`is_insufficient_material`)

To evaluate terminal states, the engine tracks draw indicators and piece structures:
* **75-Move Draw:** Automatically triggered if `halfmove_clock >= 150`.
* **50-Move Draw:** Claimable if `halfmove_clock >= 100`.
* **Stalemate & Checkmate:** Triggered if `moves.size() == 0` during legal move generation. It is checkmate if `is_in_check()` is true; otherwise, it is stalemate.
* **Insufficient Material Draw (`is_insufficient_material`):**
  Triggers a draw if neither side has sufficient material to force checkmate:
  1. Checks if any Pawns, Rooks, or Queens exist on the board:
     `if(bitboards[P] || bitboards[p] || bitboards[R] || bitboards[r] || bitboards[Q] || bitboards[q]) return false;`
  2. Counts knights and bishops using `__builtin_popcountll`:
     `total_pieces = 2 + white_knights + black_knights + white_bishops + black_bishops;`
  3. **King vs King (`total_pieces == 2`):** Returns true (draw).
  4. **King + Minor Piece vs King (`total_pieces == 3`):**
     Returns true if either side has exactly one bishop or one knight (draw).
  5. **King + Bishop vs King + Bishop (`total_pieces == 4`):**
     If both sides have exactly one bishop, the engine checks if they reside on the same square color. Square color parity is resolved via:
     `bool light = ((bishop_sq / 8) + (bishop_sq % 8)) % 2 != 0;`
     If `white_light == black_light` (both bishops are on light squares or both are on dark squares), checkmate is impossible; returns true (draw).

---

## 12. Search Optimization Roadmap

We will scale the engine's ELO from ~1200 to ~2200+ ELO by implementing search enhancements in the following order:

* **Stage 4: Move Ordering (MVV-LVA) — *Current Step***
  * Sort legal moves in `MoveGenerator` before search to check captures first. Checking captures first results in faster alpha-beta cutoffs and prunes the minimax tree.
  * **Formula:** `score = (victim_value * 10) + (10 - attacker_value)`
    * Victims: `P=10, N=20, B=30, R=40, Q=50, K=60`
    * Attackers: `P=6, N=5, B=4, R=3, Q=2, K=1`
* **Stage 5: Iterative Deepening**
  * Wraps search in a timed loop, exploring depth 1, then depth 2, then depth 3, etc., returning the best move found when the allocated time limit runs out.
* **Stage 6: Quiescence Search**
  * Prevents the "horizon effect" by continuing to search capture moves after `depth == 0` until the board position stabilizes.
* **Stage 7: Transposition Table**
  * Integrates Zobrist board hashing into the search tree to avoid re-evaluating duplicate transpositions.
* **Stage 8: Null Move Pruning + Late Move Reductions (LMR)**
  * Cuts branches early by checking if passing a turn yields a cutoff, and reduces depth for late quiet moves in well-ordered lists.
* **Stage 9: Aspiration Windows**
  * Reduces search times by searching within narrow alpha-beta windows around the previous ply's score.
* **Stage 10: Better Evaluation Details**
  * Incorporates pawn structures, king safety, and piece mobility calculations.
