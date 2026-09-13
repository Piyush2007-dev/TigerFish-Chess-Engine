# TigerFish Test Suite

All testing, benchmarking, and matching lives here. Everything compiles from
the main `engine/` folder at the project root — no local copies.

```
Test/
├── bench/           Benchmark runners
├── match/           Head-to-head engine matches
├── scripts/         Python orchestration
├── regression/      Automated test suite (87 tests)
└── output/          Benchmark results
```

All files reference the main `engine/` folder at the project root — no local
copies. Include paths use `../../engine/` (from bench/match) or
`../../engine/` (from regression/cases).

## bench/ — Benchmark Runners

### Side-by-side comparison (10 positions)

| File | Compiles to | What it does | How to run |
|------|------------|-------------|-----------|
| `bench_new.cpp` | `bench_new.exe` | Runs **enhanced engine** on 10 positions, prints nodes/time/NPS | `./bench_new.exe` |
| `bench_old.cpp` | `bench_old.exe` | Runs **old engine** on same 10 positions for comparison | `./bench_old.exe` |
| `bench_cases.cpp` | *(not compiled)* | Shared 10-position test set (startpos, Sicilian, Italian, QGD, endgames, mates) | Included by bench_new and bench_old |

**Quick comparison:**
```bash
cd Test/bench
g++ -O3 -std=c++20 -o bench_new.exe bench_new.cpp
g++ -O3 -std=c++20 -o bench_old.exe bench_old.cpp
./bench_new.exe    # Enhanced: ~43K nodes, ~15ms
./bench_old.exe    # Old: ~1.9M nodes, ~300ms
```

### CLI runners (for Python scripts)

| File | Compiles to | What it does | How to run |
|------|------------|-------------|-----------|
| `bench_runner_enhanced.cpp` | `bench_runner_enhanced.exe` | Enhanced engine CLI: takes `<FEN> <depth>`, outputs JSON | `./bench_runner_enhanced.exe "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 5` |
| `bench_runner_old.cpp` | `bench_runner_old.exe` | Old engine CLI: same interface | `./bench_runner_old.exe "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 5` |

**JSON output format:**
```json
{"move":"d2d4","score":69,"depth":5,"nodes":14802,"time_ms":4.9,"nps":3015892,"tt_hits":273,"tt_misses":6850}
```

### Standalone benchmark

| File | Compiles to | What it does | How to run |
|------|------------|-------------|-----------|
| `newbenchmark.cpp` | `newbenchmark.exe` | Self-contained: includes local engine copies, runs perft + self-play at increasing depths | `g++ -O3 -std=c++20 -o newbenchmark.exe newbenchmark.cpp && ./newbenchmark.exe` |

---

## match/ — Head-to-Head Matches

| File | Compiles to | What it does | How to run |
|------|------------|-------------|-----------|
| `NewMatchmaker.cpp` | `NewMatchmaker.exe` | Enhanced vs Old at fixed depth. 88 positions × 2 sides = 176 games, multi-threaded. Tracks per-engine nodes/time | `./NewMatchmaker.exe <depth> <threads>` |
| `match.py` | *(script)* | Generic UCI match runner: plays games between any two engine executables via UCI protocol | `python match.py --engine1 game.exe --engine2 old.exe --games 100 --depth 8` |
| `match_runner.cpp` | `match_runner.exe` | Windows parallel match runner (200 games, 24 threads). UCI-based | `g++ -O3 -std=c++20 -Wl,--stack,67108864 -o match_runner.exe match_runner.cpp` |

**Quick match:**
```bash
cd Test/match
g++ -O3 -std=c++20 -Wl,--stack,67108864 -o NewMatchmaker.exe NewMatchmaker.cpp
./NewMatchmaker.exe 6 4    # depth 6, 4 threads
```

**Output:**
```
Enhanced wins : 2
Draws         : 173
Old wins      : 1
Score         : Enhanced 88.5 - 87.5 Old

Avg nodes/move: Enhanced=2,559,351  Old=92,126,415
Avg time/move : Enhanced=2,867ms    Old=49,058ms
```

---

## scripts/ — Python Orchestration

| File | What it does | How to run |
|------|-------------|-----------|
| `run_multithreaded_benchmark.py` | Full benchmark: runs both CLI runners on standard positions + 200 tactical puzzles, multi-threaded. Reports nodes, time, NPS, tactical solve rate | `python scripts/run_multithreaded_benchmark.py` |
| `tt_benchmark.py` | Transposition table comparison: runs both engines on 10 positions at depths 3–8, compares TT hit/miss rates | `python scripts/tt_benchmark.py` |

Both scripts auto-compile the CLI runners if needed. Results saved to `output/`.

---

## regression/ — Automated Test Suite

### Master runner

| File | What it does | How to run |
|------|-------------|-----------|
| `run_all_tests.py` | Compiles all 8 test binaries, runs 87 tests, saves auto-incrementing baseline JSON | `python Test/regression/run_all_tests.py` |

### cases/ — Test Source Files

Each file includes engine source via `#include "../../../engine/..."` and is compiled by the runner.

| File | Compiles to | Tests | Count |
|------|------------|-------|-------|
| `test_perft.cpp` | `perft.exe` | Move generation node counts (6 positions) | 6 |
| `test_rules.cpp` | `rules.exe` | Legal moves: checkmate, stalemate, EP, castling, double check, pins | 8 |
| `test_evaluation.cpp` | `evaluation.exe` | Eval sign convention: queen/pawn up for both sides | 4 |
| `test_eval_advanced.cpp` | `eval_advanced.exe` | Tapered eval, bishop pair, pawn structure, mobility, king safety, symmetry | 17 |
| `test_rules_advanced.cpp` | `rules_advanced.exe` | Insufficient material, EP pin, castling, discovered check, promotions, stalemate | 21 |
| `test_search_advanced.cpp` | `search_advanced.exe` | Mate-in-1, endgame, search consistency, TT effectiveness, depth 8 under 10s, zugzwang | 19 |
| `test_search_tactics.cpp` | `tactics.exe` | 3 tactical puzzles: queen mate, back rank mate, hanging rook | 3 |

### positions/ — Test Position Data

| File | Contents |
|------|----------|
| `tactical_suite.json` | 200 tactical puzzles with FEN + expected best move |
| `positional_suite.json` | Positional evaluation test positions |
| `endgame_suite.json` | Endgame test positions |

### build/ — Compiled Binaries

Auto-generated by `run_all_tests.py`. Contains the 8 compiled test executables.

### output/ — Baseline Snapshots

Auto-incrementing JSON files (`baseline_v0.json` → `baseline_v8.json`). Each contains:
- Environment info (compiler, OS)
- Per-test pass/fail results
- Performance metrics (nodes, time, NPS)
- Tactical solve rate

Compare baselines to detect regressions after code changes.

---

## Quick Reference

| I want to... | Command |
|-------------|---------|
| Run all tests | `python Test/regression/run_all_tests.py` |
| Compare enhanced vs old (nodes/time) | `cd Test/bench && ./bench_new.exe && ./bench_old.exe` |
| Play a match at depth 6 | `cd Test/match && ./NewMatchmaker.exe 6 4` |
| Full benchmark with tactical puzzles | `python Test/scripts/run_multithreaded_benchmark.py` |
| Compare TT efficiency | `python Test/scripts/tt_benchmark.py` |
| Run self-play benchmark | `cd Test/bench && ./newbenchmark.exe` |
| Test a single position | `cd Test/bench && ./bench_runner_enhanced.exe "<FEN>" 8` |
