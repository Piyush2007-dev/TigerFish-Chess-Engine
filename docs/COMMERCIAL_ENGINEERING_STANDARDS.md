# TigerFish Chess Engine: Commercial-Grade Engineering Standards & Architectural Specification

**Document Version**: 2.0  
**Classification**: Engineering Governance, Standards & Technical Guidelines  
**Target Project**: TigerFish C++20 Chess Engine  
**Authority**: Mandatory for all engineers and AI coding agents  

---

## Table of Contents
1. [Commercial Engineering Philosophy](#1-commercial-engineering-philosophy)
2. [Codebase Architecture & Header/Forward-Declaration-Free Design](#2-codebase-architecture--headerforward-declaration-free-design)
3. [Variable Generalization & Data-Oriented Design (DOD)](#3-variable-generalization--data-oriented-design-dod)
4. [Commercial-Grade Multithreaded C++ Concurrency](#4-commercial-grade-multithreaded-c-concurrency)
5. [Testing, Benchmarking & Engine Variant Evaluation Framework](#5-testing-benchmarking--engine-variant-evaluation-framework)
6. [Git, Commit, Push & Release Workflow](#6-git-commit-push--release-workflow)
7. [The Definitive C++ Do's and Don'ts: Beginner to Advanced](#7-the-definitive-c-dos-and-donts-beginner-to-advanced)

---

## 1. Commercial Engineering Philosophy

To elevate TigerFish from a hobby experimental codebase into a **commercial-grade chess calculation engine**, all code must strictly satisfy four core engineering tenets:

### A. Zero Allocations in the Hot Path
- Move generation, static evaluation, quiescence, alpha-beta search, and transposition table operations must perform **zero dynamic heap allocations** (`new`, `malloc`, `std::vector::resize`, `std::string` formatting).
- Memory required for search recursion must be allocated on the call stack using lightweight POD structs or pre-allocated during engine startup.

### B. Mechanical Sympathy (Hardware-Conscious Design)
- Write code that aligns with modern CPU microarchitectures:
  - Keep core structs within L1/L2 cache lines (64-byte aligned).
  - Avoid branch mispredictions by ordering conditions by probability.
  - Utilize hardware-native bit manipulation instructions (`_pext_u64`, `_popcnt64`, `_tzcnt_u64`).
  - Eliminate pointer indirection in critical loops.

### C. Total Determinism & State Reversibility
- `make_move()` and `undo_move()` must guarantee exact bitwise reversibility for all board state, including Zobrist hashes, material scores, evaluation terms, castling rights, and en-passant squares.
- Fixed-depth single-threaded searches on identical positions must return bit-for-bit identical scores, node counts, and principal variations.

### D. Protocol Separation
- Calculation logic (board, movegen, eval, search) must remain **100% decoupled** from transport and protocol layers (UCI, JSON CLI, HTTP, Lichess API). The calculation engine must never know about sockets, JSON, or Python.

---

## 2. Codebase Architecture & Header/Forward-Declaration-Free Design

Commercial chess engines often prefer a **Single-Translation-Unit (Unity Build) / Header-Free Layered Architecture**. This eliminates compilation overhead, allows the compiler to inline aggressively across module boundaries, eliminates link-time optimization (LTO) bottlenecks, and avoids header/source synchronization bugs.

### A. Linear Downward Dependency Cascade
Dependencies flow in a single, strictly enforced downward cascade. Circular includes and forward declarations are forbidden:

```text
main.cpp (Entry point)
  └── cli.cpp / uci.cpp (Transport / CLI protocol)
        └── search.cpp (PVS, iterative deepening, Lazy SMP)
              ├── eval.cpp (Tapered evaluation, pawn hash, king safety)
              ├── tt.cpp (Transposition table clusters)
              └── movegen.cpp (Strictly legal move generator)
                    └── position.cpp (Position POD, linked-list StateInfo)
                          ├── bitboards.cpp (PEXT / Magic slider attacks)
                          ├── zobrist.cpp (Deterministic 64-bit hashing)
                          └── types.cpp (Primitives, bitmasks, strongly typed enums)
```

### B. Rules for Header & Forward-Declaration-Free Structure
1. **Module Hierarchy Is an Absolute Contract**:
   - `types.cpp` may depend only on C++ standard libraries (`<cstdint>`, `<bit>`).
   - `bitboards.cpp` and `zobrist.cpp` may depend on `types.cpp`.
   - `position.cpp` may depend on `bitboards.cpp` and `zobrist.cpp`.
   - Lower levels must **never** include, reference, or know about higher levels.
2. **Include Order via `#pragma once`**:
   - Every file begins with `#pragma once`.
   - Modules are included in exact dependency order. Because each file includes its dependencies, top-level consumers (`main.cpp`) only need to include the immediate top layer or compile the unity bundle.
3. **Encapsulation in Engine Namespace**:
   - All engine primitives, tables, and algorithms must reside in `namespace tigerfish`.
   - Never place `using namespace std;` in any shared or included file. Standard library types must be explicitly scoped (`std::uint64_t`, `std::atomic`, `std::clamp`).

---

## 3. Variable Generalization & Data-Oriented Design (DOD)

### A. Strong Typing Over Primitive Obsession
Avoid using raw integer types for chess domain entities. Strong typing prevents catastrophic square/piece/move transposition bugs at compile time with zero runtime cost:

```cpp
// ❌ BAD: Primitive obsession, easy to swap arguments
uint16_t pack_move(int from, int to, int flags);
int evaluate_piece(int piece, int square, int color);

// ✅ GOOD: Strongly typed zero-cost enums & POD wrappers
enum Square : uint8_t { SQ_A1 = 0, SQ_B1 = 1, ..., SQ_H8 = 63, SQ_NONE = 64 };
enum Color : uint8_t { WHITE = 0, BLACK = 1, COLOR_NONE = 2 };
enum PieceType : uint8_t { PT_NONE = 0, PT_PAWN = 1, PT_KNIGHT = 2, ..., PT_KING = 6 };
enum Piece : uint8_t { NO_PIECE = 0, W_PAWN = 1, ..., B_KING = 14 };

struct Move {
    uint16_t data = 0;
    constexpr Move() noexcept = default;
    constexpr explicit Move(uint16_t d) noexcept : data(d) {}
    constexpr Move(Square from, Square to, uint16_t flags = 0) noexcept 
        : data(static_cast<uint16_t>(from | (to << 6) | flags)) {}
    
    constexpr Square from() const noexcept { return static_cast<Square>(data & 0x3F); }
    constexpr Square to() const noexcept { return static_cast<Square>((data >> 6) & 0x3F); }
    constexpr uint16_t flags() const noexcept { return data & 0xF000; }
    constexpr bool is_none() const noexcept { return data == 0; }
};
static_assert(sizeof(Move) == 2, "Move must be exactly 16 bits");
```

### B. Perspective Generalization (Color Normalization)
Never duplicate evaluation or search logic for White and Black:
- Evaluate the board from the perspective of the **Side to Move**:
  ```cpp
  int score = evaluate(pos);
  return (pos.side_to_move == WHITE) ? score : -score;
  ```
- Use canonical mirror functions for square-table lookups:
  ```cpp
  constexpr Square mirror_square(Square sq) noexcept {
      return static_cast<Square>((7 - (sq >> 3)) * 8 + (sq & 7));
  }
  ```

### C. Stack-Allocated Reversible State (Stockfish Pattern)
Never copy large board states during search recursion. Use the linked-list state stack pattern:

```cpp
// ❌ BAD: 328 KB monolithic Board copied on the stack
class Board {
    UndoState history[8192]; // Massive memory bloat
};

// ✅ GOOD: Lightweight Position + stack-allocated StateInfo link
struct StateInfo {
    Piece captured_piece = NO_PIECE;
    uint8_t castling_rights = 0;
    Square en_passant_square = SQ_NONE;
    uint8_t halfmove_clock = 0;
    uint64_t zobrist_key = 0;
    int eval_mg = 0;
    int eval_eg = 0;
    Bitboard checkers = 0;
    Bitboard pinned = 0;
    StateInfo* previous = nullptr; // Unlinks cleanly on undo_move
};

struct Position {
    Bitboard pieces[15];
    Bitboard colors[2];
    Piece mailbox[64];
    Color side_to_move = WHITE;
    int game_ply = 0;
    StateInfo* state = nullptr;
};

// In search:
StateInfo new_state;
make_move(pos, move, new_state); // Links pos.state = &new_state
// ... recurse ...
undo_move(pos, move);             // Restores pos.state = pos.state->previous
```

---

## 4. Commercial-Grade Multithreaded C++ Concurrency

Multi-threading bugs in chess engines are notoriously subtle, producing intermittent hash corruption, phantom cutoffs, and nondeterministic crashes.

### A. Persistent Thread Pool (Zero Per-Search Allocation)
- **Never** call `std::thread workers.emplace_back(...)` inside search routines.
- Worker threads must be created **once** when the engine starts or when `setoption name Threads` is configured.
- Workers sleep on `std::condition_variable` and awaken instantly when work arrives:

```cpp
class WorkerPool {
    struct Worker {
        std::thread handle;
        std::mutex mtx;
        std::condition_variable cv_start;
        std::condition_variable cv_done;
        bool has_work = false;
        bool terminate = false;
        // per-thread search context...
    };
    std::vector<std::unique_ptr<Worker>> workers;
};
```

### B. Cacheline Alignment & False Sharing
When multiple threads write to adjacent memory locations, the CPU cache coherence protocol forces cacheline invalidations across cores (**False Sharing**), degrading performance by up to 90%:
- Align thread-local data structures to **64 bytes** (standard CPU cacheline size):
  ```cpp
  struct alignas(64) ThreadSearchContext {
      uint64_t nodes = 0;
      int seldepth = 0;
      Move killer_moves[128][2]{};
      int history_table[2][64][64]{};
  };
  ```

### C. Thread-Safe Transposition Table (Lockless Cluster Hashing)
To allow hundreds of millions of probes and stores per second across 24+ threads without mutex bottlenecks:
1. **Never use `std::mutex` per entry**. Mutex overhead destroys NPS.
2. **Never leave unaligned non-atomic writes** (`#pragma pack(push, 1)` with plain integers causes torn reads).
3. **Implement Lockless XOR Key Verification**:
   ```cpp
   // High-performance 16-byte lockless cluster entry
   struct TTEntry {
       uint64_t key_xor_data = 0; // key ^ packed_data
       uint16_t move = 0;
       int16_t score = 0;
       int16_t eval = 0;
       int8_t depth = 0;
       uint8_t bound_age = 0;
   };

   struct alignas(32) TTCluster {
       TTEntry entries[3];
       uint16_t padding = 0; // Exactly 32 bytes (half-cacheline)
   };
   ```
4. When writing an entry, use release semantics; when probing, verify the key matches after reading data.

### D. Cache-Line Padded Spinlocks (Contention Elimination)
When synchronized transposition tables or global state are accessed by 24+ threads, mutex contention collapses throughput. As demonstrated in `scratch/test_tt_locks.cpp`, use cache-line aligned spinlocks with CPU pause instructions:

```cpp
struct alignas(64) PaddedLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    inline void lock() noexcept {
        while (flag.test_and_set(std::memory_order_acquire)) {
            _mm_pause(); // Emits x86 PAUSE: prevents CPU pipeline starvation & bus flooding
        }
    }
    inline void unlock() noexcept {
        flag.clear(std::memory_order_release);
    }
};
```

### E. Root Move Parallelism (YBWC & Sibling Scouting)
As prototyped in `scratch/test_root_parallel.cpp`, combine Lazy SMP with the **Young Brothers Wait Concept (YBWC)** at the root node for depth $\ge 4$:
1. **Search the PV Move Sequentially**: The first (presumed best) move is searched with full window $[-\infty, \infty]$ to establish the true $\alpha$.
2. **Distribute Sibling Moves Atomically**: Remaining moves are dispatched across worker threads via `std::atomic<int> next_idx`.
3. **Zero-Window Sibling Scouting**: Worker threads search sibling moves under a null window:
   ```cpp
   int cur_alpha = alpha;
   int score = -alpha_beta(wpos, -cur_alpha - 1, -cur_alpha, depth - 1, 1, wctx, tt);
   if (score > cur_alpha) {
       // Fail-high: re-search with full aspiration window under root mutex
       score = -alpha_beta(wpos, -SCORE_INFINITY, -cur_alpha, depth - 1, 1, wctx, tt);
   }
   ```

### F. Asymmetric Lazy SMP Diversification
If all threads search the identical tree, multi-core scaling is near zero:
- **Depth Jitter**: Helper thread `t` starts at `depth = 1 + (t % 2)`.
- **Aspiration Bounds**: Give helper threads slightly wider aspiration windows (`delta = 35` for helpers vs `delta = 25` for main).
- **Pruning Asymmetry**: Vary LMR reductions slightly on helper threads to encourage exploration of alternative candidate moves.

### G. Win32 Native Hardware Profiling & Duty-Cycle Tracking
To verify that multi-threading actually delivers linear scaling rather than kernel lock thrashing, implement process-level CPU profiling as modeled in `scratch/test_proc_cpu.cpp` and `scratch/test_thread_balance.cpp`:
- **Kernel vs User CPU Tracking**: Use Windows native `GetProcessTimes` to compute exact wall-clock vs CPU-time ratio:
  $$\text{Equivalent Cores} = \frac{\text{CPU Time}}{\text{Wall-Clock Time}}$$
- **Thread Duty-Cycle Balance**: Measure active execution milliseconds vs idle wait time per thread. A healthy engine maintains $\ge 92\%$ active utilization across all worker threads without tail-latency stalls.

### H. Hardware Core Affinity (P-Core & E-Core Awareness)
On modern Intel (Alder Lake/Raptor Lake) hybrid processors, worker threads must not be pinned naively using `1ULL << t`:
- Query processor topology using `GetLogicalProcessorInformationEx` (Windows) or `pthread_setaffinity_np` (Linux).
- Prioritize high-performance P-cores before scheduling onto efficiency E-cores.
- Call `SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)` during high-depth benchmark sweeps to eliminate OS preemption jitter.

---

## 5. Testing, Benchmarking & Engine Variant Evaluation Framework

Commercial development requires continuous, empirical validation. Never assume an optimization improves strength without measurable evidence.

### A. Four-Tier Testing Hierarchy
1. **Tier 1: Correctness & Stockfish Perft Verification (Zero Tolerance)**
   - Run Perft to depth 6 on `startpos` (119,060,324 nodes) and depth 5 on `kiwipete` (193,690,690 nodes).
   - Use the **Stockfish Differential Comparator** (`tools/perft/perft_compare_sf.cpp`) to verify move-by-move node counts against Stockfish with automated ANSI delta diffing.
   - Run the **15-Position Depth-8 Stress Suite** (`Test/perft/run_15_perft_tests.py`) covering discovery pins, mirrored promotions, and en passant traps.
2. **Tier 2: Tactical Problem Suite (200+ Categorized Puzzles)**
   - Run `Test/regression/positions/tactical_suite.json` (1,402 lines) covering Mates in 1/2/3, Knight Forks, Absolute Pins, Deflections, and Morphy Opera House Queen Sacrifices (`Test/regression/cases/test_queen_sac.cpp`).
3. **Tier 3: FIDE Rules & Terminal State Regressions**
   - Run `Test/regression/cases/test_rules_advanced.cpp` verifying insufficient material ($K \text{ vs } K$, $K+B \text{ vs } K$, $K+N \text{ vs } K$), 50/75-move clocks, threefold repetition, and underpromotions.
4. **Tier 4: Strength & Elo Benchmarking (SPRT & Tournament Matches)**
   - Self-play matches using the 88-position tournament matchmakers and Python UCI auto-battlers.

### B. Comparing Engine Variants: Self vs Self, Old vs New
To scientifically measure whether a change (e.g., new pruning heuristic, modified PST, rewritten movegen) is superior:

#### 1. 88-Position Parallel Tournament Matchmaker (`Test/match/NewMatchmaker.cpp`)
Run 176 games (88 deduplicated positions with mandatory color swapping) across 24 concurrent threads:
- **Metrics Collected**: Win/draw/loss record, total score, average nodes/move, average time/move, NPS, and termination reasons.
- **Clocked Variant**: Run `Test/match/TimeMatchmaker.cpp` to validate time-management governors under fixed time budgets.

#### 2. Head-to-Head Self-Play Matches & Logistic ELO Calculation
Use the automated UCI matchmaker (`Test/match/match.py`) to pit candidate binaries against baseline releases:
```bash
# 100 games at fast time control (10s + 0.1s increment)
python Test/match/match.py --engine1 ./game_new.exe --engine2 ./game_old.exe --games 100 --tc 10+0.1
```
The runner computes exact mathematical ELO difference via the logistic curve:
$$\Delta\text{ELO} = -400 \times \log_{10}\left(\frac{1}{\text{ratio}} - 1\right) \quad \text{where } \text{ratio} = \frac{\text{Wins} + 0.5 \times \text{Draws}}{\text{Total Games}}$$

#### 3. SPRT (Sequential Probability Ratio Test) Standards
For release qualification:
- **Null Hypothesis ($H_0$)**: Engine A is not stronger than Engine B ($[0.0, 5.0]$ Elo).
- **Alternative Hypothesis ($H_1$)**: Engine A is stronger ($[5.0, 10.0]$ Elo).
- Stop test early when likelihood ratio crosses acceptance ($\alpha = 0.05$) or rejection ($\beta = 0.05$) thresholds.

---


## 6. Git, Commit, Push & Release Workflow

### A. Strict Git Hygiene
- **Never Commit Binaries**: `*.exe`, `*.o`, `*.obj`, `*.dll`, and build directories (`build/`) must never be staged or committed. Verify via `git status` before every commit.
- **Never Commit Secrets**: Ensure `.env` and private bot tokens are excluded.

### B. Commit Message Standard (Conventional Commits)
All commit messages must be written in the imperative mood and categorized:
- `feat(search)`: Add Reverse Futility Pruning at depth <= 3
- `fix(movegen)`: Correct en-passant horizontal pin discovery
- `perf(bitboards)`: Migrate slider attacks to dense flat LUT (<850 KB)
- `refactor(position)`: Transition Board to DOD Position struct
- `test(perft)`: Add Kiwipete depth 6 automated test case
- `docs(api)`: Update Lichess bot subprocess protocol specification

### C. Pre-Commit Gate (Mandatory Checklist)
Before any commit or push, the engineer must execute:
1. `g++ -O3 -std=c++20 -Wall -Wextra` — Must compile with **zero warnings**.
2. Run Perft suite — Must match exact node counts.
3. Run `python Test/regression/run_all_tests.py` — Must pass **100% of test cases**.
4. Run `python test_bot_startup.py` — Must pass all clock simulations without flagging.

### D. Multi-Branch Routing Policy (Per `SOP.md` & `AGENTS.md`)
- **`dev` Branch**: Active development for C++ engine features, search improvements, and evaluations.
- **`lichess` Branch**: Production Lichess bot bridge (`lichess_bot.py`, `lichess_multibot.py`).
- **`main` Branch**: Production release branch.
- When an engine version is validated on `dev`, the core `engine/` source must be synchronized across `dev`, `main`, and `lichess` simultaneously.

---

## 7. The Definitive C++ Do's and Don'ts: Beginner to Advanced

### Level 1: Basics & Foundations
- **DO** use `constexpr` for all compile-time constants, bitmasks, and lookup tables.
- **DO** initialize every struct member and variable explicitly (`int score = 0;`).
- **DO** use `std::size_t` or fixed-width integers (`std::int32_t`, `std::uint64_t`) instead of ambiguous `long` or `short`.
- **DON'T** use raw C-style arrays (`int arr[64]`) when passing data across function boundaries. Use `std::array<T, N>` or pass by reference.
- **DON'T** use `using namespace std;` in header or unity files.
- **DON'T** use `#define` for constants. Use `static constexpr auto`.

### Level 2: Modern C++20 & Type Safety
- **DO** use `enum class` or strongly typed enums with fixed underlying types (`enum Color : uint8_t`).
- **DO** mark functions that never throw exceptions as `noexcept` to assist compiler inlining and register allocation.
- **DO** use `<bit>` standard library functions (`std::popcount`, `std::countr_zero`) with fallback to BMI2 intrinsics.
- **DON'T** use C-style casts `(int)x`. Use `static_cast<int>(x)`.
- **DON'T** use runtime polymorphism (`virtual` functions, vtables, inheritance) in the core calculation engine. Use templates or DOD composition.

### Level 3: Memory & Cache Locality
- **DO** arrange struct members from largest to smallest to avoid implicit padding waste:
  ```cpp
  // ✅ 16 bytes, zero wasted padding
  struct WellPacked {
      uint64_t key;   // 8 bytes
      uint16_t move;  // 2 bytes
      int16_t score;  // 2 bytes
      int8_t depth;   // 1 byte
      uint8_t flags;  // 1 byte
      uint16_t pad;   // 2 bytes
  };
  ```
- **DO** align critical multi-threaded structures to 64 bytes (`alignas(64)`).
- **DON'T** allocate objects on the heap inside search or movegen loops.
- **DON'T** pass structs larger than 16 bytes by value. Pass by `const T&`.

### Level 4: Concurrency & Multithreading
- **DO** use `std::memory_order_relaxed` for atomic counters (e.g. node counts) where synchronization is not required.
- **DO** use `std::memory_order_acquire` on loads and `std::memory_order_release` on stores when publishing data across threads.
- **DO** keep worker threads alive in a persistent pool rather than creating/destroying threads per search.
- **DON'T** call `std::memset` on non-trivial types or structs containing `std::atomic` fields (Undefined Behavior).
- **DON'T** instantiate `std::lock_guard` inside a `for` loop body when attempting to lock an entire array.
- **DON'T** access unaligned non-atomic integers across multiple threads simultaneously (Causes CPU faults and data races).

### Level 5: High-Performance Architecture
- **DO** prefer Precomputed Flat Tables over nested arrays for multi-dimensional lookups.
- **DO** maintain incremental evaluations during `make_move` rather than recomputing material and PST from scratch.
- **DO** use branchless bitboard calculations for attack sets:
  ```cpp
  // Branchless pawn attacks
  return (color == WHITE) ? ((b << 7) & ~FILE_H) | ((b << 9) & ~FILE_A)
                          : ((b >> 7) & ~FILE_A) | ((b >> 9) & ~FILE_H);
  ```
- **DON'T** leave dead code, obsolete backup files, or stubbed comments (`// Re-sync position`) in production source files.
- **DON'T** report an engine as stronger without documented SPRT or head-to-head match verification evidence.
