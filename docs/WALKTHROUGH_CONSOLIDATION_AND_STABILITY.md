# Walkthrough — Codebase Consolidation & Single Authoritative Engine

This walkthrough details the consolidation of TigerFish into a single authoritative C++20 engine under [`engine/`](file:///d:/PROJECTS/chess_engine_v0/engine/), the migration of duplicate files and prototypes to an external backup directory, the resolution of low-level compiler alignment issues, and the complete verification suite results.

---

## 1. Directory Structure Cleanup & External Backup

All redundant directories, duplicate builds, and scratch prototypes have been migrated to an external backup directory outside the workspace root at `D:\PROJECTS\tigerfish_legacy_backup\`.

### External Backup Destination (`D:\PROJECTS\tigerfish_legacy_backup\`)
- **`newengine/`**: Full duplicate directory backed up, including all alternate build binaries (`game_flto.exe`, `game_opt.exe`, `game_warn.exe`, `perft.exe`, `profile_bench.exe`, etc.).
- **`scratch/`**: Full scratch directory backed up, including `Gigantua/`, `giga_gcc.exe`, experimental depth-3 inlining tests, and lock prototypes.
- **Obsolete Root Binaries & Skills**:
  - `game_new.exe`
  - `profile_bench.exe`
  - `cpp-concurrency.skill`
- **Buffer Directories**:
  - `.freebuff/`

### Clean Repository Layout (`d:\PROJECTS\chess_engine_v0\`)
- [`engine/`](file:///d:/PROJECTS/chess_engine_v0/engine/): **Single authoritative C++20 engine** (all modern DOD search, evaluation, move generation, transposition table, perft, UCI, and CLI logic).
- [`archive/`](file:///d:/PROJECTS/chess_engine_v0/archive/): Retained for historical baseline comparisons (`archive/engine_v0/`).
- [`tools/`](file:///d:/PROJECTS/chess_engine_v0/tools/): Benchmark and profiler tools updated to include directly from [`engine/`](file:///d:/PROJECTS/chess_engine_v0/engine/).
- [`Test/`](file:///d:/PROJECTS/chess_engine_v0/Test/): Full test suites, matches, and baselines (`tigerfish_vs_stockfish.py` now targets root `game.exe`).
- [`docs/`](file:///d:/PROJECTS/chess_engine_v0/docs/): Technical documentation, engineering standards, audit plans, and walkthroughs.
- `game.exe`: Production executable compiled directly from [`engine/main.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/main.cpp).
- `lichess_bot.py`, `lichess_multibot.py`, `test_bot_startup.py`: Live bot bridge infrastructure.

---

## 2. Low-Level Compiler Fixes & Stability Hardening

During testing under Windows with ASLR (Address Space Layout Randomization), two critical stability issues were identified and resolved:

1. **MinGW PE-COFF Section Alignment**:
   - In [`engine/eval.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/eval.cpp) and [`engine/search.cpp`](file:///d:/PROJECTS/chess_engine_v0/engine/search.cpp), static tables declared with `alignas(64)` caused intermittent `0xC0000005 (Access Violation)` crashes when ASLR loaded sections without 64-byte boundary guarantees.
   - Removed unnecessary `alignas(64)` from static arrays (`WHITE_PASSED_SPAN`, `BLACK_PASSED_SPAN`, `ADJACENT_FILES`, `KING_SHIELD_1/2`, `LMR_TABLE`). Modern x86-64 hardware handles 8/16-byte alignment with zero throughput penalty.
   - Guarded against undefined behavior in 64-bit shift `1ULL << ((r + 1) * 8)` when rank $r = 7$.
2. **Standard-Compliant Atomic Usage**:
   - In [`engine/perft.h`](file:///d:/PROJECTS/chess_engine_v0/engine/perft.h), replaced invalid `std::vector<std::atomic<uint64_t>>` with a fixed-size `std::atomic<uint64_t> root_counts[256]` array. In C++, `std::atomic` is neither copyable nor movable; using it as a vector element caused undefined memory layouts.
   - Passed `Position& root_pos` by reference to avoid unnecessary copy construction.

---

## 3. Verification & Validation Results

### A. Repeated Process Invocation Stability
Ran 10 back-to-back independent process executions of `game.exe perft startpos 5`:
- **Result**: **10 / 10 passed** with exit code `0` and zero crashes.
- Average throughput: **320+ Million NPS** single-threaded.

### B. Full 24-Thread Multi-Threaded Perft Suite
`game.exe perft bench` across all 8 standard benchmark positions at 1, 16, and 24 threads:

| Position | Depth | Expected Nodes | 1 Thread | 16 Threads | 24 Threads | Status |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| **Startpos** | 5 | 4,865,609 | 13.65 ms | 3.37 ms (1.44B NPS) | 4.73 ms (1.03B NPS) | **PASS** |
| **Startpos** | 6 | 119,060,324 | 323.40 ms | 41.87 ms (2.84B NPS) | 29.04 ms (**4.10B NPS**) | **PASS** |
| **Kiwipete** | 4 | 4,085,603 | 9.90 ms | 2.59 ms (1.58B NPS) | 5.09 ms (803M NPS) | **PASS** |
| **Kiwipete** | 5 | 193,690,690 | 360.37 ms | 43.65 ms (4.44B NPS) | 28.87 ms (**6.71B NPS**) | **PASS** |
| **Pos 3** | 4 | 43,238 | 0.89 ms | 2.81 ms | 1.85 ms | **PASS** |
| **Pos 4** | 4 | 422,333 | 1.26 ms | 1.11 ms | 2.34 ms | **PASS** |
| **Pos 5** | 4 | 2,103,487 | 5.13 ms | 1.97 ms (1.07B NPS) | 2.19 ms (961M NPS) | **PASS** |
| **Pos 6** | 4 | 3,894,594 | 6.52 ms | 2.11 ms (1.84B NPS) | 2.74 ms (1.42B NPS) | **PASS** |

**Result**: **24 / 24 combinations passed with 100% exact node counts.**

### C. Lichess Bot Preflight Check
`python test_bot_startup.py`:
- Handshake (newgame): **OK in 15.3ms**
- Hyperbullet (30s+0): **2.7ms** (`b1c3`) — **PASS**
- Bullet Scramble (2s+1s): **2.9ms** (`b8c6`) — **PASS**
- Blitz Move (3m+2s): **49.4ms** (`d2d4`) — **PASS**
- Rapid Critical (10m): **114.1ms** (`d7d5`) — **PASS**
- Fixed Shallow Depth: **1.4ms** (`g1f3`) — **PASS**
- **Result**: **ALL 5 PREFLIGHT CHECKS PASSED**.

### D. Full 87-Case Regression Suite
`python Test/regression/run_all_tests.py`:
- Perft: **6/6 passed**
- Rules: **8/8 passed**
- Evaluation: **4/4 passed**
- Eval Advanced: **17/17 passed**
- Rules Advanced: **25/25 passed**
- Search Advanced: **19/19 passed**
- Tactics baseline: **3/3 solved**
- UCI handshake: **PASS**
- Analysis CLI: **PASS**
- **Result**: **87 / 87 tests passed** (saved `baseline_v38.json`).
