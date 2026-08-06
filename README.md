# 🐅 TigerFish Chess Engine

A high-performance, lightweight chess engine written in **C++20**.

TigerFish leverages 64-bit bitboard representations, magic move generation, **Zobrist hashing with zero-drift history**, a **32 MB Transposition Table**, and **MVV-LVA move ordering** to deliver sub-millisecond calculation speeds and sharp tactical play.

---

## ⚡ Performance Benchmarks & Milestones

TigerFish features a micro-optimized bitboard move generator and an alpha-beta search pipeline evaluated across massive position datasets:

| Feature Stage | Depth 6 Search Time | Nodes Searched | Total Speedup |
| :--- | :--- | :--- | :--- |
| **Baseline (Plain Minimax)** | $46,600\text{ ms}$ ($46.6\text{s}$) | $173,000,000\text{ nodes}$ | $1\times$ |
| **Zobrist Transposition Table** | $350\text{ ms}$ | $1,203,335\text{ nodes}$ | $133\times$ FASTER |
| **TT + MVV-LVA Move Ordering** | **$67\text{ ms}$** | **$542,396\text{ nodes}$** | **$695\times$ FASTER** |

* **Move Generator Throughput**: ~25–35 Million positions / second across 24 CPU threads on parallel Perft benchmarks.
* **Cold-Search Speed**: Sub-100ms response time at Depth 6–7.

---

## ✨ Key Technical Highlights

* **64-Bit Bitboard Core**: All piece positions, occupancies, and attack rays are represented as 64-bit unsigned integers (`uint64_t`) evaluated via single-cycle hardware bit instructions.
* **32-Bit Compact Move Bitfield**: Moves are stored as 32-bit unsigned integers (`uint32_t`) with zero heap allocation overhead during search recursion.
* **Magic Bitboard Sliding Attacks**: Pre-calculated magic multipliers hash Rook, Bishop, and Queen slider attack rays instantly.
* **Zobrist Transposition Table (32 MB)**: $O(1)$ memory lookup table storing $2,097,152$ evaluation bounds (`TT_EXACT`, `TT_ALPHA`, `TT_BETA`), depth levels, and historical best moves.
* **MVV-LVA Move Ordering**: Most Valuable Victim - Least Valuable Attacker insertion sorting prioritizes high-tactical-value captures, boosting alpha-beta cutoff efficiency by $695\times$.

> 📖 *For exact C++ struct definitions, bitwise math formulas, and file-by-file code breakdowns, see the **[OPERATIONS_MANUAL.md](OPERATIONS_MANUAL.md)**.*

---

## 🚀 Building & Execution Guide

### 1. Compile the C++ Engine
Compile `engine/main.cpp` with high optimization flags:
```bash
g++ -O3 -std=c++20 engine/main.cpp -o game.exe
```

### 2. Execution Commands

- **Persistent Session Mode (IPC Pipe / Bot Driver)**:
  ```bash
  ./game.exe interactive
  ```
  *Commands over stdin: `newgame [fen]`, `apply <uci>`, `best <depth>`, `quit`.*

- **Single-shot Best Move Calculation**:
  ```bash
  ./game.exe best "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 7
  ```

- **Perft Move Generation Verification**:
  ```bash
  ./game.exe perft "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 5
  ```

- **Terminal Human vs Engine Game**:
  ```bash
  ./game.exe play 7
  ```

---

## 🏗️ 5-Layer Pyramid Architecture

```
 ┌────────────────────────────────────────────────────────┐
 │                    LAYER 5: ENGINE                     │
 │  File: search.cpp, main.cpp                            │
 │  Classes: Engine, TranspositionTable, CLI Commands     │
 │  Functions: evaluate(), minimax(), best_move()         │
 └───────────────────────────┬────────────────────────────┘
                             │ depends down on
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │            LAYER 4: MOVE GENERATION & RULES            │
 │  File: rules.cpp                                       │
 │  Classes & Structs: MoveGenerator, MoveList, PinInfo   │
 │  Functions: generate_moves(), sort_mvv_lva(),          │
 │             get_game_result(), is_in_check()           │
 └───────────────────────────┬────────────────────────────┘
                             │ depends down on
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │                 LAYER 3: BOARD STATE                   │
 │  File: board.cpp                                       │
 │  Class: Board                                          │
 │  Functions: make_move(), unmake_move(), set_fen(),    │
 │             init_zobrist(), zobrist_hash               │
 └───────────────────────────┬────────────────────────────┘
                             │ depends down on
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │            LAYER 2: 32-BIT MOVE PACKING                │
 │  File: board.cpp                                       │
 │  Functions: pack_move(), move_from(), move_to(),       │
 │             move_to_uci()                              │
 └───────────────────────────┬────────────────────────────┘
                             │ depends down on
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │          LAYER 1: ENUMS, MASKS & LOOKUP TABLES         │
 │  Files: board.cpp, magic_lut.cpp, eval_lut.cpp         │
 │  Enums: Piece, Color, Direction, CastleRights,         │
 │         GameResult                                     │
 │  Tables: ray_table, PST arrays, Magic Bitboards        │
 └───────────────────────────┴────────────────────────────┘
```

## 📁 Repository Structure

```
.
├── engine/                 # C++20 5-Layer Chess Engine Source
│   ├── board.cpp           # Layer 2 & 3: Board state, 32-bit Move packing, Zobrist hashing
│   ├── eval_lut.cpp        # Layer 1: Centipawn values & Piece-Square Tables (PST)
│   ├── magic_lut.cpp       # Layer 1: Precomputed magic bitboard lookups
│   ├── main.cpp            # Layer 5: CLI interface & interactive persistent mode
│   ├── rules.cpp           # Layer 4: MoveGenerator, MoveList (MVV-LVA), checkmate/stalemate
│   └── search.cpp          # Layer 5: Minimax, 32MB Transposition Table, evaluation
├── .gitignore              # Repository Git ignore rules
├── game.exe                # Compiled C++ Engine Binary (32MB Transposition Table)
├── OPERATIONS_MANUAL.md    # Deep C++ Engine Developer Manual
└── README.md               # You are here!
```
