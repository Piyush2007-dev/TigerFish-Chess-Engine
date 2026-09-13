# TigerFish 2.0 — The Ultimate Codebase Implementation & Developer Learning Guide

> **Document Version**: 2.0 (Comprehensive Technical Specification & Architecture Masterclass)  
> **Target Audience**: Engine Creator & Sole Maintainer.  
> **Source Directory**: [`engine/`](file:///d:/PROJECTS/chess_engine_v0/engine/)  
> **Compilation Command**: `g++ -O3 -march=native -std=c++20 -pthread engine/main.cpp -o game.exe`  

---

## Executive Summary: How to Read This Guide

This document was created specifically because **TigerFish 2.0 is a 100% modern C++20 Data-Oriented rewrite** of the legacy TigerFish 1.0 engine. If you wrote or understood parts of the legacy engine (such as the object-oriented `Board` class, classical magic bitboards, pseudo-legal move generation with check filtering, or mutex-locked transposition tables), this guide serves as your authoritative textbook.

Every single enum, struct, function, bitwise trick, mathematical derivation, template specialization, and concurrency primitive across all 15 files in `engine/` is documented here with line-by-line clarity.

---

# Table of Contents
1. [Master Mental Model: From OOP to Modern Data-Oriented Design (DOD)](#1-master-mental-model-from-oop-to-modern-data-oriented-design-dod)
2. [Single Translation Unit (STU) Architecture & Inclusion Cascade](#2-single-translation-unit-stu-architecture--inclusion-cascade)
3. [The Complete Enums & Primitives Directory (`types.cpp`)](#3-the-complete-enums--primitives-directory-typescpp)
   - [3.1 Board Topology & Coordinate Math](#31-board-topology--coordinate-math)
   - [3.2 Color & Piece Arithmetic](#32-color--piece-arithmetic)
   - [3.3 Compact 16-Bit Move Binary Packing](#33-compact-16-bit-move-binary-packing)
   - [3.4 Hardware Bit Intrinsics & Assembly Optimization](#34-hardware-bit-intrinsics--assembly-optimization)
   - [3.5 Game Score & Evaluation Constants](#35-game-score--evaluation-constants)
4. [Bitboard Attack Engine & BMI2 PEXT Sliders (`bitboards.cpp`)](#4-bitboard-attack-engine--bmi2-pext-sliders-bitboardscpp)
   - [4.1 Dense Ray & Line Lookup Geometry](#41-dense-ray--line-lookup-geometry)
   - [4.2 Non-Slider Precomputed Attack Lookups](#42-non-slider-precomputed-attack-lookups)
   - [4.3 BMI2 PEXT Sliders vs. Classical Magic Bitboards](#43-bmi2-pext-sliders-vs-classical-magic-bitboards)
   - [4.4 Geometric Ray Line Masks: `BETWEEN_BB` and `LINE_BB`](#44-geometric-ray-line-masks-between_bb-and-line_bb)
5. [Zobrist Hashing & Incremental State Fingerprinting (`zobrist.cpp`)](#5-zobrist-hashing--incremental-state-fingerprinting-zobristcpp)
   - [5.1 SplitMix64 PRNG Generation](#51-splitmix64-prng-generation)
   - [5.2 Incremental O(1) Updates via XOR Commutativity](#52-incremental-o1-updates-via-xor-commutativity)
   - [5.3 Dual Hash Keys: Board Zobrist & Pawn Zobrist](#53-dual-hash-keys-board-zobrist--pawn-zobrist)
6. [Position Representation & The Zero-Allocation Stack (`position.cpp`)](#6-position-representation--the-zero-allocation-stack-positioncpp)
   - [6.1 The ~140-Byte Flat `Position` Struct](#61-the-140-byte-flat-position-struct)
   - [6.2 The Stack-Allocated `StateInfo` Linked-List Node](#62-the-stack-allocated-stateinfo-linked-list-node)
   - [6.3 Incremental Board Mutation: `put_piece`, `remove_piece`, `move_piece`](#63-incremental-board-mutation-put_piece-remove_piece-move_piece)
   - [6.4 Full Step-by-Step Trace of `make_move`](#64-full-step-by-step-trace-of-make_move)
   - [6.5 Full Step-by-Step Trace of `undo_move`](#65-full-step-by-step-trace-of-undo_move)
   - [6.6 Checkers & Pin Detection Geometry](#66-checkers--pin-detection-geometry)
   - [6.7 Draw Detection Engines: Repetition, 50-Move, & Insufficient Material](#67-draw-detection-engines-repetition-50-move--insufficient-material)
7. [The C++ Template Metaprogramming Engine (`movegen.cpp`)](#7-the-c-template-metaprogramming-engine-movegencpp)
   - [7.1 The Cost of Traditional Branching in Search](#71-the-cost-of-traditional-branching-in-search)
   - [7.2 Line-by-Line Breakdown of `template <Color Us, bool CapturesOnly>`](#72-line-by-line-breakdown-of-template-color-us-bool-capturesonly)
   - [7.3 Pawn Move Generation (Pushes, Promotions, Captures, En Passant Discovered Check)](#73-pawn-move-generation-pushes-promotions-captures-en-passant-discovered-check)
   - [7.4 Piece Move Generation & Pin Ray Filtering](#74-piece-move-generation--pin-ray-filtering)
   - [7.5 King Moves & Direct Legal Castling](#75-king-moves--direct-legal-castling)
   - [7.6 Master Legal Move Dispatcher: Double-Check vs. Single-Check vs. No-Check](#76-master-legal-move-dispatcher-double-check-vs-single-check-vs-no-check)
   - [7.7 Ultra-Fast Pure Bitboard Move Counting (8+ Billion NPS in Perft)](#77-ultra-fast-pure-bitboard-move-counting-8-billion-nps-in-perft)
8. [Lockless Cache-Aligned Transposition Table (`tt.cpp`)](#8-lockless-cache-aligned-transposition-table-ttcpp)
   - [8.1 64-Byte CPU Cacheline Cluster Topology](#81-64-byte-cpu-cacheline-cluster-topology)
   - [8.2 Bitfield Packing of 64-Bit Data Payload](#82-bitfield-packing-of-64-bit-data-payload)
   - [8.3 Lockless Multi-Threaded Safety via Key-Data XOR Verification](#83-lockless-multi-threaded-safety-via-key-data-xor-verification)
   - [8.4 Single-Cycle Power-of-2 Bitmask Indexing](#84-single-cycle-power-of-2-bitmask-indexing)
   - [8.5 Four-Slot Cluster Replacement Policy](#85-four-slot-cluster-replacement-policy)
   - [8.6 Root-Relative Mate Score Normalization](#86-root-relative-mate-score-normalization)
9. [Tapered Evaluation & Heuristic Scoring (`eval.cpp`)](#9-tapered-evaluation--heuristic-scoring-evalcpp)
   - [9.1 Game Phase Arithmetic & Linear Interpolation](#91-game-phase-arithmetic--linear-interpolation)
   - [9.2 Piece-Square Tables (PST) & Incremental Accumulation](#92-piece-square-tables-pst--incremental-accumulation)
   - [9.3 Pawn Structure Analysis & The Dedicated 64K Pawn Cache](#93-pawn-structure-analysis--the-dedicated-64k-pawn-cache)
   - [9.4 Positional Factors: Bishop Pair & Open Files](#94-positional-factors-bishop-pair--open-files)
   - [9.5 King Safety: 2-Tier Pawn Shields & King Zone Attack Counters](#95-king-safety-2-tier-pawn-shields--king-zone-attack-counters)
   - [9.6 External Machine Learning / Neural Network Hook](#96-external-machine-learning--neural-network-hook)
10. [Tournament-Grade Search Engine & Modern Pruning (`search.cpp`)](#10-tournament-grade-search-engine--modern-pruning-searchcpp)
    - [10.1 Iterative Deepening & Exponential Aspiration Windows](#101-iterative-deepening--exponential-aspiration-windows)
    - [10.2 Quiescence Search: Stand-Pat, Dynamic Delta Pruning, & SEE Guard](#102-quiescence-search-stand-pat-dynamic-delta-pruning--see-guard)
    - [10.3 Principal Variation Search (PVS Alpha-Beta)](#103-principal-variation-search-pvs-alpha-beta)
    - [10.4 The Complete Pruning Hierarchy (RFP, NMP, ProbCut, Razor, Futility, LMP, SEE)](#104-the-complete-pruning-hierarchy-rfp-nmp-probcut-razor-futility-lmp-see)
    - [10.5 Singular Extensions (SE) & Multi-Cut Logic](#105-singular-extensions-se--multi-cut-logic)
    - [10.6 Fast-Path TT Move Verification & Instant Cutoff](#106-fast-path-tt-move-verification--instant-cutoff)
    - [10.7 Static Exchange Evaluation (SEE): The Recursive Swap Engine](#107-static-exchange-evaluation-see-the-recursive-swap-engine)
    - [10.8 Staged Move Ordering & Dynamic Heuristics](#108-staged-move-ordering--dynamic-heuristics)
    - [10.9 3D Continuation History (Conthist) System](#109-3d-continuation-history-conthist-system)
    - [10.10 History Gravity with Malus Formula](#1010-history-gravity-with-malus-formula)
    - [10.11 Improving-Flag Late Move Reductions (LMR)](#1011-improving-flag-late-move-reductions-lmr)
    - [10.12 Multithreaded SMP: Lazy SMP, Persistent WorkerPool, & YBWC](#1012-multithreaded-smp-lazy-smp-persistent-workerpool--ybwc)
    - [10.13 CPU Topology & Thread Affinity Management on Windows](#1013-cpu-topology--thread-affinity-management-on-windows)
11. [Multi-Threaded Work-Stealing Perft Engine (`perft.h`)](#11-multi-threaded-work-stealing-perft-engine-perfth)
    - [11.1 Dynamic Work-Stealing & Adaptive Task Chunking](#111-dynamic-work-stealing--adaptive-task-chunking)
    - [11.2 Lockless Perft TT & Split-Tree Synchronization](#112-lockless-perft-tt--split-tree-synchronization)
12. [Auxiliary Systems: Opening Book, Tablebases, & Protocols](#12-auxiliary-systems-opening-book-tablebases--protocols)
    - [12.1 Polyglot Opening Book Reader (`opening_book.cpp`)](#121-polyglot-opening-book-reader-opening_bookcpp)
    - [12.2 Endgame Tablebases (`tablebase.cpp`)](#122-endgame-tablebases-tablebasecpp)
    - [12.3 Reinforcement Learning Interface (`rl_interface.cpp`)](#123-reinforcement-learning-interface-rl_interfacecpp)
    - [12.4 Interactive CLI Protocol (`cli.cpp`)](#124-interactive-cli-protocol-clicpp)
    - [12.5 UCI Protocol Parser (`uci.cpp`)](#125-uci-protocol-parser-ucicpp)
    - [12.6 Python Lichess Bridge (`lichess_bot.py`)](#126-python-lichess-bridge-lichess_botpy)
13. [Master Data-Flow & Execution Lifecycles](#13-master-data-flow--execution-lifecycles)
    - [13.1 Complete Search Request Lifecycle](#131-complete-search-request-lifecycle)
    - [13.2 Complete Move Mutation Lifecycle](#132-complete-move-mutation-lifecycle)
    - [13.3 Complete Move Generation Lifecycle](#133-complete-move-generation-lifecycle)
14. [Developer Maintenance, Tuning & Debugging Guide](#14-developer-maintenance-tuning--debugging-guide)
    - [14.1 The 5 Critical Invariants of the Codebase](#141-the-5-critical-invariants-of-the-codebase)
    - [14.2 How to Add a New Evaluation Term](#142-how-to-add-a-new-evaluation-term)
    - [14.3 How to Tune Search Pruning Thresholds](#143-how-to-tune-search-pruning-thresholds)
    - [14.4 Systematic Debugging: How to Isolate Movegen or Search Bugs](#144-systematic-debugging-how-to-isolate-movegen-or-search-bugs)

---

# 1. Master Mental Model: From OOP to Modern Data-Oriented Design (DOD)

If you previously built chess engines using classical Object-Oriented Programming (OOP), your mental model was likely built on classes with methods, polymorphic objects, and dynamic memory allocations:

```text
       OLD OOP PARADIGM (TigerFish 1.0)                    NEW DOD PARADIGM (TigerFish 2.0)
┌──────────────────────────────────────────────┐    ┌──────────────────────────────────────────────┐
│ • Monolithic Board object (~328 KB)          │    │ • ~140-byte flat POD Position struct         │
│ • History stored in 8192-element heap array  │    │ • Zero-allocation stack-linked StateInfo     │
│ • Pseudo-legal movegen + check tests         │    │ • Strictly legal movegen directly            │
│ • Runtime 'if (side == WHITE)' branching     │    │ • Compile-time template specialization       │
│ • Mutex-locked Transposition Table           │    │ • 64-byte lockless XOR cluster TT            │
│ • Magic bitboard 2.3 MB lookup tables        │    │ • BMI2 PEXT 850 KB dense L2-cache tables     │
│ • Moves represented as 32-bit/64-bit structs │    │ • Moves packed into single 16-bit integers   │
└──────────────────────────────────────────────┘    └──────────────────────────────────────────────┘
```

### Why Data-Oriented Design (DOD) Matters in Computer Chess

A modern CPU core (such as Intel Raptor Lake or AMD Zen 4) executes at 4–5 GHz. It can execute 4 to 8 arithmetic or bitwise instructions per clock cycle. However, fetching data from main RAM costs **150 to 250 clock cycles** of complete stall! 

To achieve **45+ Million Nodes Per Second (NPS)** in search and **8+ Billion NPS in Perft**, the engine must adhere to four strict physical hardware realities:

1. **L1 and L2 CPU Cache Residency**:
   - Every piece of data touched in the inner search loop must stay resident in L1 (32–48 KB) or L2 (1–2 MB) cache.
   - The entire `Position` state is ~140 bytes. Two entire board states fit in a single 256-byte CPU cacheline pair.
   - Attack lookup tables are dense BMI2 PEXT tables under 850 KB, completely fitting into L2 cache.
2. **Zero Dynamic Memory Allocation**:
   - Calls to `malloc`, `new`, `std::vector::resize`, or `std::string` require OS heap allocation locks and pointer chasing.
   - In TigerFish 2.0, **not a single heap allocation occurs during search or perft**. Every data structure is either preallocated statically at engine startup or lives on the local C++ execution call stack.
3. **Elimination of Branch Mispredictions**:
   - Modern CPUs use deep 16–20 stage execution pipelines. When the CPU mispredicts an `if/else` branch, the entire pipeline is flushed, wasting 15–20 clock cycles.
   - By using C++20 template metaprogramming (`template <Color Us, bool CapturesOnly>`), White and Black moves are generated by two completely separate blocks of machine code where directional offsets (`+8` vs. `-8`) and promotion ranks are hardcoded constants. The CPU executes straight-line instructions without branching.
4. **Lockless Concurrency**:
   - Mutexes (`std::mutex`) require OS kernel context switches. Spinlocks saturate memory buses.
   - TigerFish 2.0 uses 64-byte cacheline clusters with **Key-Data XOR Verification**. 24 or 64 worker threads can write to the same Transposition Table simultaneously with zero locks and zero torn reads.

---

# 2. Single Translation Unit (STU) Architecture & Inclusion Cascade

TigerFish 2.0 compiles as a **Single Translation Unit (STU)**. Instead of compiling 15 individual `.cpp` files into separate `.o` object files and linking them together, the entire engine is included into a single master compilation file: [`engine/main.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/main.cpp).

```text
                                       engine/main.cpp
                                              │
               ┌──────────────────────────────┴──────────────────────────────┐
               ▼                                                             ▼
        engine/types.cpp                                              engine/cli.cpp
               │                                                             │
        engine/bitboards.cpp                                          engine/uci.cpp
               │                                                             │
        engine/zobrist.cpp                                            engine/perft.h
               │                                                             ▲
        engine/position.cpp                                                  │
               │                                                      engine/search.cpp
        engine/movegen.cpp                                                   ▲
               │                                                             │
        engine/tt.cpp ─────────► engine/eval.cpp ─────────► engine/opening_book.cpp
                                                       └───► engine/tablebase.cpp
                                                       └───► engine/rl_interface.cpp
```

### Why STU is Structurally Superior:
- **Whole-Program Inlining**: The C++ compiler sees the entire call graph at once. Functions such as `pop_lsb()`, `make_square()`, `is_square_attacked()`, and `score_move()` are inlined directly into the assembly instructions of `alpha_beta()` without any call-overhead.
- **Dead-Code Elimination**: If a function or table is unused, the compiler discards it entirely.
- **Strict Linear Layer Boundary**: A source file can ONLY call functions declared **above** it in the cascade. For example, `position.cpp` can call `bitboards.cpp`, but `bitboards.cpp` cannot even reference `Position`. This guarantees mathematically that circular dependencies are impossible.

---

# 3. The Complete Enums & Primitives Directory (`types.cpp`)

[`engine/types.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/types.cpp) defines the fundamental bitwise vocabulary of the engine.

### 3.1 Board Topology & Coordinate Math

The chess board is indexed as a flat 1D array of 64 squares, numbered from `0` (A1) to `63` (H8):

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

```cpp
using Bitboard = uint64_t;

enum Square : uint8_t {
    A1 = 0,  B1 = 1,  C1 = 2,  D1 = 3,  E1 = 4,  F1 = 5,  G1 = 6,  H1 = 7,
    A2 = 8,  B2 = 9,  C2 = 10, D2 = 11, E2 = 12, F2 = 13, G2 = 14, H2 = 15,
    ...
    A8 = 56, B8 = 57, C8 = 58, D8 = 59, E8 = 60, F8 = 61, G8 = 62, H8 = 63,
    SQ_NONE = 64
};

enum File : uint8_t { FILE_A = 0, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H };
enum Rank : uint8_t { RANK_1 = 0, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8 };
```

#### Coordinate Bit-Math Proofs:
1. **Square from File and Rank**:
   $$\text{sq} = (\text{rank} \times 8) + \text{file} \equiv (\text{rank} \ll 3) \mid \text{file}$$
   ```cpp
   constexpr Square make_square(File f, Rank r) noexcept {
       return static_cast<Square>((r << 3) | f);
   }
   ```
2. **File Extraction**:
   $$\text{file} = \text{sq} \pmod 8 \equiv \text{sq} \ \& \ 7$$
   ```cpp
   constexpr File square_file(Square sq) noexcept {
       return static_cast<File>(sq & 7);
   }
   ```
3. **Rank Extraction**:
   $$\text{rank} = \lfloor \text{sq} / 8 \rfloor \equiv \text{sq} \gg 3$$
   ```cpp
   constexpr Rank square_rank(Square sq) noexcept {
       return static_cast<Rank>(sq >> 3);
   }
   ```
4. **Vertical Square Mirroring (Black Perspective)**:
   When evaluating a Black piece using White Piece-Square Tables (PST), we mirror the square vertically:
   $$\text{mirror\_sq} = ((7 - \text{rank}) \times 8) + \text{file} \equiv \text{sq} \oplus 56$$
   *Proof*: In binary, $56 = 111000_2$. XORing with 56 inverts bits 3, 4, and 5 (the rank bits: $7 - \text{rank}$) while leaving bits 0, 1, and 2 (the file bits) completely unchanged!

---

### 3.2 Color & Piece Arithmetic

```cpp
enum Color : uint8_t {
    WHITE = 0,
    BLACK = 1,
    COLOR_NONE = 2
};

enum PieceType : uint8_t {
    PT_NONE   = 0,
    PT_PAWN   = 1,
    PT_KNIGHT = 2,
    PT_BISHOP = 3,
    PT_ROOK   = 4,
    PT_QUEEN  = 5,
    PT_KING   = 6
};

enum Piece : uint8_t {
    NO_PIECE = 0,
    W_PAWN = 1, W_KNIGHT = 2, W_BISHOP = 3, W_ROOK = 4, W_QUEEN = 5, W_KING = 6,
    B_PAWN = 9, B_KNIGHT = 10, B_BISHOP = 11, B_ROOK = 12, B_QUEEN = 13, B_KING = 14
};
```

#### The Elegance of Piece Packing:
Look closely at the numbers:
- `W_PAWN = 1` ($0001_2$), `B_PAWN = 9` ($1001_2$)
- `W_ROOK = 4` ($0100_2$), `B_ROOK = 12` ($1100_2$)
- **Formula**: $\text{Piece} = (\text{Color} \ll 3) \mid \text{PieceType}$
  - **Color Extraction**: `piece >> 3` gives `0` (WHITE) or `1` (BLACK) in 1 clock cycle.
  - **Type Extraction**: `piece & 7` extracts the raw piece type ($1..6$) in 1 clock cycle.
  - **Opponent Color**: `color ^ 1` flips White to Black or Black to White in 1 clock cycle.

---

### 3.3 Compact 16-Bit Move Binary Packing

In traditional engines, a move struct is often 4 to 8 bytes. In TigerFish 2.0, every move is packed into a **single 16-bit integer (`uint16_t`)**:

```text
 15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
┌──────┬───────┬───────────────────────┬───────────────────────┐
│ Special Flags│     To Square (0-63)  │    From Square (0-63) │
└──────┴───────┴───────────────────────┴───────────────────────┘
```

```cpp
struct Move {
    uint16_t raw_data = 0;

    constexpr Move() noexcept = default;
    constexpr explicit Move(uint16_t data) noexcept : raw_data(data) {}
    constexpr Move(Square from, Square to, uint16_t flag = MoveFlags::QUIET) noexcept
        : raw_data(static_cast<uint16_t>(from | (to << 6) | flag)) {}

    constexpr Square from() const noexcept { return static_cast<Square>(raw_data & 0x3F); }
    constexpr Square to() const noexcept   { return static_cast<Square>((raw_data >> 6) & 0x3F); }
    constexpr uint16_t flags() const noexcept { return raw_data & 0xF000; }
    constexpr bool is_capture() const noexcept { return (raw_data & 0x4000) != 0; }
    constexpr bool is_promotion() const noexcept { return (raw_data & 0x8000) != 0; }
    ...
};
```

#### Flags Breakdown (`MoveFlags` in bits 12..15):
| Flag Hex | Binary | Semantic Meaning | Usage in Movegen |
|---|---|---|---|
| `0x0000` | `0000` | **Quiet Move** | Standard non-capturing move |
| `0x1000` | `0001` | **Double Pawn Push** | Sets en passant square target |
| `0x2000` | `0010` | **Castle Kingside** | King moves 2 squares, jumps H-rook |
| `0x3000` | `0011` | **Castle Queenside** | King moves 2 squares, jumps A-rook |
| `0x4000` | `0100` | **Normal Capture** | Captures piece on destination square |
| `0x5000` | `0101` | **En Passant Capture** | Captures pawn on adjacent square |
| `0x8000` | `1000` | **Promotion -> Knight** | Quiet promotion |
| `0x9000` | `1001` | **Promotion -> Bishop** | Quiet promotion |
| `0xA000` | `1010` | **Promotion -> Rook** | Quiet promotion |
| `0xB000` | `1011` | **Promotion -> Queen** | Quiet promotion |
| `0xC000` | `1100` | **Capture-Promo -> Knight** | Capture + promote to Knight |
| `0xD000` | `1101` | **Capture-Promo -> Bishop** | Capture + promote to Bishop |
| `0xE000` | `1110` | **Capture-Promo -> Rook** | Capture + promote to Rook |
| `0xF000` | `1111` | **Capture-Promo -> Queen** | Capture + promote to Queen |

- **Instant Capture Check**: `(raw_data & 0x4000) != 0` detects all captures and capture-promotions in 1 cycle.
- **Instant Promotion Check**: `(raw_data & 0x8000) != 0` detects all promotions in 1 cycle.

---

### 3.4 Hardware Bit Intrinsics & Assembly Optimization

TigerFish uses hardware intrinsics that map directly to single-cycle x86-64 instructions:

1. **`popcount(Bitboard mask)`**:
   - Maps to instruction: `POPCNT`
   - Action: Returns the exact count of set bits (1s) in a 64-bit integer in **1 clock cycle**.
2. **`lsb(Bitboard mask)`**:
   - Maps to instruction: `TZCNT` (Trailing Zero Count) or `BSF` (Bit Scan Forward)
   - Action: Returns the index ($0..63$) of the lowest set bit in **1 clock cycle**.
3. **`msb(Bitboard mask)`**:
   - Maps to instruction: `LZCNT` (Leading Zero Count) or `BSR` (Bit Scan Reverse)
   - Action: Returns the index ($0..63$) of the highest set bit in **1 clock cycle**.
4. **`pop_lsb(Bitboard& mask)`**:
   - Maps to instruction: `BLSR` (Reset Lowest Set Bit)
   - Action: Retrieves the lowest set bit and removes it from `mask` simultaneously:
     ```cpp
     inline Square pop_lsb(Bitboard& mask) noexcept {
         Square sq = lsb(mask);
         mask &= (mask - 1); // Or _blsr_u64(mask) under BMI
         return sq;
     }
     ```
     *Mathematical Proof of `mask & (mask - 1)`*: Subtracting 1 from a binary number turns the lowest `1` bit into `0` and changes all trailing `0`s into `1`s. ANDing this with the original number preserves all higher bits while canceling the lowest set bit completely!

---

### 3.5 Game Score & Evaluation Constants

```cpp
constexpr int SCORE_ZERO       = 0;
constexpr int SCORE_MATE       = 30000;
constexpr int SCORE_MATE_BOUND = 29000;
constexpr int SCORE_INFINITY   = 32000;
```

Any score $\ge 29000$ or $\le -29000$ represents a forced checkmate. For example, a score of $+29996$ means White delivers checkmate in $30000 - 29996 = 4$ plies (2 full moves).

---

# 4. Bitboard Attack Engine & BMI2 PEXT Sliders (`bitboards.cpp`)

[`engine/bitboards.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/bitboards.cpp) generates attack bitboards for every square and piece type on the board.

### 4.1 Dense Ray & Line Lookup Geometry

TigerFish precomputes directional lines and geometric intervals into global arrays:
- **`LINE_BB[sq1][sq2]`**:
  If `sq1` and `sq2` share a rank, file, or diagonal, this contains the **infinite bitboard line** passing through both squares. If they are not collinear, it contains `0ULL`.
- **`BETWEEN_BB[sq1][sq2]`**:
  Contains only the squares strictly between `sq1` and `sq2` (excluding the endpoints).
- **`DIAGONAL_MASKS[sq]`**: All diagonal rays passing through `sq`.
- **`ORTHOGONAL_MASKS[sq]`**: All rank and file rays passing through `sq`.

---

### 4.2 Non-Slider Precomputed Attack Lookups

During startup, `init_bitboards()` populates lookup arrays for fast non-slider pieces:
- `PAWN_ATTACKS[WHITE][sq]`: Precomputed North-East and North-West diagonal capture targets.
- `PAWN_ATTACKS[BLACK][sq]`: Precomputed South-East and South-West diagonal capture targets.
- `KNIGHT_ATTACKS[sq]`: The 8 possible L-shaped jump destinations.
- `KING_ATTACKS[sq]`: The 8 adjacent squares surrounding the king.

---

### 4.3 BMI2 PEXT Sliders vs. Classical Magic Bitboards

In chess, sliding pieces (Rooks, Bishops, Queens) have their rays stopped by blocking pieces. The fundamental challenge of computer chess is: *Given the 64-bit occupancy of the board, how do we look up the slider's legal attacks in 1 cycle?*

#### The Classical Magic Bitboard Approach:
Classical engines multiply the masked occupancy by a 64-bit "magic multiplier" and shift it right:
$$\text{index} = (\text{occupancy} \times \text{magic}) \gg \text{shift}$$
- **Disadvantages**:
  1. Magic hash functions have "holes" (unused indices), requiring huge lookup arrays (~2.3 MB).
  2. Large tables exceed the CPU's L1 cache, causing high-latency cache misses.

#### The BMI2 PEXT Sliders Approach (TigerFish 2.0):
Modern x86-64 CPUs (Intel Haswell+, AMD Zen 3+) feature the hardware instruction `_pext_u64` (Parallel Bit Extract):

```text
Occupancy Bitboard:  0 1 0 0 1 0 0 0 0 0 1 0 0 0 0 0  (scattered blocker bits)
Slider Mask:         0 1 0 0 1 0 0 0 0 0 1 0 0 0 0 0  (relevant ray bits)
                     ───────────────────────────────
PEXT Output Index:   1 1 1                             (contiguous dense index: 7)
```

```cpp
inline Bitboard rook_attacks(Square sq, Bitboard occupied) noexcept {
#if defined(__BMI2__)
    return ROOK_ATTACKS[sq][_pext_u64(occupied, ROOK_MASK[sq])];
#else
    occupied &= ROOK_MASK[sq];
    return ROOK_ATTACKS[sq][(occupied * ROOK_MAGICS[sq]) >> ROOK_SHIFT[sq]];
#endif
}
```

- **Zero Collisions**: `_pext_u64` extracts the blocker bits into a perfectly dense, gap-free integer index from $0$ to $2^{\text{mask\_bits}} - 1$.
- **Dense Packing**:
  - Rook attack table: 102,400 entries (800 KB).
  - Bishop attack table: 5,248 entries (41 KB).
  - **Total attack table footprint is under 850 KB**, fitting completely within the CPU's fast L2 cache!

---

# 5. Zobrist Hashing & Incremental State Fingerprinting (`zobrist.cpp`)

[`engine/zobrist.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/zobrist.cpp) provides 64-bit mathematical fingerprinting for board positions to enable transposition table lookups and 3-fold repetition detection.

### 5.1 SplitMix64 PRNG Generation

Every piece on every square, castling right, en passant file, and side to move is assigned a unique 64-bit random number generated via SplitMix64:

```cpp
inline uint64_t splitmix64(uint64_t& state) noexcept {
    uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
```

Precomputed arrays:
- `ZOBRIST_PIECES[15][64]`: Every piece type on every square ($15 \times 64$).
- `ZOBRIST_CASTLING[16]`: All 16 combinations of castling rights ($0..15$).
- `ZOBRIST_EN_PASSANT[8]`: Active en passant file ($0..7$).
- `ZOBRIST_SIDE`: Random constant toggled when Black is to move.
- `ZOBRIST_PAWNS[15][64]`: Dedicated pawn hashing table for the pawn evaluation cache.

---

### 5.2 Incremental O(1) Updates via XOR Commutativity

Because bitwise XOR ($\oplus$) is commutative and self-inverting ($A \oplus B \oplus B = A$), we never recompute the 64-bit hash from scratch.
- Moving a White Knight from B1 to C3:
  $$\text{key} \leftarrow \text{key} \oplus \text{ZOBRIST\_PIECES}[\text{W\_KNIGHT}][\text{B1}] \oplus \text{ZOBRIST\_PIECES}[\text{W\_KNIGHT}][\text{C3}]$$
- Capturing a Black Pawn on C3:
  $$\text{key} \leftarrow \text{key} \oplus \text{ZOBRIST\_PIECES}[\text{B\_PAWN}][\text{C3}]$$
- Changing turn:
  $$\text{key} \leftarrow \text{key} \oplus \text{ZOBRIST\_SIDE}$$
Each update executes in a single CPU cycle.

---

# 6. Position Representation & The Zero-Allocation Stack (`position.cpp`)

[`engine/position.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/position.cpp) encapsulates the board state mutation engine.

### 6.1 The ~140-Byte Flat `Position` Struct

```cpp
struct Position {
    Bitboard pieces[15];     // Bitboard per Piece enum (W_PAWN..B_KING)
    Bitboard colors[2];      // colors[WHITE] and colors[BLACK] occupancy
    Piece mailbox[64];       // O(1) piece lookup by square index (0..63)
    Color side_to_move = WHITE;
    int game_ply = 0;
    StateInfo* state = nullptr; // Points to current node on call stack
};
```

- **`pieces[15]`**: 15 64-bit integers. `pieces[W_PAWN]` gives a bitmask of all White pawns.
- **`colors[2]`**: Bitmask of all White pieces and Black pieces. Total board occupancy is `colors[WHITE] | colors[BLACK]`.
- **`mailbox[64]`**: 64-byte array mapping square index directly to the `Piece` occupying it. Allows $O(1)$ inspection of what piece occupies square E4 without checking 12 bitboards.

---

### 6.2 The Stack-Allocated `StateInfo` Linked-List Node

Irreversible chess state (castling rights, captured pieces, 50-move halfmove clock, Zobrist keys) is stored in `StateInfo`:

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

    StateInfo* previous = nullptr; // Linked-list pointer to parent stack frame
    Move move = MOVE_NONE;
    Piece moving_piece = NO_PIECE;
};
```

```text
Call Stack Depth 1:              Call Stack Depth 2:
┌─────────────────────────┐      ┌─────────────────────────┐
│ StateInfo st1           │      │ StateInfo st2           │
│ st1.previous = nullptr  │◄─────┼ st2.previous = &st1     │
└─────────────────────────┘      └─────────────────────────┘
```

**Zero Allocation**: In search, `StateInfo new_state;` is declared on the **local C++ execution stack**. `make_move` links `new_state.previous = pos.state` and sets `pos.state = &new_state`. `undo_move` sets `pos.state = pos.state->previous`. When the function returns, the stack frame disappears automatically without touching dynamic heap memory!

---

### 6.3 Incremental Board Mutation: `put_piece`, `remove_piece`, `move_piece`

When pieces move, TigerFish updates bitboards, mailboxes, Zobrist hashes, and Piece-Square Table (PST) scores incrementally:

```cpp
inline void put_piece(Position& pos, Piece p, Square sq) noexcept {
    Bitboard mask = 1ULL << sq;
    Color c = piece_color(p);

    pos.pieces[p] |= mask;
    pos.colors[c] |= mask;
    pos.mailbox[sq] = p;

    // Incremental Zobrist
    pos.state->zobrist_key ^= ZOBRIST_PIECES[p][sq];
    if (piece_type(p) == PT_PAWN) pos.state->pawn_zobrist_key ^= ZOBRIST_PAWNS[p][sq];

    // Incremental Evaluation
    pos.state->eval_mg += PIECE_PST_MG[p][sq];
    pos.state->eval_eg += PIECE_PST_EG[p][sq];
    pos.state->game_phase += PHASE_WEIGHTS[piece_type(p)];
}
```

---

### 6.4 Full Step-by-Step Trace of `make_move`

When `make_move(pos, move, new_state)` runs:
1. **State Replication**:
   - Copies `castling_rights`, increments `halfmove_clock`, flips `ZOBRIST_SIDE`.
   - Links `new_state.previous = pos.state` and points `pos.state = &new_state`.
2. **Piece Movement**:
   - `remove_piece(pos, from)` removes the moving piece.
   - If capture: `remove_piece(pos, to)` removes the captured victim and resets `halfmove_clock = 0`.
   - If promotion: `put_piece(pos, promo_piece, to)`.
   - Else: `put_piece(pos, moving_piece, to)`.
3. **Special Cases**:
   - Double pawn push: computes `ep_sq = (from + to) / 2` and sets `new_state.en_passant_square`.
   - En passant capture: calculates `cap_sq = make_square(to.file, from.rank)` and removes enemy pawn.
   - Castling: moves the accompanying Rook (`H1->F1`, `A1->D1`, `H8->F8`, or `A8->D8`).
4. **Castling Rights Updates**:
   - If King moves: clears all castling rights for that color.
   - If Rook moves from or is captured on A1/H1/A8/H8: clears that specific castling right and XORs `ZOBRIST_CASTLING`.
5. **Switch Side & Checkers**:
   - `pos.side_to_move ^= 1`.
   - Calls `update_checkers(pos)` to precalculate all direct checks against the newly active king.

---

### 6.5 Full Step-by-Step Trace of `undo_move`

When `undo_move(pos, move)` runs:
1. Flips `pos.side_to_move ^= 1` and decrements `pos.game_ply`.
2. If promotion: removes promoted piece and restores pawn on `to`.
3. Calls `move_piece(pos, to, from)` to slide piece back.
4. If capture: calls `put_piece(pos, captured_piece, to)` (or at `cap_sq` for en passant).
5. If castling: moves Rook back to its original corner.
6. Restores pointer: `pos.state = pos.state->previous`.

---

### 6.6 Checkers & Pin Detection Geometry

Instead of generating pseudo-legal moves and testing whether each move leaves the King in check, TigerFish **precomputes the board constraints**:

- **`update_checkers(pos)`**:
  Fires pawn, knight, bishop, and rook rays from the friendly King square (`ksq`) into enemy pieces to identify attackers delivering check.
- **`update_pins(pos)`**:
  Fires slider rays from the friendly King square through friendly pieces. If an enemy bishop, rook, or queen lies behind a friendly piece along that line, that friendly piece is added to `pos.state->pinned`.

---

### 6.7 Draw Detection Engines: Repetition, 50-Move, & Insufficient Material

- **50-Move Rule (`is_fifty_moves`)**:
  Returns true if `pos.state->halfmove_clock >= 100` (100 plies = 50 full moves).
- **Threefold Repetition (`is_repetition`)**:
  Walks backward along `StateInfo::previous`, checking `curr->zobrist_key == pos.state->zobrist_key` by skipping 2 plies at a time (since repetition requires the same side to move).
- **Insufficient Material (`is_insufficient_material`)**:
  Detects dead drawn endgames: King vs. King, King + Knight vs. King, King + Bishop vs. King, King + 2 Knights vs. King.

---

# 7. The C++ Template Metaprogramming Engine (`movegen.cpp`)

[`engine/movegen.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/movegen.cpp) contains the legal move generator.

### 7.1 The Cost of Traditional Branching in Search

In traditional engines:
```cpp
// RUNTIME COLOR BRANCHING (SLOW)
if (pos.side_to_move == WHITE) {
    pawn_push = 8;
    promo_rank = RANK_8;
} else {
    pawn_push = -8;
    promo_rank = RANK_1;
}
```
When executed 50 million times per second inside search trees, branch predictor stalls degrade engine speed by 25–40%.

---

### 7.2 Line-by-Line Breakdown of `template <Color Us, bool CapturesOnly>`

TigerFish generates moves using template metaprogramming:

```cpp
template <Color Us, bool CapturesOnly>
inline void generate_pawn_moves(const Position& pos, MoveList& move_list, Bitboard target_mask) noexcept {
    constexpr Color Them = opponent_color(Us);
    constexpr Rank PromoRank = (Us == WHITE) ? RANK_8 : RANK_1;
    constexpr int Up = (Us == WHITE) ? 8 : -8;
```

#### What the Compiler Generates in Assembly:
When `g++` compiles this template, it produces **two completely separate machine code routines**:
1. **`generate_pawn_moves<WHITE, false>`**:
   - `Us` is compiled as constant `0`.
   - `Up` is compiled as constant `+8` (`shl rax, 8`).
   - `PromoRank` is a fixed constant bitmask `0xFF00000000000000ULL`.
   - **Zero conditional jumps for color exist in the emitted binary.**
2. **`generate_pawn_moves<BLACK, false>`**:
   - `Us` is compiled as constant `1`.
   - `Up` is compiled as constant `-8` (`shr rax, 8`).
   - `PromoRank` is a fixed constant bitmask `0x00000000000000FFULL`.
3. **`CapturesOnly == true`**:
   - In Quiescence Search, `generate_captures(pos, move_list)` instantiates `CapturesOnly = true`.
   - The compiler detects that single pushes and double pushes are enclosed in `if constexpr (!CapturesOnly)` and **completely removes quiet pawn generation code from the binary!**

---

### 7.3 Pawn Move Generation

1. **Single Pushes**:
   - White: `(pawns << 8) & ~occ & target_mask`
   - Black: `(pawns >> 8) & ~occ & target_mask`
   - Promoted singles push 4 moves (`PROMO_QUEEN`, `PROMO_ROOK`, `PROMO_BISHOP`, `PROMO_KNIGHT`).
2. **Double Pushes**:
   - Pawns on rank 2 pushed through rank 3 to rank 4 without blockers.
3. **Diagonal Captures**:
   - Precomputed via `pawn_attacks(Us, from) & target_mask & enemies`.
4. **En Passant Discovered Check Corner Case**:
   - If both the moving pawn and the captured pawn vanish from rank 5 (White) or rank 4 (Black), they might open a horizontal ray between an enemy Rook/Queen and the friendly King!
   - TigerFish verifies this directly via bitwise simulation:
     ```cpp
     Bitboard test_occ = (occ ^ (1ULL << from) ^ (1ULL << cap_sq)) | (1ULL << ep_sq);
     Bitboard enemy_sliders = pos.pieces[make_piece(Them, PT_ROOK)] | pos.pieces[make_piece(Them, PT_QUEEN)];
     if (!(rook_attacks(ksq, test_occ) & enemy_sliders)) {
         move_list.push(Move(from, ep_sq, MoveFlags::EN_PASSANT));
     }
     ```

---

### 7.4 Piece Move Generation & Pin Ray Filtering

For Knights, Bishops, Rooks, and Queens:
```cpp
template <PieceType Pt, bool CapturesOnly>
inline void generate_piece_moves(const Position& pos, MoveList& move_list, Bitboard target_mask) noexcept {
    ...
    if constexpr (Pt == PT_KNIGHT) {
        if (pinned & (1ULL << from)) continue; // Pinned knights can NEVER move!
        attacks = knight_attacks(from);
    } else if constexpr (Pt == PT_BISHOP) {
        attacks = bishop_attacks(from, occ);
        if (pinned & (1ULL << from)) attacks &= line_through(ksq, from);
    }
```
If a bishop, rook, or queen is in `pos.state->pinned`, its attack bitboard is bitwise ANDed with `line_through(ksq, from)`. A pinned piece can only move along the axis connecting it to the king; off-axis moves are filtered out at the bitboard level!

---

### 7.5 King Moves & Direct Legal Castling

- **King Moves**: The King generates moves to `king_attacks(ksq)` excluding squares attacked by enemy pieces (`!is_square_attacked(pos, to, occ_without_king, them)`).
- **Castling**:
  - Validated only when NOT in check (`pos.state->checkers == 0`).
  - Verifies that intermediate squares (`F1`, `G1` or `D1`, `C1`) are completely unoccupied and not attacked by enemy pieces.

---

### 7.6 Master Legal Move Dispatcher: Double-Check vs. Single-Check vs. No-Check

```text
Inspect pos.state->checkers:
├── Double Check (checkers >= 2):
│   └── ONLY King moves are generated! All other pieces are skipped.
│
├── Single Check (checkers == 1):
│   └── Target Mask = Checker Square | Squares Between King and Checker
│       (Non-king pieces CAN ONLY capture the checker or block the check ray!)
│
└── No Check:
    └── Target Mask = All squares not occupied by friendly pieces
```

Because moves are constrained to `target_mask` before being generated, **every move generated is 100% legal**. The engine never generates an illegal move and never calls an expensive `is_legal()` function!

---

### 7.7 Ultra-Fast Pure Bitboard Move Counting (8+ Billion NPS in Perft)

At depth 1 of a Perft calculation, the goal is merely to count the total number of legal moves.
Instead of pushing moves into a `MoveList` array in RAM and returning `move_list.size()`, `count_legal_moves(pos)` computes legal moves directly in CPU registers:
```cpp
count += popcount(knight_attacks(sq) & target_mask);
```
Using the single-cycle `POPCNT` instruction without allocating or writing moves to RAM allows TigerFish 2.0 to achieve **8.06 Billion NPS** in Perft benchmarking!

---

# 8. Lockless Cache-Aligned Transposition Table (`tt.cpp`)

[`engine/tt.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/tt.cpp) caches previously searched positions.

### 8.1 64-Byte CPU Cacheline Cluster Topology

CPU memory controllers transfer data from RAM in **64-byte cachelines**. TigerFish groups entries into 64-byte `TTCluster` structures containing exactly 4 entries:

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        TTCluster (64 Bytes)                            │
├──────────────────┬──────────────────┬──────────────────┬───────────────┤
│  Entry 0 (16B)   │  Entry 1 (16B)   │  Entry 2 (16B)   │ Entry 3 (16B) │
└──────────────────┴──────────────────┴──────────────────┴───────────────┘
```

```cpp
struct alignas(16) TTClusterEntry {
    uint64_t key;  // 64-bit: zobrist_key ^ data
    uint64_t data; // 64-bit packed payload
};

struct alignas(64) TTCluster {
    TTClusterEntry entries[4]; // 4 * 16 = 64 bytes
};
```

---

### 8.2 Bitfield Packing of 64-Bit Data Payload

Each 16-byte `TTClusterEntry` stores all search information packed into a single 64-bit integer (`uint64_t data`):

```text
 63      58 57  56 55      48 47             32 31             16 15            0
┌──────────┬──────┬──────────┬─────────────────┬─────────────────┬───────────────┐
│ Age (6b) │Bound │Depth (8b)│ Static Eval(16b)│ Search Score(16b│Best Move (16b)│
└──────────┴──────┴──────────┴─────────────────┴─────────────────┴───────────────┘
```

- **Bits 0..15**: `Move` (packed 16-bit move).
- **Bits 16..31**: `int16_t score` (normalized score).
- **Bits 32..47**: `int16_t eval` (static evaluation).
- **Bits 48..55**: `int8_t depth` (search depth).
- **Bits 56..57**: `TTBound` (`BOUND_EXACT = 1`, `BOUND_LOWER = 2`, `BOUND_UPPER = 3`).
- **Bits 58..63**: `uint8_t age` (generation counter for aging out old games).

---

### 8.3 Lockless Multi-Threaded Safety via Key-Data XOR Verification

When 24 threads search simultaneously, using mutexes to protect TT entries causes catastrophic lock contention.
TigerFish uses **Key-Data XOR Checksums**:

1. **Writing**:
   ```cpp
   cluster.entries[idx].data = packed_data;
   cluster.entries[idx].key  = zobrist_key ^ packed_data;
   ```
2. **Reading**:
   ```cpp
   uint64_t d = cluster.entries[i].data;
   uint64_t k = cluster.entries[i].key ^ d;

   if (k == zobrist_key) {
       // 100% VALID READ!
       // If another thread wrote mid-read, (key ^ data) would NOT match zobrist_key!
   }
   ```
If a reader encounters a torn read (where a writer updated `data` before `key`), the XOR checksum fails. The reader simply treats it as a TT cache miss without crashing or reading corrupted data!

---

### 8.4 Single-Cycle Power-of-2 Bitmask Indexing

Instead of the slow modulo division operator (`key % cluster_count`), which requires a 30-cycle hardware divide instruction (`idiv`), TigerFish rounds the cluster count down to the nearest power of 2 ($2^N$).
Indexing is a single-cycle bitwise AND:
$$\text{cluster\_index} = \text{key} \ \& \ (\text{cluster\_count} - 1)$$

---

### 8.5 Four-Slot Cluster Replacement Policy

When storing a position into a cluster of 4 entries, `store_tt` selects the slot to overwrite:
1. If an entry matches the current `zobrist_key`, update that entry.
2. If an entry is empty (`BOUND_NONE`), use it immediately.
3. Otherwise, compute the replacement quality score:
   $$\text{quality} = \text{depth} - (\text{current\_age} - \text{entry\_age}) \times 4$$
   The entry with the lowest quality score is replaced.

---

### 8.6 Root-Relative Mate Score Normalization

Transposition table scores must be independent of the current search depth:
- **`score_to_tt(score, ply)`**: If White has mate in 3 at ply 5 ($+29997$), store $+29997 + 5 = +30002$.
- **`score_from_tt(score, ply)`**: When reading the score at ply 2, subtract 2 to restore the correct mate distance ($+30002 - 2 = +30000$).

---

# 9. Tapered Evaluation & Heuristic Scoring (`eval.cpp`)

[`engine/eval.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/eval.cpp) calculates the static heuristic evaluation of a position.

### 9.1 Game Phase Arithmetic & Linear Interpolation

A chess game transitions continuously from opening to endgame.
- **Phase Weights**:
  - Pawns: 0, Knights: 1, Bishops: 1, Rooks: 2, Queens: 4.
  - Maximum Phase (all non-pawns on board):
    $$4 \times 1 + 4 \times 1 + 4 \times 2 + 2 \times 4 = 24$$
- **Tapered Blending Formula**:
  $$\text{eval} = \frac{(\text{eval}_{MG} \times \text{phase}) + (\text{eval}_{EG} \times (24 - \text{phase}))}{24}$$

---

### 9.2 Piece-Square Tables (PST) & Incremental Accumulation

Every piece on every square has an MG and EG positional score:
- **Knights**: Rewarded for controlling center squares (d4, e4, d5, e5); penalized on board edges.
- **Bishops**: Rewarded on open diagonals; penalized on blocked pawns.
- **Kings**: Rewarded for tucking into castled corners (g1, b1) in the middlegame; rewarded for rushing to the center in the endgame.
- **Incremental Updates**: PST values are accumulated incrementally in `Position::put_piece` and `Position::remove_piece`. `evaluate()` retrieves `pos.state->eval_mg` and `eval_eg` in $O(1)$ time without scanning 64 squares!

---

### 9.3 Pawn Structure Analysis & The Dedicated 64K Pawn Cache

Pawn structure changes infrequently. TigerFish caches pawn evaluations in a 65,536-entry hash table `PAWN_TABLE`:
- **Doubled Pawns**: Penalized -15 MG / -25 EG for each extra pawn on the same file.
- **Isolated Pawns**: Penalized -15 MG / -20 EG if no friendly pawns occupy adjacent files.
- **Passed Pawns**: Awarded exponential bonuses as they approach promotion:
  $$\text{Bonus}_{EG} = [0, 10, 20, 35, 60, 100, 160, 0]$$

---

### 9.4 Positional Factors: Bishop Pair & Open Files

- **Bishop Pair**: +35 MG / +50 EG awarded to a side owning two or more active bishops.
- **Rook on Open File**: +25 MG / +15 EG awarded if no pawns of either color occupy the file.
- **Rook on Semi-Open File**: +12 MG / +8 EG awarded if only enemy pawns occupy the file.

---

### 9.5 King Safety: 2-Tier Pawn Shields & King Zone Attack Counters

King safety is evaluated in the middlegame:
1. **Pawn Shield Bitboards**:
   - `KING_SHIELD_1`: The 3 squares directly in front of the castled king (+15 cp per pawn).
   - `KING_SHIELD_2`: The 3 squares two ranks in front of the castled king (+8 cp per pawn).
2. **Open Files Near King**: Penalized -20 cp for fully open files and -10 cp for semi-open files adjacent to the king.
3. **King Zone Attackers**: Fired attacks into the 9-square zone surrounding the king are counted. Penalties scale quadratically:
   ```cpp
   constexpr int ATTACKER_PENALTY[9] = { 0, 0, -10, -25, -50, -80, -120, -170, -230 };
   ```

---

### 9.6 External Machine Learning / Neural Network Hook

```cpp
using EvalHookFunc = int (*)(const Position&);
inline EvalHookFunc ACTIVE_EVAL_HOOK = nullptr;
```
If a machine learning model or neural evaluation function is registered, `evaluate()` delegates directly to `ACTIVE_EVAL_HOOK(pos)` without requiring engine recompilation.

---

# 10. Tournament-Grade Search Engine & Modern Pruning (`search.cpp`)

[`engine/search.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/search.cpp) is the decision-making engine of TigerFish 2.0.

### 10.1 Iterative Deepening & Exponential Aspiration Windows

Search runs depth by depth ($1, 2, 3, \dots, D$).
- **Aspiration Window**: Instead of searching $(-\infty, +\infty)$, search within a narrow window around the previous iteration's score:
  $$[\alpha, \beta] = [\text{score} - 25, \text{score} + 25]$$
- **Exponential Widening**: If the score fails low or fails high, widen the window:
  $$\delta \leftarrow \delta + \frac{\delta}{2}$$
  $$\alpha \leftarrow \alpha - \delta, \quad \beta \leftarrow \beta + \delta$$
  Opens to $(-\infty, +\infty)$ only if $\delta > 500$ cp.

---

### 10.2 Quiescence Search: Stand-Pat, Dynamic Delta Pruning, & SEE Guard

Quiescence search resolves tactical volatility before returning static evaluation:
1. **Stand-Pat**: If static evaluation $\ge \beta$, return $\beta$ immediately.
2. **Dynamic Delta Pruning**: If `stand_pat + max_enemy_piece_value + 200 < alpha`, skip searching further captures.
3. **Guarded SEE Pruning**:
   ```cpp
   if (!m.is_promotion()) {
       if (see(pos, m) < -50) continue;
       if (SEE_PIECE_VALUES[victim] < SEE_PIECE_VALUES[attacker] && see(pos, m) < 0) continue;
   }
   ```
   Losing trades (such as Queen capturing a defended Pawn) are pruned immediately.

---

### 10.3 Principal Variation Search (PVS Alpha-Beta)

- **First Move (PV Move)**: Searched with the full window $(\alpha, \beta)$.
- **Subsequent Moves**: Searched with a zero-window (null-window) scout search:
  $$\text{score} = -\text{alpha\_beta}(\dots, -\alpha - 1, -\alpha, \text{reduced\_depth})$$
- **Re-Search**: If the scout search fails high ($\text{score} > \alpha$), re-search with the full window $(\alpha, \beta)$ at unreduced depth.

---

### 10.4 The Complete Pruning Hierarchy

```text
Top of Alpha-Beta:
│
├── 1. Transposition Table Cutoff (depth >= current_depth)
│
├── 2. Draw Detection (Repetitions, 50-move rule) & Mate Distance Pruning
│
├── 3. Reverse Futility Pruning (RFP / Static Null Move Pruning, depth <= 8)
│      If (static_eval - (improving ? 65 : 85) * depth >= beta) return beta;
│
├── 4. Null Move Pruning (NMP, depth >= 3)
│      Pass turn; search with reduction R = 4 + depth / 4.
│      If null_score >= beta: cutoff branch!
│
├── 5. ProbCut (depth >= 5)
│      Search winning captures with high beta margin. If fail-high, prune branch.
│
├── 6. Razor Pruning (depth <= 2)
│      If (static_eval + 300 * depth <= alpha) drop directly into Quiescence!
│
├── 7. Check Extension (depth + 1 when king is in check)
│
├── 8. Singular Extensions (SE, depth >= 8)
│      Test if TT move is uniquely dominant; if so, extend depth by +1 or +2.
│
├── 9. Fast-Path TT Move Verification
│      Search TT move before generating or scoring other candidate moves!
│
└── 10. Move Loop Pruning:
       ├── Futility Pruning: At depth <= 3, skip quiets that cannot reach alpha.
       ├── Late Move Pruning (LMP): At depth <= 4, skip quiets beyond move index N.
       ├── History Pruning: Skip quiets with severely negative history.
       ├── Quiet Negative SEE: Prune quiets that hang pieces (SEE < -25 * depth).
       └── Capture SEE Pruning: Prune losing captures at shallow depths.
```

---

### 10.5 Singular Extensions (SE) & Multi-Cut Logic

- **The Problem**: Alpha-beta pruning might cut off a subtree prematurely if only ONE move saves the position while all others lose.
- **The Singular Test**: At depth $\ge 8$, if the TT move is an exact or lower-bound score, run a reduced search at depth $(d - 1) / 2$ with search window:
  $$\beta_{\text{singular}} = \text{tt\_score} - 2 \times d$$
  with the TT move **excluded** from search.
- **Extension**: If no alternative move can achieve $\ge \beta_{\text{singular}}$, the TT move is singular. Extend search depth by **+1 ply** (or +2 plies for double extension)!
- **Multi-Cut**: If an alternative move achieves $\ge \beta_{\text{singular}} \ge \beta$, the node fails high immediately without searching further!

---

### 10.6 Fast-Path TT Move Verification & Instant Cutoff

TigerFish 2.0 searches the TT move **before generating legal moves or scoring candidate moves**:
```cpp
if (is_valid_tt_move(pos, tt_move) && tt_move != excluded_move) {
    make_move(pos, tt_move, state);
    int score = -alpha_beta(pos, -beta, -alpha, depth - 1 + extension, ply + 1, ctx, tt);
    undo_move(pos, tt_move);
    if (score >= beta) {
        // Instant Beta Cutoff! ZERO other moves were generated or scored!
        return score;
    }
}
```
If the TT move causes a beta cutoff, move generation is skipped entirely, saving millions of instructions!

---

### 10.7 Static Exchange Evaluation (SEE): The Recursive Swap Engine

`see(pos, move)` simulates a sequence of captures on a single square to determine if a capture is tactically winning or losing:
1. Picks the least valuable attacker to the target square (`PT_PAWN` $\to$ `PT_KNIGHT` $\to$ `PT_BISHOP` $\to$ `PT_ROOK` $\to$ `PT_QUEEN` $\to$ `PT_KING`).
2. Removes the attacker from the occupancy bitboard, exposing any X-ray sliding attackers behind it.
3. Propagates the scores backward using Negamax minimax:
   $$\text{gain}[d - 1] = -\max(-\text{gain}[d - 1], \text{gain}[d])$$

---

### 10.8 Staged Move Ordering & Dynamic Heuristics

Moves are searched in descending order of tactical likelihood:
1. **TT Move**: Best move from Transposition Table (Score: 2,000,000).
2. **Good Captures**: Ordered by MVV-LVA (Score: 1,000,000 + MVV-LVA).
3. **1st Killer Move**: Non-capture that caused a cutoff at this ply (Score: 900,000).
4. **2nd Killer Move**: Previous killer move at this ply (Score: 800,000).
5. **Quiet Moves**: Ordered by Butterfly History + Continuation History.
6. **Bad Captures**: Losing trades where SEE $< 0$.

---

### 10.9 3D Continuation History (Conthist) System

A move's quality often depends on the opponent's previous move.
TigerFish maintains a 3-dimensional table:
$$\text{cont\_history}[15][64][64] \quad \rightarrow \quad [\text{prev\_piece}][\text{prev\_to}][\text{curr\_to}]$$
When the opponent moves a piece to `prev_to`, the continuation history score for moves to `curr_to` is added to the move's ordering score.

---

### 10.10 History Gravity with Malus Formula

- **Gravity Formula (Prevents Integer Overflow)**:
  $$\text{history} \leftarrow \text{history} + \text{bonus} - \frac{\text{history} \times |\text{bonus}|}{16384}$$
- **History Malus (Penalizes Failing Moves)**:
  When a quiet move causes a beta cutoff:
  - The cutoff move receives `+bonus`.
  - **All preceding quiet moves that were searched and failed receive `-bonus`.**

---

### 10.11 Improving-Flag Late Move Reductions (LMR)

Quiet moves searched late in the move list are searched at reduced depth using the precomputed logarithmic formula:
$$\text{reduction} = \lfloor 0.75 + \frac{\ln(\text{depth}) \times \ln(\text{move\_index})}{2.25} \rfloor$$
- **Improving Flag**:
  $$\text{improving} = (\text{static\_eval} \ge \text{eval\_history}[\text{ply} - 2])$$
  - If **improving**: `reduction++` (position is strong, prune enemy replies aggressively).
  - If **not improving**: `reduction--` (position is in danger, search candidate moves more thoroughly).

---

### 10.12 Multithreaded SMP: Lazy SMP, Persistent WorkerPool, & YBWC

TigerFish supports parallel search across two architectures:
1. **Persistent `WorkerPool` (Lazy SMP)**:
   - Helper threads sleep on `std::condition_variable` between moves.
   - When a search starts, threads wake up in microseconds without the latency of spawning OS threads.
   - Threads search asynchronously, sharing the lockless Transposition Table.
2. **Root-Move Parallel Search (Young Brothers Wait Concept / YBWC)**:
   - Searches the oldest brother (PV move) on the main thread to establish $\alpha$.
   - Distributes young brothers across worker threads using zero-window scout searches.

---

### 10.13 CPU Topology & Thread Affinity Management on Windows

- **The Problem**: In sandboxed environments (IDE background tasks, Docker containers), Windows restricts processes to specific CPU core subsets via Job Objects. Calling `SetThreadAffinityMask` on a restricted core causes immediate process termination (`exit code 1`).
- **The TigerFish Fix**:
  ```cpp
  DWORD_PTR proc_mask = 0, sys_mask = 0;
  if (GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask) && proc_mask != 0) {
      DWORD_PTR target = (static_cast<DWORD_PTR>(1) << (t % 64));
      if (target & proc_mask) {
          SetThreadAffinityMask(GetCurrentThread(), target);
      }
  }
  ```
  Guarantees 100% crash-free execution under any OS container or Job Object.

---

# 11. Multi-Threaded Work-Stealing Perft Engine (`perft.h`)

[`engine/perft.h`](file:///d:/PROJECTS/chess_engine_v0/engine/perft.h) validates move generation correctness across millions of positions.

### 11.1 Dynamic Work-Stealing & Adaptive Task Chunking

- **Tree Splitting**: Pre-generates tasks 2–3 plies deep into a flat array of `PerftTask` structs.
- **Adaptive Chunking**: Workers claim batches of 16 down to 1 task dynamically using atomic `fetch_add`:
  $$\text{chunk} = \operatorname{clamp}\left(\frac{\text{remaining\_tasks}}{\text{threads} \times 4}, 1, 16\right)$$
  Prevents memory bus contention when 24 threads access atomic counters simultaneously.

---

### 11.2 Lockless Perft TT & Split-Tree Synchronization

Perft uses a dedicated lockless transposition table `PerftTT` caching subtree leaf counts by `(zobrist_key, depth)`. Duplicate game positions transpose into instant $O(1)$ lookups.

---

# 12. Auxiliary Systems: Opening Book, Tablebases, & Protocols

### 12.1 Polyglot Opening Book Reader (`opening_book.cpp`)
Parses binary `.bin` books. Converts 64-bit Polyglot Zobrist keys and selects moves using weighted random tournament probabilities.

### 12.2 Endgame Tablebases (`tablebase.cpp`)
Probes Syzygy WDL (Win/Draw/Loss) metrics for positions with $\le 5$ pieces on the board.

### 12.3 Reinforcement Learning Interface (`rl_interface.cpp`)
Exposes `ACTIVE_EVAL_HOOK` to allow PyTorch or external neural models to evaluate positions directly.

### 12.4 Interactive CLI Protocol (`cli.cpp`)
Line-oriented protocol for direct terminal control:
- `newgame [fen]`: Resets board state.
- `apply <uci>`: Plays a move on the board.
- `undo`: Takes back a move.
- `best <depth> [time_ms]`: Searches position and outputs best move and score.

### 12.5 UCI Protocol Parser (`uci.cpp`)
Standard protocol used by chess GUIs (Arena, Cutechess, Banksia):
- `uci`, `isready`, `position startpos moves ...`, `go depth 12 movetime 1000`, `stop`, `quit`.

### 12.6 Python Lichess Bridge (`lichess_bot.py`)
Asynchronous Python bridge connecting TigerFish to Lichess:
- Dynamic clock budget formula:
  $$\text{budget} = \frac{\text{clock\_remaining} - \text{lag\_buffer}}{25.0} + (\text{increment} \times 0.75)$$
- Low-time panic thresholds ($< 800$ ms) to prevent flagging on Lichess.

---

# 13. Master Data-Flow & Execution Lifecycles

### 13.1 Complete Search Request Lifecycle

```text
[GUI / Lichess Bot sends "go depth 12 movetime 1000"]
                    │
                    ▼
uci.cpp: parse_go() -> search_position()
                    │
                    ├── 1. Probes Polyglot Opening Book (if hit, returns book move immediately)
                    │
                    ├── 2. Iterative Deepening Loop (depth = 1 .. 12)
                    │      │
                    │      ├── Aspiration Window: [best_score - 25, best_score + 25]
                    │      │
                    │      └── alpha_beta(pos, alpha, beta, depth, ply=0, ctx, tt)
                    │             │
                    │             ├── TT Probe & Cutoff
                    │             ├── Reverse Futility Pruning (RFP)
                    │             ├── Null Move Pruning (NMP)
                    │             ├── Singular Extensions (SE)
                    │             │
                    │             ├── Fast-Path TT Move Search (Cutoff before movegen!)
                    │             │
                    │             ├── Legal Move Generation: generate_legal_moves(pos, ml)
                    │             │     └── template <Us, ALL_MOVES> specialized assembly
                    │             │
                    │             ├── Move Ordering: TT > Good Captures > Killers > Conthist > Quiets
                    │             │
                    │             ├── Move Loop:
                    │             │     ├── Futility / LMP / SEE Pruning
                    │             │     ├── StateInfo new_state; (allocated on local stack)
                    │             │     ├── make_move(pos, move, new_state);
                    │             │     ├── alpha_beta(pos, -beta, -alpha, depth - reduction);
                    │             │     └── undo_move(pos, move);
                    │             │
                    │             └── History Gravity Updates & TT Storage (store_tt)
                    │
                    ▼
Emits: "bestmove e2e4"
```

---

### 13.2 Complete Move Mutation Lifecycle

```text
Position pos (state points to st3)
           │
           │  1. Declare StateInfo new_state on local execution stack
           ▼
make_move(pos, move, new_state)
           │
           ├── Link: new_state.previous = pos.state (st3)
           ├── Advance: pos.state = &new_state
           │
           ├── Bitboard Updates:
           │     pos.pieces[piece] ^= (1ULL << from) | (1ULL << to)
           │     pos.colors[us]    ^= (1ULL << from) | (1ULL << to)
           │     pos.mailbox[from]  = NO_PIECE
           │     pos.mailbox[to]    = piece
           │
           ├── Zobrist Hash XOR:
           │     pos.state->zobrist_key ^= ZOBRIST_PIECES[piece][from]
           │     pos.state->zobrist_key ^= ZOBRIST_PIECES[piece][to]
           │     pos.state->zobrist_key ^= ZOBRIST_SIDE
           │
           ├── Incremental PST Evaluation Update:
           │     pos.state->eval_mg += PST_MG[piece][to] - PST_MG[piece][from]
           │
           └── Compute Checkers: update_checkers(pos)
```

---

### 13.3 Complete Move Generation Lifecycle

```text
generate_legal_moves(pos, move_list)
                    │
                    ├── 1. Lazy Pin Computation: update_pins(pos)
                    │
                    ├── 2. Generate King Moves (always legal, tests enemy attacked squares)
                    │
                    ├── 3. Branch on Check Count:
                    │      ├── Double Check (checkers >= 2):
                    │      │     └── Return immediately! (Only King can move)
                    │      │
                    │      ├── Single Check (checkers == 1):
                    │      │     └── target_mask = checker_sq | squares_between(ksq, checker_sq)
                    │      │
                    │      └── No Check:
                    │            └── target_mask = ~pos.colors[us]
                    │
                    ├── 4. Generate Pawns: generate_pawn_moves<Us, CapturesOnly>(pos, ml, target_mask)
                    │      ├── Single & double pushes
                    │      ├── Diagonal captures
                    │      └── En passant (with discovered horizontal pin guard)
                    │
                    └── 5. Generate Pieces: generate_piece_moves<Pt, CapturesOnly>(pos, ml, target_mask)
                           ├── Knights: skipped entirely if in pos.state->pinned
                           └── Sliders: attack mask ANDed with line_through(ksq, from) if pinned
```

---

# 14. Developer Maintenance, Tuning & Debugging Guide

### 14.1 The 5 Critical Invariants of the Codebase

When extending or maintaining TigerFish 2.0, you must preserve these five invariants:
1. **Never Allocate Heap Memory in Search**: Never use `new`, `malloc`, or dynamic vector resizing in `search.cpp`, `eval.cpp`, `position.cpp`, or `movegen.cpp`. Everything must be allocated on the call stack or preallocated at engine startup.
2. **Never Put `alignas(64)` on Global/Static Variables**: In MinGW-W64 GCC on Windows, PE-COFF section alignment does not guarantee 64-byte alignment under ASLR. This causes `0xC0000005 (Access Violation)` crashes. Use standard 8-byte or 16-byte alignment on static variables.
3. **Never Use `std::vector<std::atomic<T>>`**: `std::atomic` is non-copyable and non-movable. Using it inside a vector causes undefined compiler memory layouts. Use fixed arrays like `std::atomic<uint64_t> counts[256]` instead.
4. **Never Shift 64-Bit Integers by 64 or More**: `1ULL << 64` is undefined behavior in C++. Always guard shifts with `if (shift < 64)`.
5. **Always Guard Thread Affinity**: Never call `SetThreadAffinityMask` without first checking `GetProcessAffinityMask` to prevent termination under Windows Job Objects.

---

### 14.2 How to Add a New Evaluation Term

1. Open [`engine/eval.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/eval.cpp).
2. Define your MG and EG bonus constants:
   ```cpp
   constexpr int ROOK_ON_SEVENTH_MG = 20;
   constexpr int ROOK_ON_SEVENTH_EG = 35;
   ```
3. Add the logic inside `eval_pieces(pos, mg, eg)`:
   ```cpp
   Bitboard w_rooks = pos.pieces[W_ROOK] & RANK_MASKS[RANK_7];
   mg += popcount(w_rooks) * ROOK_ON_SEVENTH_MG;
   eg += popcount(w_rooks) * ROOK_ON_SEVENTH_EG;
   ```
4. Compile with `g++ -O3 -march=native -std=c++20 -pthread engine/main.cpp -o game.exe`.
5. Run a 100-game match against the previous binary using Cutechess to measure Elo impact.

---

### 14.3 How to Tune Search Pruning Thresholds

All pruning margins are defined as linear or logarithmic functions in `search.cpp`:
- **RFP Margin**: `(improving ? 65 : 85) * depth`
- **NMP Reduction**: `4 + depth / 4 + std::min(3, (static_eval - beta) / 200)`
- **LMR Reduction**: `0.75 + std::log(d) * std::log(m) / 2.25`
- **Singular Beta**: `tt_score - 2 * depth`

To tune these values:
1. Adjust the multipliers in increments of $\pm 5\%$.
2. Verify that tactical test suites (STS, WAC, Arasan) do not regress in solved puzzles.
3. Run fixed-time-control self-play matches.

---

### 14.4 Systematic Debugging: How to Isolate Movegen or Search Bugs

1. **Move Generation Mismatch**:
   Run perft against known Stockfish node counts:
   ```bash
   ./game.exe perft 6
   ```
   If the count differs:
   - Run `perft 1` for each root move (`divide`).
   - Compare the per-move counts against Stockfish to identify the exact branch that diverges.
2. **Search Non-Determinism**:
   Run search with 1 thread:
   ```bash
   ./game.exe
   position startpos
   go depth 10
   ```
   Verify that `nodes` and `bestmove` are 100% identical on repeated runs.

---

*TigerFish 2.0 Comprehensive Architecture & Developer Guide — Maintained in [`docs/CODEBASE_IMPLEMENTATION_GUIDE.md`](file:///d:/PROJECTS/chess_engine_v0/docs/CODEBASE_IMPLEMENTATION_GUIDE.md).*
