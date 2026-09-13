# TigerFish 2.0 Chess Engine — Operations & Architecture Manual

> **Document Classification**: Definitive Engine Engineering Manual & Operational Specification  
> **Target Project**: TigerFish C++20 Chess Engine  
> **Build Command**: `g++ -O3 -march=native -std=c++20 -pthread engine/main.cpp -o game.exe`  
> **Authoritative Root**: `engine/` (Single Translation Unit)  

---

## Table of Contents
1. [Executive Overview: From Legacy Monolith to Modern DOD](#1-executive-overview-from-legacy-monolith-to-modern-dod)
2. [Architectural Blueprint & Inclusion Cascade](#2-architectural-blueprint--inclusion-cascade)
3. [Core Primitives & Compact Binary Encodings (`types.cpp`)](#3-core-primitives--compact-binary-encodings-typescpp)
4. [Hardware Bitboard Acceleration & BMI2/PEXT Sliders (`bitboards.cpp`)](#4-hardware-bitboard-acceleration--bmi2pext-sliders-bitboardscpp)
5. [Position State & Zero-Allocation Stack History (`position.cpp`, `zobrist.cpp`)](#5-position-state--zero-allocation-stack-history-positioncpp-zobristcpp)
6. [Compile-Time Template Specialization & Legal Movegen (`movegen.cpp`)](#6-compile-time-template-specialization--legal-movegen-movegencpp)
7. [Lockless Cache-Aligned Transposition Table (`tt.cpp`)](#7-lockless-cache-aligned-transposition-table-ttcpp)
8. [Tapered Static Evaluation & Pawn Structure (`eval.cpp`)](#8-tapered-static-evaluation--pawn-structure-evalcpp)
9. [Tournament-Grade Search Heuristics & Modern Pruning (`search.cpp`)](#9-tournament-grade-search-heuristics--modern-pruning-searchcpp)
10. [Multithreaded Work-Stealing Perft Engine (`perft.h`)](#10-multithreaded-work-stealing-perft-engine-perfth)
11. [Protocols, CLI & Lichess Bridge (`uci.cpp`, `cli.cpp`, `lichess_bot.py`)](#11-protocols-cli--lichess-bot-bridge-ucicpp-clicpp-lichess_botpy)
12. [Verification Suite, Diagnostics & Troubleshooting Runbook](#12-verification-suite-diagnostics--troubleshooting-runbook)

---

## 1. Executive Overview: From Legacy Monolith to Modern DOD

In earlier iterations of TigerFish (v1.x), the codebase relied on classic object-oriented programming (OOP) paradigms:
- A heavy `Board` class (~328 KB) encapsulating deep heap-allocated undo state buffers (`UndoState history[8192]`).
- Bulky 512-byte `PinInfo` structs passed across function boundaries by value.
- Ray-casting move generation that required post-move legality filters (generating pseudolegal moves, making the move, verifying if the king was left in check, and reverting).
- High branch misprediction rates caused by runtime `if (side == WHITE)` evaluations nested inside innermost loops.

### The TigerFish 2.0 Paradigm Shift
TigerFish 2.0 completely supersedes the legacy monolith with **Data-Oriented Design (DOD)** and **Hardware-Conscious C++20**:
1. **Compact Flat Structs**: The core board representation (`Position`) is reduced to ~140 bytes of contiguous bitboards and a 64-byte piece mailbox.
2. **Zero Dynamic Memory Allocation**: The entire engine executes with zero `new`, `malloc`, or dynamic vector resizing in hot calculation paths. Move reversibility is handled through a stack-allocated linked list (`StateInfo`).
3. **Compile-Time Branch Elimination**: C++ template metaprogramming (`template <Color Us, GenType Type>`) generates specialized machine code for White and Black, completely erasing runtime color checks from hot movegen and search loops.
4. **Hardware Acceleration**: Exploits modern CPU hardware instructions (`_pext_u64`, `_tzcnt_u64`, `_blsr_u64`, `__builtin_popcountll`) for single-cycle slider attack calculation and bitboard scans.
5. **Lockless Concurrency**: Shared Transposition Table clusters are 64-byte aligned (matching CPU cachelines) with XOR checksum validation, achieving 100% thread safety across 24+ cores with zero mutex overhead.

---

## 2. Architectural Blueprint & Inclusion Cascade

TigerFish compiles as a **Single Translation Unit (STU)**. Instead of compiling multiple `.cpp` files into intermediate `.o` object files and linking them, all modules are included linearly into `engine/main.cpp`.

### Why Single Translation Unit?
- **Inter-procedural Inlining**: The compiler has global visibility across all functions, structs, and lookup tables. It aggressively inlines leaf functions (e.g., `make_move`, `popcount`, `score_move`) directly into call sites, eliminating function call frame overhead.
- **Link-Time Elimination**: Unreferenced code paths and constants are stripped during compilation.
- **Build Simplicity**: A single `g++` invocation produces the production binary without complex Makefiles or CMake dependency graphs.

```text
Layer 5: CLI, UCI Protocol & Interactive Session
         engine/cli.cpp         engine/uci.cpp
               │                      │
Layer 4: Search Engine & Multithreaded Concurrency
         engine/search.cpp  (PVS, SE, Conthist, Gravity, LMR, Lazy SMP)
               │
Layer 3: Evaluation, Transposition Table & Peripheral Databases
         engine/eval.cpp        engine/tt.cpp
         engine/opening_book.cpp engine/tablebase.cpp   engine/rl_interface.cpp
               │
Layer 2: Compile-Time Legal Move Generation & Hardware Perft
         engine/movegen.cpp     engine/perft.h
               │
Layer 1: Position Mutation & Stack-Allocated State
         engine/position.cpp    engine/zobrist.cpp
               │
Layer 0: Core Types, Primitives & Hardware Slider Bitboards
         engine/types.cpp       engine/bitboards.cpp
```

### Strict Downward Dependency Rule
Dependencies strictly flow downwards. A lower layer (e.g., `bitboards.cpp` or `position.cpp`) is NEVER aware of higher layers (e.g., `search.cpp` or `cli.cpp`). Circular includes are physically impossible in this cascade.

---

## 3. Core Primitives & Compact Binary Encodings (`types.cpp`)

[`engine/types.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/types.cpp) defines the mathematical and memory primitives that underpin every calculation in the engine.

### Square Mapping (LERF: Little-Endian Rank-File)
Squares are indexed as an 8-bit unsigned integer (`0` to `63`):
```text
  8 ┆ 56 57 58 59 60 61 62 63    (a8 to h8)
  7 ┆ 48 49 50 51 52 53 54 55
  6 ┆ 40 41 42 43 44 45 46 47
  5 ┆ 32 33 34 35 36 37 38 39
  4 ┆ 24 25 26 27 28 29 30 31
  3 ┆ 16 17 18 19 20 21 22 23
  2 ┆  8  9 10 11 12 13 14 15
  1 ┆  0  1  2  3  4  5  6  7    (a1 to h1)
    └─────────────────────────
       a  b  c  d  e  f  g  h
```
- Square index calculation: `sq = (rank << 3) | file`
- File extraction: `file = sq & 7`
- Rank extraction: `rank = sq >> 3`

### Piece & Color Encodings
```cpp
enum Color : uint8_t { WHITE = 0, BLACK = 1, COLOR_NONE = 2 };

enum PieceType : uint8_t {
    PT_NONE = 0, PT_PAWN = 1, PT_KNIGHT = 2,
    PT_BISHOP = 3, PT_ROOK = 4, PT_QUEEN = 5, PT_KING = 6
};

enum Piece : uint8_t {
    NO_PIECE = 0,
    W_PAWN = 1, W_KNIGHT = 2, W_BISHOP = 3, W_ROOK = 4, W_QUEEN = 5, W_KING = 6,
    B_PAWN = 9, B_KNIGHT = 10, B_BISHOP = 11, B_ROOK = 12, B_QUEEN = 13, B_KING = 14
};
```
- **Piece Math**: A piece's value is packed as `(color << 3) | piece_type`.
  - Color from piece: `piece >> 3` (e.g., `B_ROOK (12) >> 3 = 1` -> `BLACK`).
  - PieceType from piece: `piece & 7` (e.g., `B_ROOK (12) & 7 = 4` -> `PT_ROOK`).
  - Opponent color: `c ^ 1` (instantaneous 1-cycle toggle).

### Compact 16-Bit Move Layout (`struct Move`)
Every chess move in TigerFish occupies exactly 16 bits (`uint16_t raw_data`), avoiding memory bus traffic:

```text
 15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
┌──────┬───────┬───────────────────────┬───────────────────────┐
│ Special Flags│     To Square (0-63)  │    From Square (0-63) │
└──────┴───────┴───────────────────────┴───────────────────────┘
```
- **Bits 0–5 (0x3F)**: Source square (`from`).
- **Bits 6–11 (0x3F << 6)**: Destination square (`to`).
- **Bits 12–15 (0xF000)**: Move flags:
  - `0x0000`: Quiet move
  - `0x1000`: Double pawn push (sets en passant target)
  - `0x2000`: Kingside castle
  - `0x3000`: Queenside castle
  - `0x4000`: Standard capture
  - `0x5000`: En passant capture
  - `0x8000–0xB000`: Quiet promotion (Knight, Bishop, Rook, Queen)
  - `0xC000–0xF000`: Capture promotion (Knight, Bishop, Rook, Queen)

### Hardware Bit Intrinsics
The engine leverages hardware x86-64 bit manipulation extensions:
- **`popcount(Bitboard mask)`**: Returns the number of set bits via hardware instruction `__builtin_popcountll` (`POPCNT`).
- **`lsb(Bitboard mask)`**: Returns the least significant set bit index using `_tzcnt_u64` (`TZCNT`).
- **`pop_lsb(Bitboard& mask)`**: Isolates and clears the lowest set bit in a single CPU cycle using `_blsr_u64(mask)` (`BLSR`).

---

## 4. Hardware Bitboard Acceleration & BMI2/PEXT Sliders (`bitboards.cpp`)

[`engine/bitboards.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/bitboards.cpp) replaces slow loop-based ray casting with 64-bit parallel bitboards (`uint64_t`).

### Modern PEXT Sliders vs. Classical Magic Bitboards
Sliding pieces (Rooks, Bishops, Queens) have their attacks blocked by other pieces. Classical engines use Magic Bitboard multiplication (`(occupancy * magic) >> shift`) to generate an index into an attack array. However, magic bitboards:
1. Suffer from sparse lookup tables (~2.3 MB).
2. Require trial-and-error generation of magic multipliers.
3. Waste L2/L3 CPU cache lines.

TigerFish uses **BMI2 Hardware Parallel Bit Extract (`_pext_u64`)**:
```cpp
inline Bitboard rook_attacks(Square sq, Bitboard occ) noexcept {
#if defined(__BMI2__)
    return ROOK_ATTACK_TABLE[ROOK_OFFSETS[sq] + _pext_u64(occ, ROOK_MASKS[sq])];
#else
    // Magic Bitboard Fallback
    occ &= ROOK_MASKS[sq];
    unsigned int idx = static_cast<unsigned int>((occ * ROOK_MAGICS[sq]) >> ROOK_SHIFTS[sq]);
    return ROOK_ATTACK_TABLE[ROOK_OFFSETS[sq] + idx];
#endif
}
```
- `_pext_u64(occ, mask)` compresses the relevant occupancy bits directly into a contiguous integer index in a single clock cycle.
- **Dense Packing**: Total slider attack memory is compressed under 850 KB, fitting entirely within CPU L2 cache!

### Directional Lines and Rays
- `LINE_BB[sq1][sq2]`: A bitboard containing the entire infinite line connecting two squares (if they share a rank, file, or diagonal). Used to constrain pinned pieces to their legal pinning ray in $O(1)$ time.
- `BETWEEN_BB[sq1][sq2]`: A bitboard containing only the squares strictly situated between `sq1` and `sq2`. Used to generate check-evasion interception masks in $O(1)$ time.

---

## 5. Position State & Zero-Allocation Stack History (`position.cpp`, `zobrist.cpp`)

[`engine/position.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/position.cpp) represents the chessboard state and handles state transitions (`make_move` / `undo_move`).

### The `Position` Struct (~140 Bytes)
```cpp
struct Position {
    Bitboard pieces[15];     // Bitboard per Piece enum (W_PAWN through B_KING)
    Bitboard colors[2];      // Occupancy masks: colors[WHITE] and colors[BLACK]
    Piece mailbox[64];       // Fast O(1) piece-on-square array
    Color side_to_move = WHITE;
    int game_ply = 0;
    StateInfo* state = nullptr; // Pointer to current stack-allocated state node
};
```

### The Linked-List `StateInfo` (Zero Heap Allocation)
Every state change that cannot be easily reconstructed (captured piece, castling rights, en passant square, 50-move counter, Zobrist key) is stored in a `StateInfo` struct.

```text
Stack Frame Depth 0:  StateInfo st0  (allocated on search stack)
                           ▲
Stack Frame Depth 1:  StateInfo st1  (st1.previous = &st0)
                           ▲
Stack Frame Depth 2:  StateInfo st2  (st2.previous = &st1)
```

```cpp
struct StateInfo {
    Piece captured_piece = NO_PIECE;
    uint8_t castling_rights = CASTLE_NONE;
    Square en_passant_square = SQ_NONE;
    uint8_t halfmove_clock = 0;
    uint64_t zobrist_key = 0;
    uint64_t pawn_zobrist_key = 0;
    int eval_mg = 0;
    int eval_eg = 0;
    int game_phase = 0;

    Bitboard checkers = 0;
    Bitboard pinned = 0;

    StateInfo* previous = nullptr; // Links to parent node
    Move move = MOVE_NONE;          // Move that led to this state
    Piece moving_piece = NO_PIECE;  // Piece that moved
};
```

### Execution Flow of `make_move` and `undo_move`
When a move is executed during search:
1. `StateInfo new_state;` is declared on the local C++ call stack.
2. `make_move(pos, move, new_state)` updates bitboards, moves pieces in the mailbox, updates the Zobrist hash incrementally, links `new_state.previous = pos.state`, and sets `pos.state = &new_state`.
3. The recursive search runs down the tree.
4. When backtracking, `undo_move(pos, move)` reverts piece positions, restores captured pieces, and restores `pos.state = pos.state->previous`.
5. **No memory allocation, no copying of arrays, 100% cache-local!**

---

## 6. Compile-Time Template Specialization & Legal Movegen (`movegen.cpp`)

[`engine/movegen.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/movegen.cpp) contains one of the most sophisticated optimization concepts in the engine: **Compile-Time Branch Elimination through C++ Templates**.

### The Problem with Traditional Move Generation
In standard chess engines, move generation constantly branches on the active color:
```cpp
// TRADITIONAL RUNTIME BRANCHING (SLOW)
if (pos.side_to_move == WHITE) {
    pawn_forward = 8;
    promo_rank = RANK_8;
    ...
} else {
    pawn_forward = -8;
    promo_rank = RANK_1;
    ...
}
```
Inside tree searches evaluating millions of nodes per second, these branching checks pollute the CPU instruction pipeline and cause branch mispredictions.

### The Template Solution: `template <Color Us, GenType Type>`
TigerFish turns `Color` and `GenType` into **compile-time template parameters**:

```cpp
template <Color Us, GenType Type>
inline void generate_moves_for_side(const Position& pos, MoveList& move_list) noexcept {
    constexpr Color Them = opponent_color(Us);
    constexpr Direction Up = (Us == WHITE) ? NORTH : SOUTH;
    constexpr Rank PromoRank = (Us == WHITE) ? RANK_8 : RANK_1;
    constexpr Rank StartRank = (Us == WHITE) ? RANK_2 : RANK_7;
    ...
```

#### How the Compiler Specializes Code
When compiled, C++ instantiates completely separate functions in machine code:
1. `generate_moves_for_side<WHITE, ALL_MOVES>`
2. `generate_moves_for_side<BLACK, ALL_MOVES>`
3. `generate_moves_for_side<WHITE, CAPTURES>`
4. `generate_moves_for_side<BLACK, CAPTURES>`

Inside each specialized function:
- `constexpr Color Them` is hardcoded as an immediate value (no variable lookup).
- `Up` is a constant shift (+8 for White, -8 for Black). The compiler emits direct bitwise shift instructions (`shl` or `shr`) without any conditional jumps!
- If `Type == CAPTURES` (used by Quiescence search), quiet move generation code is completely omitted by the compiler dead-code elimination.

#### Dispatch Wrapper
At runtime, only a single outer branch determines which templated function to execute:
```cpp
inline void generate_legal_moves(const Position& pos, MoveList& move_list) noexcept {
    if (pos.side_to_move == WHITE) {
        generate_moves_for_side<WHITE, ALL_MOVES>(pos, move_list);
    } else {
        generate_moves_for_side<BLACK, ALL_MOVES>(pos, move_list);
    }
}
```

### Strictly Legal Generation: Checks and Pins
TigerFish never generates pseudolegal moves that leave the king in check:
1. **Double Check (`popcount(checkers) >= 2`)**: Only King moves are generated (`KING_ATTACKS[ksq] & ~attacked_squares`).
2. **Single Check (`popcount(checkers) == 1`)**: Non-king pieces are restricted to a `target_mask = checkers_bb | BETWEEN_BB[ksq][checker_sq]`. Any move that does not capture the attacker or block the check is discarded before generation.
3. **Pinned Pieces**: `pos.state->pinned` stores all pieces pinned to the king. A pinned piece's destination square is masked with `LINE_BB[ksq][from_sq]`. If a pinned piece cannot move along that line, it generates zero moves.

### Leaf Popcount Optimization (`count_legal_moves`)
At the final ply (depth 1) of Perft, generating and storing 16-bit `Move` structs into an array is pure waste.
`count_legal_moves(pos)` bypasses move list generation entirely:
- Generates attack bitboards directly into CPU registers.
- Computes move counts via `popcount(attacks & target_mask)`.
- Reaches up to **8.06 Billion NPS** in multi-threaded perft!

---

## 7. Lockless Cache-Aligned Transposition Table (`tt.cpp`)

[`engine/tt.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/tt.cpp) implements a high-speed, lockless Transposition Table (TT).

### The 64-Byte Cacheline Cluster Architecture
CPU caches read and write RAM in 64-byte chunks (cachelines). If a struct crosses a 64-byte boundary, accessing it requires two DRAM reads instead of one.

TigerFish packs exactly **4 TT entries into a 64-byte cluster**:
```cpp
struct alignas(16) TTClusterEntry {
    uint64_t key;  // Zobrist key XOR'd with packed data payload
    uint64_t data; // [move:16][score:16][eval:16][depth:8][meta:8]
};
static_assert(sizeof(TTClusterEntry) == 16);

struct alignas(64) TTCluster {
    TTClusterEntry entries[4]; // 4 * 16 = 64 bytes (Exactly 1 CPU Cacheline!)
};
```

```text
┌───────────────────────────────────────────────────────────────┐
│                 64-Byte CPU Cacheline Cluster                 │
├───────────────┬───────────────┬───────────────┬───────────────┤
│ Entry 0 (16B) │ Entry 1 (16B) │ Entry 2 (16B) │ Entry 3 (16B) │
└───────────────┴───────────────┴───────────────┴───────────────┘
```

### Lockless Multi-Threaded Concurrency (Torn Read Immunity)
In multi-threaded Lazy SMP search, multiple threads read and write to the same cluster simultaneously without mutexes.
To prevent "torn reads" (where thread A reads half-written data from thread B):
1. **Store**: The writer stores `entry.key = zobrist_key ^ entry.data`.
2. **Probe**: The reader reads `key` and `data`. It checks:
   ```cpp
   if ((entry.key ^ entry.data) == current_zobrist_key) {
       // Valid read! Data was NOT corrupted mid-write.
   }
   ```
   If a writer modified the entry midway, the XOR checksum will fail, and the entry is safely ignored as a cache miss without any crash or corrupted move!

### Power-of-2 Bitmask Indexing
The table size is allocated as a power of 2 ($2^N$ clusters).
- Traditional indexing: `idx = key % num_clusters` (executes slow `idiv` instruction, ~30–40 CPU cycles).
- TigerFish indexing: `idx = key & (cluster_count - 1)` (single-cycle bitwise `AND` instruction, 1 CPU cycle).

---

## 8. Tapered Static Evaluation & Pawn Structure (`eval.cpp`)

[`engine/eval.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/eval.cpp) evaluates non-terminal board states using a tapered evaluation framework.

### Game Phase Blending
Evaluation transitions smoothly between Middlegame (MG) and Endgame (EG) based on total non-pawn material:
- Game phase weights: Knight = 1, Bishop = 1, Rook = 2, Queen = 4.
- Total phase value ranges from `0` (bare pawns/kings) to `24` (full opening board).
- Blended score formula:
  $$\text{score} = \frac{(\text{score}_{MG} \times \text{phase}) + (\text{score}_{EG} \times (24 - \text{phase}))}{24}$$

### Evaluation Components
1. **Piece-Square Tables (PST)**: Tuned positional tables for every piece on all 64 squares. Incremental updates maintain PST scores in $O(1)$ time inside `make_move` and `undo_move`.
2. **Bishop Pair**: +35 cp MG / +50 cp EG awarded for maintaining both bishops.
3. **Rook Open & Semi-Open Files**:
   - Fully open file (no White or Black pawns): +25 cp MG / +15 cp EG.
   - Semi-open file (no friendly pawns): +12 cp MG / +8 cp EG.
4. **Pawn Structure & Dedicated Pawn Cache (`PAWN_TABLE[65536]`)**:
   - Doubled pawns penalty (-15 MG / -25 EG).
   - Isolated pawns penalty (-15 MG / -20 EG).
   - Passed pawns bonus scaling exponentially with rank (up to +100 MG / +160 EG on 7th rank).
   - Evaluated pawn structures are cached in a dedicated 65,536-entry pawn transposition table.
5. **King Safety (Middlegame)**: Evaluates pawn shields directly in front of castled kings. Open files adjacent to the king incur severe tactical penalties.
6. **Tempo Bonus**: +20 cp granted to the side to move to represent the initiative.

---

## 9. Tournament-Grade Search Heuristics & Modern Pruning (`search.cpp`)

[`engine/search.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/search.cpp) is the calculation core. It implements state-of-the-art non-NNUE algorithms designed to maximize tactical accuracy while aggressively cutting hopeless branches.

### A. Singular Extensions (SE)
- **Concept**: At non-root nodes with `depth >= 8`, when the TT move has a lower bound or exact score, we test if that move is uniquely superior.
- **Method**: Run a reduced null-window search at depth $(d - 1) / 2$ with an artificially reduced threshold $\beta = \text{tt\_score} - 2 \times d$, excluding the TT move from search.
- **Extension**: If no alternative move can achieve $\beta$, the TT move is singular. The search depth is extended by $+1$ ply (or $+2$ for double extensions). This focuses deep search effort along forced, critical lines.
- **Multi-Cut**: If an alternative move scores $\ge \beta$, the node fails high immediately without searching further.

### B. Continuation History (Conthist)
- **Concept**: Moves are evaluated in context with the previous opponent move.
- Keyed as a 3D table `cont_history[15][64][64]` (`[prev_moving_piece][prev_to_sq][curr_to_sq]`).
- During move scoring, countermoves that historically answered the opponent's previous maneuver receive a strong priority boost.

### C. History Gravity with Malus
- **Gravity Formula**: Replaced unbounded history scores with a bounded formula that caps memory inflation:
  $$\text{history} \leftarrow \text{history} + \text{bonus} - \frac{\text{history} \times |\text{bonus}|}{16384}$$
- **History Malus**: When a quiet move triggers a $\beta$-cutoff:
  - The winning move is awarded `+bonus`.
  - **All earlier quiet moves that failed low in that same node are penalized with `-bonus`.**
  - Suboptimal moves rapidly sink below Late Move Pruning thresholds.

### D. Exponential Aspiration Windows
- Initial search window centered around previous iteration score: $[\text{score} - 25, \text{score} + 25]$.
- On fail-low or fail-high, widen the window exponentially ($\delta \leftarrow \delta + \delta / 2$).
- Opens to $(-\infty, +\infty)$ only if $\delta > 500$ cp. Prevents wasteful full-window re-searches.

### E. Improving-Flag Late Move Reductions (LMR)
- Quiet moves searched late in the move order (beyond index 3) have search depth reduced using a precomputed logarithmic table `LMR_TABLE[depth][move_index]`.
- **Dynamic Scaling**:
  - If position is **improving** (`static_eval >= eval_history[ply - 2]`): `reduction++` (prunes more aggressively when comfortably winning).
  - If position is **not improving**: `reduction--` (searches defensive lines more deeply).

### F. Complete Pruning Cascade (Top to Bottom)
1. **Transposition Table Cutoff**: Exact or bound-exceeding score at $\ge \text{depth}$ returns immediately.
2. **Reverse Futility Pruning (RFP)**: At depth $\le 8$, if static eval minus margin exceeds $\beta$, return eval.
3. **Null Move Pruning (NMP)**: If side to move can pass their turn and still exceed $\beta$, cutoff branch with reduction $R = 4 + d / 4$.
4. **Razor Pruning**: At depth $\le 3$, if static eval is hopeless, drop directly into quiescence search.
5. **Futility Pruning**: At depth $\le 3$, prune quiet moves that cannot raise alpha even with material gain.
6. **Late Move Pruning (LMP)**: At shallow depths, skip quiet moves beyond a threshold count.
7. **SEE Capture Pruning**: In quiescence search, discard losing trades (e.g., Queen capturing defended Pawn) using Static Exchange Evaluation.

### G. Staged Move Ordering
1. TT Best Move
2. Good Captures (ordered by MVV-LVA / SEE $\ge 0$)
3. Killer Moves (1st and 2nd killer from same ply)
4. Countermoves (from continuation history)
5. Quiet Moves (ordered by Butterfly History + Continuation History)
6. Bad Captures (SEE $< 0$)

---

## 10. Multithreaded Work-Stealing Perft Engine (`perft.h`)

[`engine/perft.h`](file:///d:/PROJECTS/chess_engine_v0/engine/perft.h) is TigerFish's move-generation validation and stress-testing system.

- **Work-Stealing Architecture**: Traverses the root position to ply 2–3, accumulating leaf tasks into a flat vector.
- **Adaptive Chunking**: Worker threads dynamically claim batches of 16 down to 1 task via `fetch_add`, eliminating atomic bus contention on high-core CPUs (e.g., 24-core i9/Ryzen).
- **Perft Transposition Table (`PerftTT`)**: Caches perft counts by `(key, occ, depth)` across all threads, multiplying throughput.
- **Windows Job Object Affinity Fix**: Automatically checks `GetProcessAffinityMask` before binding threads, ensuring complete stability inside sandboxed containers or IDE environments.

---

## 11. Protocols, CLI & Lichess Bridge (`uci.cpp`, `cli.cpp`, `lichess_bot.py`)

### Command Modes
- **UCI Mode (`./game.exe uci` or `./game.exe`)**: Standard Universal Chess Interface protocol for GUI integration (Lichess, Arena, Cutechess).
- **Interactive Mode (`./game.exe interactive`)**: Persistent bidirectional protocol used by `lichess_bot.py`:
  - `newgame [fen]`: Initializes fresh board and clears TT.
  - `apply <uci>`: Advances position by applying move.
  - `undo`: Steps back one half-move.
  - `best <depth> [time_ms]`: Searches and outputs best move and evaluation.
- **JSON Analysis Mode (`./game.exe analyze "<fen>" --depth D --threads T`)**: Standalone structured JSON output for web backends and automated analysis pipelines.
- **Benchmark Suite (`./game.exe bench [depth] [threads]`)**: Multi-position engine benchmark reporting time, nodes, and NPS.
- **Perft Suite (`./game.exe perft bench`)**: Runs the 8 standard perft regression positions across 1, 16, and 24 threads.

### Lichess Bot Architecture (`lichess_bot.py` & `lichess_multibot.py`)
- Communicates via non-blocking asynchronous pipes (`asyncio.subprocess`).
- **Dynamic Clock Management**:
  $$\text{budget} = \frac{\text{clock\_remaining} - \text{lag\_buffer}}{25.0} + (\text{increment} \times 0.75)$$
  Features low-time scramble modes and panic thresholds ($< 800$ ms) to prevent flagging under lag.

---

## 12. Verification Suite, Diagnostics & Troubleshooting Runbook

### Primary Verification Commands

```bash
# 1. Compile production engine
g++ -O3 -march=native -std=c++20 -pthread engine/main.cpp -o game.exe

# 2. Run full 87-case regression test harness
python Test/regression/run_all_tests.py

# 3. Run multi-threaded perft benchmark suite (1, 16, 24 threads)
.\game.exe perft bench

# 4. Verify Lichess bot startup and clock management preflight
python test_bot_startup.py

# 5. Execute 24-threaded engine search benchmark
.\game.exe bench 12 24
```

### Troubleshooting Runbook

| Symptom | Probable Cause | Corrective Action |
|---|---|---|
| **Crash with `0xC0000005` at startup** | Static table alignment or MinGW PE-COFF section misalignment under ASLR | Ensure no global/static tables use `alignas(64)`. Use standard 8/16-byte alignment. |
| **Thread termination with exit code 1** | Invalid thread affinity mask under Windows Job Object | Verify that thread mask is guarded by `GetProcessAffinityMask` in `perft.h` and `search.cpp`. |
| **Movegen perft mismatch** | En passant discovery check or castling square clearance | Run `python Test/perft/run_15_position_perft_stress_suite.py` to identify failing FEN and ply. |
| **Lichess Bot disconnects or flags** | Subprocess pipe buffer saturation or excessive time budget | Verify `test_bot_startup.py` passes all clock safety caps ($< 25\%$ remaining clock). |

---

*TigerFish 2.0 Architecture & Operations Manual — Maintained in [`docs/`](file:///d:/PROJECTS/chess_engine_v0/docs/) and root [`OPERATIONS_MANUAL.md`](file:///d:/PROJECTS/chess_engine_v0/OPERATIONS_MANUAL.md).*
