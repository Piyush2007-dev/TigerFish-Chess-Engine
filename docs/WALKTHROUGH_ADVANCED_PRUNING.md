# Advanced Non-NNUE Pruning & Search Modernization

This document details the implementation of 5 advanced, non-NNUE search algorithms in the TigerFish 2.0 engine core (`engine/` and `newengine/`).

---

## 1. Implemented Algorithms

### A. Singular Extensions (SE)
- **Concept**: At non-root nodes with `depth >= 8`, when the Transposition Table (TT) entry is a lower bound or exact score and reliable, we test whether the TT move is uniquely dominant by running a reduced-depth null-window search (`(depth - 1) / 2`) with an artificially lowered threshold $\beta = \text{tt\_score} - 2 \times \text{depth}$ with `tt_move` excluded.
- **Effect**: If no other move can reach this threshold, the TT move is singular. Depth is extended by $+1$ ply (or $+2$ for double extensions). In forced tactical sequences, this collapses the game tree into a 1-move-wide corridor.
- **Multi-Cut**: If the singular search score $\ge \beta$, the node fails high immediately without searching.

### B. Continuation History (Conthist)
- **Concept**: Keyed as a 3D table `cont_history[15][64][64]` (`[prev_moving_piece][prev_to_sq][curr_to_sq]`) stored in `SearchContext`.
- **State Tracking**: `StateInfo` was augmented with `Move move` and `Piece moving_piece` in `engine/position.cpp`, populated in `make_move()` with zero heap allocation and automatic reversibility on unmake.
- **Scoring**: In `score_move()`, when a previous move exists, the continuation history score is added to the butterfly history score, boosting countermove ordering.

### C. History Gravity with Malus
- **Gravity Damping**: Replaced unbounded positive-only history increments with bounded gravity damping:
  $$\text{history} \leftarrow \text{history} + \text{bonus} - \frac{\text{history} \times |\text{bonus}|}{16384}$$
- **History Malus**: When a quiet move causes a $\beta$-cutoff:
  - The cutoff move receives `+bonus`.
  - **All preceding quiet moves that failed low in that same node receive a `-bonus` malus.**
  - Over thousands of nodes, bad moves sink into Late Move Pruning (LMP) and Late Move Reduction (LMR) cutoff zones.

### D. Exponential Aspiration Windows
- **Concept**: Replaced the previous hard reset to $(-\infty, +\infty)$ with exponential widening:
  - Initial window: $[\text{best\_score} - 25, \text{best\_score} + 25]$.
  - On fail-low: $\beta \leftarrow (\alpha + \beta) / 2$, $\alpha \leftarrow \alpha - \delta$, $\delta \leftarrow \delta + \delta / 2$.
  - On fail-high: $\beta \leftarrow \beta + \delta$, $\delta \leftarrow \delta + \delta / 2$.
  - Opens to $(-\infty, +\infty)$ only if $\delta > 500\text{cp}$.
- Applied across `search_position()` (Lazy SMP main thread) and `WorkerPool` (helper threads).

### E. Improving-Flag LMR Scaling
- In `alpha_beta`, Late Move Reductions dynamically scale based on whether the position is improving (`static_eval >= eval_history[ply - 2]`):
  - **Improving (`true`)**: `reduction++` (prunes more aggressively when position is winning).
  - **Not Improving (`false`)**: `reduction--` (searches defensive moves more thoroughly).

---

## 2. Empirical Benchmarks & Verification

### A. Core Move Generation & Multi-Threaded Perft Suite
`.\game.exe perft bench` (All 8 positions tested across 1, 16, and 24 threads):

| Position | Depth | Expected Nodes | 1 Thread | 16 Threads | 24 Threads | Status |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| **Startpos** | 5 | 4,865,609 | 8.98 ms | 2.00 ms | 2.04 ms | **PASS** |
| **Startpos** | 6 | 119,060,324 | 233.23 ms | 32.03 ms | 25.48 ms (4.67B NPS) | **PASS** |
| **Kiwipete** | 4 | 4,085,603 | 8.28 ms | 2.14 ms | 2.19 ms | **PASS** |
| **Kiwipete** | 5 | 193,690,690 | 271.55 ms | 33.85 ms | 25.99 ms (**7.45B NPS**) | **PASS** |
| **Pos 3** | 4 | 43,238 | 0.59 ms | 1.55 ms | 1.46 ms | **PASS** |
| **Pos 4** | 4 | 422,333 | 1.09 ms | 0.95 ms | 1.13 ms | **PASS** |
| **Pos 5** | 4 | 2,103,487 | 4.52 ms | 1.46 ms | 1.68 ms | **PASS** |
| **Pos 6** | 4 | 3,894,594 | 4.92 ms | 1.64 ms | 1.80 ms | **PASS** |

### B. Full 87-Case Regression Suite
`python Test/regression/run_all_tests.py`:
- 87 / 87 tests passed cleanly (Saved `baseline_v34.json`).

### C. Search Benchmark Comparison (Depth 12, 4 Threads)
`.\game.exe bench 12 4`:

| Metric | Before Modernization | After Modernization | Improvement |
|---|:---:|:---:|:---:|
| **Total Nodes** | 14,980,120 | **3,922,803** | **$-73.8\%$ nodes eliminated** |
| **Elapsed Time** | 1.12 s | **0.366 s** | **$3.05\times$ faster search** |

### D. Live 24-Threaded TigerFish vs Stockfish 18 Match
`python Test/match/tigerfish_vs_stockfish.py 1 24 24` (1.0s / move):
- **Game Length**: Extended from 42 half-moves to **78 half-moves (39 full moves)**.
- **TigerFish Total Nodes**: **1,179,471,099 nodes** in 33.0s (**35.7 Million NPS**).
- **Stockfish Total Nodes**: **375,809,383 nodes** in 35.3s (**10.6 Million NPS**).
