# TigerFish 2.0 Chess Engine & Lichess Bridge — Operations Manual

> **Audience**: Developers maintaining, optimizing, or extending the engine or Lichess bot bridge.  
> **Build**: `g++ -O3 -march=native -std=c++20 -pthread engine/main.cpp -o game.exe`  
> **Entry Point**: `engine/main.cpp` dispatches between UCI protocol and TigerFish CLI/JSON commands.

---

## Table of Contents
1. [Architecture & Layer Hierarchy](#1-architecture--layer-hierarchy)
2. [Include Cascade & Single Translation Unit](#2-include-cascade--single-translation-unit)
3. [Data Representations & Encoding Conventions](#3-data-representations--encoding-conventions)
4. [Bitboard Attack Calculation & PEXT Sliders (`bitboards.cpp`)](#4-bitboard-attack-calculation--pext-sliders-bitboardscpp)
5. [Board State & Stack-Linked History (`position.cpp`)](#5-board-state--stack-linked-history-positioncpp)
6. [Legal Move Generation & Bitboard Leaf Popcount (`movegen.cpp`)](#6-legal-move-generation--bitboard-leaf-popcount-movegencpp)
7. [Lockless Cache-Aligned Transposition Table (`tt.cpp`)](#7-lockless-cache-aligned-transposition-table-ttcpp)
8. [Tapered Static Evaluation (`eval.cpp`)](#8-tapered-static-evaluation-evalcpp)
9. [Search Engine & Parallelism (`search.cpp`)](#9-search-engine--parallelism-searchcpp)
10. [High-Performance Parallel Perft Engine (`perft.h`)](#10-high-performance-parallel-perft-engine-perfth)
11. [CLI, UCI & Lichess Bot Bridge (`cli.cpp`, `uci.cpp`, `lichess_bot.py`)](#11-cli-uci--lichess-bot-bridge-clicpp-ucicpp-lichess_botpy)
12. [Performance Profile & Verification Matrix](#12-performance-profile--verification-matrix)

---

## 1. Architecture & Layer Hierarchy

TigerFish 2.0 is built on **Data-Oriented Design (DOD)** principles, completely replacing heavy OOP class abstractions with compact flat structs, hardware bitboards, and stack-allocated linked-list frames:

```text
Layer 5: CLI, UCI Protocol & Interactive Session
         engine/cli.cpp   engine/uci.cpp
               |
Layer 4: Search Engine & Multiprocessor Parallelism
         engine/search.cpp  (PVS, YBWC, Lazy SMP, WorkerPool)
               |
Layer 3: Evaluation, Transposition Table & Move Ordering
         engine/eval.cpp   engine/tt.cpp   engine/opening_book.cpp   engine/tablebase.cpp
               |
Layer 2: Legal Move Generation & Hardware Popcount
         engine/movegen.cpp   engine/perft.h
               |
Layer 1: Position Representation & Stack-Linked State
         engine/position.cpp   engine/zobrist.cpp
               |
Layer 0: Core Types & BMI2/PEXT Slider Bitboards
         engine/types.cpp   engine/bitboards.cpp
```

### Architectural Principles
- **Strict Downward Dependency**: Every module depends strictly on the layers below it. Circular dependencies are strictly prohibited.
- **Single Translation Unit (STU)**: The entire calculation engine compiles via `engine/main.cpp`. Link-time optimization (LTO) occurs naturally at compile-time with full inlining visibility across layers.
- **Zero Dynamic Heap Allocations**: Hot search and move generation paths allocate zero heap memory (`malloc`/`new`). Board history is maintained on the execution call stack via `StateInfo` structs.

---

## 2. Include Cascade & Single Translation Unit

```text
engine/main.cpp
  ├── engine/types.cpp
  ├── engine/bitboards.cpp
  ├── engine/zobrist.cpp
  ├── engine/position.cpp
  ├── engine/movegen.cpp
  ├── engine/tt.cpp
  ├── engine/eval.cpp
  ├── engine/search.cpp
  ├── engine/opening_book.cpp
  ├── engine/tablebase.cpp
  ├── engine/rl_interface.cpp
  ├── engine/uci.cpp
  ├── engine/cli.cpp
  └── engine/perft.h
```

---

## 3. Data Representations & Encoding Conventions

### Square Indexing (0 to 63)
Little-Endian Rank-File (LERF) mapping:
```text
a1=0   b1=1   c1=2   d1=3   e1=4   f1=5   g1=6   h1=7
a2=8   b2=9   ...
...
a8=56  b8=57  c8=58  d8=59  e8=60  f8=61  g8=62  h8=63
```
Formula: `Square sq = static_cast<Square>(rank * 8 + file)`.

### Piece & PieceType Encoding
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
- Piece encoding: `(color << 3) | piece_type`.
- Color extraction: `piece >> 3`.
- Type extraction: `piece & 7`.

### Compact 16-Bit Move Encoding (`struct Move`)
Moves are packed into an ultra-compact 16-bit integer (`uint16_t`):
```text
Bits  0–5  : from square (0–63)
Bits  6–11 : to square (0–63)
Bits 12–13 : promotion piece type offset (0=Knight, 1=Bishop, 2=Rook, 3=Queen)
Bits 14–15 : move flags:
             00 = normal move / quiet
             01 = promotion
             10 = en passant
             11 = castling
```
Zero heap allocations. Move generation populates a contiguous stack array `MoveList` capable of holding up to 256 moves.

---

## 4. Bitboard Attack Calculation & PEXT Sliders (`bitboards.cpp`)

TigerFish leverages modern x86-64 BMI2 hardware instructions:
- **PEXT Sliders**: Bishop and Rook attacks use `_pext_u64(occupancy, mask)` to index directly into dense precomputed attack arrays with zero hash collisions.
- **Fallback**: Automatically falls back to standard magic bitboard multiplier hashing if compiled on architectures without BMI2.
- **Rays & Masks**: Precomputed lines (`LINE_BB[sq1][sq2]`), squares between (`BETWEEN_BB[sq1][sq2]`), knight attacks (`KNIGHT_ATTACKS[64]`), and king attacks (`KING_ATTACKS[64]`).

---

## 5. Board State & Stack-Linked History (`position.cpp`)

### `Position` Struct (~140 Bytes)
```cpp
struct Position {
    Bitboard pieces[16]{};      // Bitboards indexed by Piece enum
    Bitboard colors[2]{};       // Combined occupancy per color
    Piece mailbox[64]{};        // 0-63 square piece lookup
    Square king_sq[2]{};        // Cached White and Black king squares
    Color side_to_move = WHITE;
    uint8_t castling_rights = 0;
    int rule50 = 0;
    int game_ply = 0;
    StateInfo* state = nullptr; // Pointer to current stack frame
};
```

### Stack-Allocated `StateInfo`
```cpp
struct StateInfo {
    uint64_t zobrist_key = 0;
    Square en_passant_square = SQ_NONE;
    Piece captured_piece = NO_PIECE;
    uint8_t castling_rights = 0;
    int rule50 = 0;
    Bitboard checkers = 0;
    Bitboard pinned = 0;
    StateInfo* previous = nullptr;
};
```
When `make_move(pos, move, state)` is called:
1. The new `StateInfo` is allocated on the caller's execution stack frame.
2. It links to `pos.state->previous = old_state`.
3. `undo_move(pos, move)` restores the previous pointer with zero dynamic memory operations.

---

## 6. Legal Move Generation & Bitboard Leaf Popcount (`movegen.cpp`)

TigerFish generates **strictly legal moves** directly without post-move legality filters:
1. Checks and Pins are precomputed via bitwise operations (`update_pins(pos)`).
2. If in double-check, only king evasions are generated.
3. If in single check, non-king moves must intercept the checker ray or capture the checker (`target_mask = squares_between(ksq, checker) | (1ULL << checker)`).
4. Pinned pieces can only move along their pinning ray.
5. **Hardware Leaf Popcount (`count_legal_moves`)**: At depth 1 of perft, move serialization to RAM is completely bypassed. Move counts are computed directly via `popcount(attacks & target_mask)`, achieving over **8.06 Billion NPS**.

---

## 7. Lockless Cache-Aligned Transposition Table (`tt.cpp`)

```cpp
struct alignas(16) TTClusterEntry {
    uint64_t key;  // Full 64-bit Zobrist key XOR'd with packed data
    uint64_t data; // [move:16][score:16][eval:16][depth:8][meta:8]
};

struct alignas(64) TTCluster {
    TTClusterEntry entries[4]; // Exactly 64 bytes = 1 CPU cacheline
};
```
- **Torn Read Prevention**: `key` is stored as `zobrist_key ^ data`. Upon probe, `(entry.key ^ entry.data) == zobrist_key` verifies consistency. If a multi-threaded writer modified the entry mid-probe, the XOR mismatch discards the entry automatically without mutex locking.
- **Power-of-2 Indexing**: TT size is allocated as a power of 2, allowing single-cycle bitmask indexing (`key & (cluster_count - 1)`) without expensive integer division instructions.

---

## 8. Tapered Static Evaluation (`eval.cpp`)

Static evaluation blends Middlegame (MG) and Endgame (EG) scores according to game phase ($0 \le \text{phase} \le 24$):
- **Piece-Square Tables (PST)**: Tuned tapered values for all piece types across all 64 squares.
- **Bishop Pair Bonus**: Awarded when a side maintains 2 or more active bishops.
- **Pawn Structure**: Evaluates doubled pawns, isolated pawns, and passed pawns with rank-based bonus scaling.
- **King Safety**: Evaluates pawn shield integrity in front of castled kings.
- **Tempo Bonus**: +20 cp granted to the side with the initiative.

---

## 9. Search Engine & Multiprocessor Parallelism (`search.cpp`)

TigerFish 2.0 features modern tournament-grade search heuristics:
1. **Principal Variation Search (PVS)**: Explores candidate moves with zero-window bounds (`[-alpha - 1, -alpha]`), re-searching only upon fail-high.
2. **Reverse Futility Pruning (RFP)**: Evaluates static margin at depth $\le 8$ and cuts off branches where static score safely exceeds $\beta$.
3. **Adaptive Null Move Pruning (NMP)**: Reduces search depth by $4 + \text{depth} / 4$ on null moves when pieces exist.
4. **Logarithmic Late Move Reduction (LMR)**: Non-tactical moves searched late in the move order are reduced logarithmically using a precomputed 64x64 reduction matrix.
5. **Dynamic Quiescence Delta Pruning**: Inspects remaining opponent material and pawn promotions to dynamically prune impossible tactical lines in quiescence search.
6. **Depth-Threshold TT Probing**: Bypasses TT probing at leaf plies (`depth < 3 && !is_root`) to eliminate DRAM/cache thrashing.
7. **Young Brothers Wait Concept (YBWC / Root Parallelism)**: Evaluates the oldest brother (PV move) sequentially on the main thread, then distributes remaining root moves across worker threads with zero-window scout searches.
8. **Persistent `WorkerPool`**: Helper threads sleep on `std::condition_variable` between moves, completely eliminating Windows/Linux thread creation latency during matches.

---

## 10. High-Performance Parallel Perft Engine (`perft.h`)

- **Dynamic Work-Array Stealing**: Pre-generates tasks 2–3 plies deep into a flat array.
- **Adaptive Task Chunking**: Workers claim batches of 16 down to 1 task dynamically, preventing atomic memory bus locking on 24+ core systems.
- **Live Telemetry Stream (`--stream`)**: Displays real-time task progress, instantaneous NPS, overall NPS, elapsed time, and ETA.
- **Split Perft (`--split`)**: Generates move-by-move node counts matching Stockfish perft division.

---

## 11. CLI, UCI & Lichess Bot Bridge (`cli.cpp`, `uci.cpp`, `lichess_bot.py`)

### Command Dispatches
- `./game.exe uci`: Standard Universal Chess Interface for Lichess bots, GUIs (Arena, Cutechess), and web backends.
- `./game.exe interactive`: Lightweight line-oriented protocol (`newgame [fen]`, `apply <uci>`, `undo`, `best <depth>`, `quit`).
- `./game.exe bench [depth] [threads] [--ybwc]`: Standardized 5-position benchmark.
- `./game.exe perft [fen] [depth] [--threads N] [--split] [--tt] [--stream]`: Standalone Perft suite.

### Lichess Bot Bridge Architecture
`lichess_bot.py` communicates with `game.exe` over asynchronous non-blocking pipes (`asyncio.create_subprocess_exec`). Time management dynamically computes move budgets based on clock remaining, increment, speed mode, and network lag buffer.

---

## 12. Performance Profile & Verification Matrix

| Changed Module | Verification Command | Exit Criteria |
| :--- | :--- | :--- |
| **Movegen & Bitboards** | `.\game.exe perft bench` | 100% exact node count match across all 8 positions |
| **Stockfish Parity** | `tools\perft\stockfish_perft_comparator.exe startpos 4` | 100% exact match on all 20 root moves |
| **Search & Pruning** | `.\game.exe bench 12 4` | Completed with $>15\text{M NPS}$ and zero thread stalls |
| **Bot Startup Preflight** | `python test_bot_startup.py` | All 5 clock scenarios PASS within budget |
| **Regression Test Harness** | `python Test/regression/run_all_tests.py` | 87/87 tests PASS (baseline JSON saved) |
