# TigerFish 2.0 Comprehensive Architecture & Technical Reference

> **Audience**: Core developers, engine maintainers, and researchers.  
> **Status**: 100% Modern C++20 Data-Oriented Design (DOD).  
> **Source Directory**: `engine/`  

---

## 1. Introduction: Why TigerFish Was Rewritten

In TigerFish 1.0, the codebase followed classic Object-Oriented Programming (OOP):
- A monolithic `Board` class stored piece arrays, board history, unmake states, and attack arrays inside a single 328 KB object.
- Functions made deep copies of state, creating memory bandwidth bottlenecks.
- Move generation generated pseudo-legal moves that had to be tested by making the move, verifying if the king was left in check, and reverting it.
- Conditional branches on `side_to_move == WHITE` were evaluated repeatedly in every inner loop.

**TigerFish 2.0 is a complete ground-up rewrite** based on **Data-Oriented Design (DOD)**:
1. **Mechanical Sympathy**: Code is structured to match the CPU's L1/L2 cacheline layout and branch predictor.
2. **Compile-Time Branch Elimination**: Templates instantiate specialized White and Black functions so that the CPU never executes a runtime color branch in hot loops.
3. **Zero Dynamic Allocation**: Zero heap allocations (`malloc`, `new`, `std::vector::resize`) occur during search or move generation.
4. **Hardware Bitwise Intrinsics**: Uses single-cycle x86-64 BMI2 instructions (`_pext_u64`, `_tzcnt_u64`, `_blsr_u64`, `POPCNT`).

---

## 2. Source Code Architecture & File Layout

TigerFish compiles as a **Single Translation Unit (STU)** via `engine/main.cpp`.

```text
engine/
├── types.cpp        # Basic types (Color, Piece, Square, Bitboard, 16-bit Move struct)
├── bitboards.cpp    # Bitboard lookup tables, rays, line masks, and BMI2 PEXT sliders
├── zobrist.cpp      # SplitMix64 pseudo-random generator & incremental Zobrist hashes
├── position.cpp     # Board state, mailbox, stack-allocated StateInfo, make/undo move
├── movegen.cpp      # Templated strictly-legal move generator & leaf popcount
├── tt.cpp           # 64-byte cache-aligned lockless Transposition Table
├── eval.cpp         # Tapered handcrafted evaluation (PST, pawn structures, king safety)
├── search.cpp       # Search algorithms (SE, Conthist, Gravity, LMR, PVS, Lazy SMP)
├── perft.h          # Multithreaded work-stealing parallel perft engine
├── opening_book.cpp # Polyglot binary opening book parser
├── tablebase.cpp    # Endgame tablebase framework
├── rl_interface.cpp # Reinforcement learning / external evaluation hook
├── uci.cpp          # Universal Chess Interface protocol implementation
├── cli.cpp          # TigerFish CLI (interactive, analyze, perft, bench commands)
├── main.cpp         # Entry point and command dispatcher
└── profile_bench.cpp# Micro-benchmarks and profiler tools
```

---

## 3. The C++ Template Metaprogramming Concept Explained

One of the largest changes from the legacy engine is the extensive use of **Compile-Time Template Specialization** in [`engine/movegen.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/movegen.cpp).

### The Classical Problem
In chess, White moves "North" (+8 in square index) while Black moves "South" (-8 in square index). White promotes on Rank 8; Black promotes on Rank 1. White pawns start on Rank 2; Black pawns start on Rank 7.

In traditional code, every pawn step requires a condition:
```cpp
// OLD RUNTIME CHECK (Executed millions of times per second)
int dir = (pos.side_to_move == WHITE) ? 8 : -8;
Rank promo = (pos.side_to_move == WHITE) ? RANK_8 : RANK_1;
Bitboard single_push = (pawns << dir) & empty_squares; // Inefficient shift!
```

### The Modern Template Specialization
In TigerFish 2.0, we write the logic once as a template:
```cpp
template <Color Us, GenType Type>
inline void generate_moves_for_side(const Position& pos, MoveList& move_list) noexcept {
    constexpr Color Them = opponent_color(Us);
    constexpr Direction Up = (Us == WHITE) ? NORTH : SOUTH;
    constexpr Rank PromoRank = (Us == WHITE) ? RANK_8 : RANK_1;
    constexpr Rank StartRank = (Us == WHITE) ? RANK_2 : RANK_7;
    ...
```

### What Happens Behind the Scenes?
The compiler analyzes the code at compile time and generates **separate machine code routines**:

1. **`generate_moves_for_side<WHITE, ALL_MOVES>`**:
   - `Us` is replaced with `0`.
   - `Them` is replaced with `1`.
   - `Up` is replaced with `+8` (`shl rax, 8`).
   - `PromoRank` is replaced with constant bitmask `0xFF00000000000000ULL`.
   - **Zero conditional branches exist in the generated assembly!**

2. **`generate_moves_for_side<BLACK, ALL_MOVES>`**:
   - `Us` is replaced with `1`.
   - `Them` is replaced with `0`.
   - `Up` is replaced with `-8` (`shr rax, 8`).
   - `PromoRank` is replaced with constant bitmask `0x00000000000000FFULL`.
   - **Zero conditional branches exist in the generated assembly!**

3. **`generate_moves_for_side<WHITE, CAPTURES>` & `generate_moves_for_side<BLACK, CAPTURES>`**:
   - Used by Quiescence search (`qsearch`).
   - Because `Type == CAPTURES`, the compiler recognizes that all quiet move generation blocks are dead code and strips them completely from the binary!

### The Runtime Dispatcher
At runtime, only a single branch is evaluated at the start:
```cpp
inline void generate_legal_moves(const Position& pos, MoveList& move_list) noexcept {
    if (pos.side_to_move == WHITE) {
        generate_moves_for_side<WHITE, ALL_MOVES>(pos, move_list);
    } else {
        generate_moves_for_side<BLACK, ALL_MOVES>(pos, move_list);
    }
}
```
The CPU branch predictor learns this branch with near 100% accuracy, and the execution jumps into a streamlined, branch-free assembly routine.

---

## 4. Deep-Dive: Memory Layout & State Transitions

### `Position` Struct Layout (~140 Bytes)
`Position` contains only what is needed for board state queries:
```cpp
struct Position {
    Bitboard pieces[15];     // Bitboard per Piece enum (Pawns through Kings)
    Bitboard colors[2];      // Occupancy masks: White pieces and Black pieces
    Piece mailbox[64];       // Array mapping square (0-63) to Piece enum
    Color side_to_move = WHITE;
    int game_ply = 0;
    StateInfo* state = nullptr; // Pointer to current node on the call stack
};
```

### `StateInfo` Linked List (Zero-Allocation History)
Rather than maintaining an undo stack array on the heap, each recursive search frame allocates `StateInfo` on its local call stack:

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

    Bitboard checkers = 0; // Checkers attacking the friendly king
    Bitboard pinned = 0;   // Friendly pieces pinned to the king

    StateInfo* previous = nullptr; // Pointer to parent node
    Move move = MOVE_NONE;         // Move that brought us here
    Piece moving_piece = NO_PIECE; // Piece that moved (used by Continuation History)
};
```

### How `make_move` Works:
```cpp
inline void make_move(Position& pos, Move move, StateInfo& new_state) noexcept {
    // 1. Copy forward irreversible state fields
    new_state.castling_rights = pos.state->castling_rights;
    new_state.en_passant_square = SQ_NONE;
    new_state.halfmove_clock = pos.state->halfmove_clock + 1;
    new_state.zobrist_key = pos.state->zobrist_key ^ ZOBRIST_SIDE;
    new_state.previous = pos.state; // Link back to caller's frame

    // 2. Set pos.state pointer to new_state
    pos.state = &new_state;

    // 3. Move piece in mailbox & bitboards using XOR operations
    // 4. Update checkers bitboard for next side to move
}
```

When `undo_move` is called:
```cpp
inline void undo_move(Position& pos, Move move) noexcept {
    // Revert piece positions and uncapture if necessary
    ...
    // Restore pointer to caller's frame
    pos.state = pos.state->previous;
}
```
**Advantage**: Memory is automatically reclaimed when the function returns. Zero fragmentation, zero cache misses.

---

## 5. Transposition Table Architecture (`tt.cpp`)

The Transposition Table stores results of previously searched positions to avoid redundant tree evaluation.

### Cacheline Alignment (64 Bytes)
Modern CPU L1/L2 caches fetch memory in 64-byte cachelines. TigerFish packs 4 entries into exactly 64 bytes:

```cpp
struct alignas(16) TTClusterEntry {
    uint64_t key;  // Zobrist key XOR'd with data payload
    uint64_t data; // Bitfield: [move:16][score:16][eval:16][depth:8][meta:8]
};
static_assert(sizeof(TTClusterEntry) == 16);

struct alignas(64) TTCluster {
    TTClusterEntry entries[4]; // 4 * 16 = 64 bytes
};
static_assert(sizeof(TTCluster) == 64);
```

### Lockless Multi-Threaded Safety
When 24 threads search simultaneously, locking mutexes on every TT probe kills performance.
TigerFish uses **Key-Data XOR Verification**:
- When storing: `entry.key = zobrist_key ^ entry.data;`
- When probing:
  ```cpp
  uint64_t read_key = entry.key;
  uint64_t read_data = entry.data;
  if ((read_key ^ read_data) == expected_zobrist_key) {
      // 100% valid! No thread modified this entry while we were reading.
  }
  ```
If a writer thread writes to the entry mid-probe, the XOR sum will not equal `expected_zobrist_key`. The reader simply discards the entry as a cache miss without crashing or using corrupted data!

---

## 6. Modern Search Algorithms Explained (`search.cpp`)

### 1. Singular Extensions (SE)
- **Problem**: Alpha-beta pruning might cut off a move prematurely if the branch has sharp tactics that only work for one specific response.
- **Solution**: If the TT move is significantly better than any other move in the position, extend search depth along that move by +1 ply (or +2 plies for double extension).
- **Check**: Run a reduced search excluding the TT move. If no alternative move can achieve $\beta = \text{tt\_score} - 2 \times \text{depth}$, the TT move is declared singular.

### 2. Continuation History (Conthist)
- **Problem**: Standard history heuristics only remember which move was good overall, but a move's strength depends on the opponent's previous move.
- **Solution**: A 3-dimensional table `cont_history[15][64][64]` tracks move success conditioned on:
  - What piece the opponent just moved (`prev_moving_piece`)
  - Where the opponent moved to (`prev_to_sq`)
  - Where our piece moves to (`curr_to_sq`)
- This boosts natural tactical countermoves to the front of the move list.

### 3. History Gravity with Malus
- **Gravity Formula**: Keeps history counters stable and prevents numerical overflow:
  $$\text{history} \leftarrow \text{history} + \text{bonus} - \frac{\text{history} \times |\text{bonus}|}{16384}$$
- **Malus**: When a quiet move produces a beta cutoff:
  - The winning move gets `+bonus`.
  - All previously examined quiet moves in that position that failed low receive `-bonus`.
  - Suboptimal moves are quickly penalized so Late Move Pruning can skip them in subsequent searches.

### 4. Exponential Aspiration Windows
- Instead of searching $[-\infty, +\infty]$ at each iterative deepening depth, search a narrow window around the previous depth's score: $[\text{score} - 25, \text{score} + 25]$.
- If search fails low (score $\le \alpha$) or fails high (score $\ge \beta$), widen the window by $50\%$ ($\delta \leftarrow \delta + \delta / 2$).
- Avoids searching the full tree while recovering quickly when evaluations shift.

### 5. Improving-Flag LMR Scaling
- Late Move Reductions (LMR) reduce search depth on moves searched late in the move order.
- **Improving Check**: The position is improving if static eval at the current ply is greater than or equal to static eval two plies ago (`static_eval >= eval_history[ply - 2]`).
- **Scaling**:
  - If **improving**: Increase reduction by 1 (`reduction++`). Since the position is already winning, opponent candidate moves can be pruned more aggressively.
  - If **not improving**: Decrease reduction by 1 (`reduction--`). When defending or facing threats, search alternative moves more carefully.

---

## 7. Operational Workflow & Command Reference

### Compilation
```bash
g++ -O3 -march=native -std=c++20 -pthread engine/main.cpp -o game.exe
```

### Modes of Operation
1. **UCI Mode (Default)**:
   ```bash
   ./game.exe uci
   ```
   Standard protocol for GUIs (Lichess, Arena, Banksia, Cutechess).

2. **Interactive Protocol Mode**:
   ```bash
   ./game.exe interactive
   ```
   Commands:
   - `newgame [fen]`: Resets board state and clears TT.
   - `apply <uci_move>`: Plays a move on the internal board (e.g. `apply e2e4`).
   - `undo`: Takes back the last move.
   - `best <depth> [time_ms]`: Computes best move within depth and time constraints.
   - `status`: Outputs current game state (`ongoing`, `checkmate_white_wins`, `draw`, etc.).
   - `fen`: Outputs current FEN string.

3. **Standalone JSON Analysis Mode**:
   ```bash
   ./game.exe analyze "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" --depth 12 --threads 4
   ```
   Outputs structured JSON with bestMove, evaluation, depth, nodes, timeMs, NPS, and PV array.

4. **Multi-Threaded Perft Benchmark**:
   ```bash
   ./game.exe perft bench
   ```
   Executes the 8 standard perft regression positions across 1, 16, and 24 threads.

5. **Engine Search Benchmark**:
   ```bash
   ./game.exe bench 12 4
   ```
   Standardized 5-position benchmark measuring search depth, nodes, and NPS.

---

*TigerFish 2.0 Comprehensive Reference — Maintained in `docs/ENGINE_ARCHITECTURE_MANUAL.md`.*
