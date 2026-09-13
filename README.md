# 🐅 TigerFish 2.0 Chess Engine

[![Play on Lichess](https://img.shields.io/badge/Play_on-Lichess-swords?style=for-the-badge&logo=lichess&color=2e7d32)](https://lichess.org/@/TigerFish-BOT)  
🎮 **Play Live on Lichess**: [https://lichess.org/@/TigerFish-BOT](https://lichess.org/@/TigerFish-BOT)

A high-performance, commercial-grade chess engine written in **pure modern C++20** using **Data-Oriented Design (DOD)** and **Single-Translation-Unit** architecture.

TigerFish 2.0 features hardware-accelerated **BMI2 / PEXT bitboard attack calculation**, lockless 64-byte cache-aligned **Transposition Tables**, pure popcount legal move counting delivering up to **8.06 Billion NPS** in parallel perft, and an advanced search pipeline featuring **Principal Variation Search (PVS)**, **Young Brothers Wait Concept (YBWC)** root parallelism, **Dynamic Quiescence Delta Pruning**, logarithmic **Late Move Reductions (LMR)**, and persistent thread pools.

---

## ⚡ Performance Benchmarks & Milestones

TigerFish 2.0 was benchmarked on an Intel Core i7-14650HX (16 physical cores / 24 logical threads) running under AC power:

### 1. Multi-Threaded Perft Benchmark (24 Threads)

| Test Position | Depth | Legal Nodes | 1 Thread (Baseline) | 16 Threads (P-Cores) | 24 Threads (All Cores) | Peak Throughput | Status |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Startpos** | 5 | 4,865,609 | 10.01 ms (486.2M NPS) | 1.92 ms (2,533M NPS) | 1.84 ms (2,643M NPS) | **2.64 Billion NPS** | **PASS** |
| **Startpos** | 6 | 119,060,324 | 217.95 ms (546.3M NPS) | 29.90 ms (3,982M NPS) | 24.30 ms (4,899M NPS) | **4.90 Billion NPS** | **PASS** |
| **Kiwipete** | 4 | 4,085,603 | 7.65 ms (534.3M NPS) | 2.44 ms (1,671M NPS) | 1.96 ms (2,089M NPS) | **2.09 Billion NPS** | **PASS** |
| **Kiwipete** | 5 | 193,690,690 | 235.34 ms (823.0M NPS) | 29.51 ms (6,564M NPS) | 24.04 ms (8,056M NPS) | **8.06 Billion NPS** | **PASS** |
| **Position 5** | 4 | 2,103,487 | 4.16 ms | 1.34 ms | 1.60 ms | **1.57 Billion NPS** | **PASS** |
| **Position 6** | 4 | 3,894,594 | 4.75 ms | 1.45 ms | 1.68 ms | **2.68 Billion NPS** | **PASS** |

> **Stockfish Parity**: Verified 100% exact node count parity against official `Stockfish 16` (`tools/perft/stockfish_perft_comparator.exe startpos 4`).

### 2. Search Engine Benchmark (`game.exe bench 14 16`)

- **16-Thread Lazy SMP**: **43.9 Million NPS** (16,901,121 nodes in 384.8 ms).
- **16-Thread Root-Parallel YBWC (`--ybwc`)**: **3,130,623 nodes** in 390.9 ms (**81.5% fewer nodes** visited to achieve identical tactical depth through zero-window scout cutoffs).
- **Bot Clock Preflight**: UltraBullet depth 8 in **2.0 ms**; Blitz depth 12 in **10.0 ms**; Rapid depth 14 in **67.6 ms**.

---

## ✨ Key Technical Highlights

* **Pure C++20 Data-Oriented Design (DOD)**: Lightweight ~140-byte `Position` struct and stack-linked `StateInfo` nodes. Zero heap allocation overhead in search and movegen.
* **PEXT / BMI2 Hardware Slider Attacks**: Uses `_pext_u64` hardware instructions for single-cycle Bishop and Rook attack queries with flat, dense lookup tables (<850 KB total RAM).
* **Hardware Bitboard Leaf Popcount**: At leaf plies, legal moves are counted directly through bitboard intersections and hardware `__builtin_popcountll`, bypassing move struct serialization completely.
* **Lockless 64-Byte Cacheline Transposition Table**: 16-byte `TTClusterEntry` stores `key ^ data` packed payload. 4 entries per 64-byte `TTCluster` matching CPU cache line width. 100% thread-safe without mutex bottlenecks.
* **Young Brothers Wait Concept (YBWC)**: Principal variation move is evaluated sequentially on the main thread to set $\alpha$; young brothers are scouted in parallel using zero-window bounds across worker threads.
* **Persistent WorkerPool**: Helper threads sleep on `std::condition_variable` between moves, completely eliminating thread spawning overhead during time-critical matches.
* **Dynamic Quiescence Delta Pruning**: Inspects remaining opponent material and pawn promotions to dynamically prune impossible tactical lines in quiescence search.
* **Polyglot & Syzygy Integration**: Standard Polyglot opening book probing (`0x463b96181691fc9c` startpos key verified) and optimal Syzygy WDL endgame move selection.

---

## 🚀 Building & Execution Guide

### 1. Compile the C++ Engine
Compile `engine/main.cpp` using GCC with modern C++20 and native hardware instructions:
```bash
g++ -O3 -march=native -std=c++20 -pthread engine/main.cpp -o game.exe
```

### 2. Execution Modes & Commands

- **Standard UCI Protocol Mode (Lichess Bot / GUI)**:
  ```bash
  ./game.exe uci
  # or simply:
  ./game.exe
  ```

- **Interactive Persistent IPC Session (Python Bridge)**:
  ```bash
  ./game.exe interactive
  ```
  *Commands over stdin: `newgame [fen]`, `apply <uci>`, `undo`, `best <depth>`, `quit`.*

- **Multi-Core Benchmark Suite**:
  ```bash
  ./game.exe bench 12 4           # Lazy SMP: depth 12 on 4 threads
  ./game.exe bench 14 16 --ybwc   # Root-Parallel YBWC: depth 14 on 16 threads
  ```

- **Parallel Work-Stealing Perft**:
  ```bash
  ./game.exe perft bench                        # Standardized 8-position multi-core suite
  ./game.exe perft startpos 6 --threads 24      # 24-thread parallel perft
  ./game.exe perft kiwipete 5 --split           # Move-by-move split node breakdown
  ./game.exe perft startpos 8 --tt --tt-size 512 # Transposition Table accelerated perft
  ```

- **Single-shot Best Move Search**:
  ```bash
  ./game.exe best "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 12
  ```

- **FEN Static Evaluation**:
  ```bash
  ./game.exe analyze "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 10
  ```

---

## 🏗️ Single-Translation-Unit Architecture

TigerFish 2.0 builds through a single clean linear C++20 cascade:

```text
engine/main.cpp
  ├── engine/cli.cpp
  ├── engine/uci.cpp
  └── engine/search.cpp
        ├── engine/eval.cpp
        ├── engine/tt.cpp
        ├── engine/opening_book.cpp
        ├── engine/tablebase.cpp
        ├── engine/rl_interface.cpp
        └── engine/movegen.cpp
              └── engine/position.cpp
                    ├── engine/bitboards.cpp
                    ├── engine/zobrist.cpp
                    └── engine/types.cpp
  └── engine/perft.h
```

### Architectural Principles
- **Strict Downward Dependency**: Every module depends strictly on the layers below it. Zero circular dependencies.
- **Zero Heap Allocations**: All search and movegen states are managed via stack-allocated linked-list frames (`StateInfo`).
- **Thread Safety**: Transposition table probing and task stealing are 100% lockless and immune to torn reads.

---

## 🧪 Testing & Verification Suite

TigerFish includes a comprehensive regression test harness covering rules, evaluations, tactical puzzles, and UCI handshakes:

```bash
# Run full 87-case regression test suite
python Test/regression/run_all_tests.py

# Run live Lichess bot clock preflight simulation
python test_bot_startup.py

# Run Stockfish Perft comparison harness
tools/perft/stockfish_perft_comparator.exe startpos 4
```
All suites pass 100% on every release.
