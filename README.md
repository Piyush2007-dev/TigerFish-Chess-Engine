# 🐅 TigerFish Chess Engine

[![Play on Lichess](https://img.shields.io/badge/Play_on-Lichess-swords?style=for-the-badge&logo=lichess&color=2e7d32)](https://lichess.org/@/TigerFish-BOT)  
🎮 **Play Live on Lichess**: [https://lichess.org/@/TigerFish-BOT](https://lichess.org/@/TigerFish-BOT)

A high-performance chess engine written in **C++20** featuring tapered evaluation, enhanced search with modern pruning, and a 32 MB transposition table.

---

## ⚡ Performance Benchmarks

| Metric | Enhanced Engine | Old Engine | Improvement |
|--------|----------------|------------|-------------|
| **Depth 6 avg nodes/move** | 2,559,351 | 92,126,415 | **36× fewer** |
| **Depth 6 avg time/move** | 2,867ms | 49,058ms | **17× faster** |
| **Match score (88 positions)** | **88.5** | 87.5 | Enhanced wins |
| **Move Generator Throughput** | ~20–35 Mnps | — | — |

---

## ✨ Key Technical Highlights

* **Tapered Evaluation**: MG/EG phase-weighted PST lookups — king stays safe in middlegame, centralizes in endgame.
* **Enhanced Search**: Null-move pruning, LMR, killer moves, history heuristic, quiescence search.
* **32 MB Transposition Table**: $O(1)$ lookup with 2M entries, exact/bound flags, 32-bit evaluation scores.
* **Pawn Structure**: Doubled pawn penalties, isolated pawn detection, passed pawn bonuses scaled by rank.
* **Piece Mobility**: Safe-square counting weighted by phase — strong positional signal.
* **King Safety**: Pawn shield presence, attacker count × weight, quadratic falloff for multiple attackers.
* **MVV-LVA + History Move Ordering**: Captures sorted by tactical value, quiet moves boosted by history.

---

## 🚀 Build & Run

### Compile
```bash
g++ -O3 -std=c++20 -Wl,--stack,67108864 -o game.exe engine/main.cpp
```

### Commands

**UCI Protocol** (default):
```bash
./game.exe
```

**Analyze a position** (returns JSON):
```bash
./game.exe analyze "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 8
```

**Persistent bot session**:
```bash
./game.exe interactive
# stdin: newgame, apply <move>, best <depth>, quit
```

**Best move**:
```bash
./game.exe best "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 7
```

**Legal moves**:
```bash
./game.exe moves "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
```

**Make a move**:
```bash
./game.exe make "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" e2e4
```

---

## 🏗️ Architecture

```
Layer 5: CLI & Interactive Protocol
         engine/main.cpp
               |
Layer 4: Enhanced Search — null-move, LMR, killers, history, quiescence
         engine/search.cpp
               |
Layer 3: Legal Move Generator — pins, check detection, MVV-LVA
         engine/rules.cpp
               |
Layer 2: Board State — bitboards, Zobrist hash, make/unmake
         engine/board.cpp
               |
Layer 1: Magic Bitboards + Evaluation Tables
         engine/magic_lut.cpp   engine/eval.cpp
```

### Evaluation Pipeline
```
evaluate(board)
  ├── eval_tapered()        Material + MG/EG PST lookup
  ├── eval_bishop_pair()    +35 cp bonus for both bishops
  ├── eval_pawn_structure() Doubled, isolated, passed pawns
  ├── eval_mobility()       Safe-square counting per piece
  └── eval_king_safety()    Pawn shield + attacker zones (MG only)
  
  phase = weighted piece count (0=opening, 24=endgame)
  final = (mg × phase + eg × (24 − phase)) / 24
```

---

## 📁 Repository Structure

```
.
├── engine/                 # C++20 Chess Engine Source
│   ├── magic_lut.cpp       # Layer 1: Magic bitboard tables
│   ├── board.cpp           # Layer 2-3: Board state, Zobrist hash
│   ├── rules.cpp           # Layer 3: Legal move generation
│   ├── eval.cpp            # Layer 1+3: Tapered eval, PSTs, old_evaluate()
│   ├── search.cpp          # Layer 4: Enhanced search engine
│   ├── main.cpp            # Layer 5: CLI entry point
│   ├── benchmark.cpp       # Standalone benchmark (included by Test/bench/)
│   ├── oldsearch.cpp       # Legacy search (for A/B comparison)
│   └── uci_main.cpp        # Minimal UCI-only entry
│
├── Test/                   # Testing & Benchmarking
│   ├── bench/              # Side-by-side benchmark runners
│   ├── match/              # Head-to-head match engine
│   ├── scripts/            # Python orchestration
│   ├── regression/         # 87 automated tests
│   └── output/             # Benchmark results
│
├── archive/                # Old code, experiments, docs
├── game.exe                # Compiled engine binary (~200 KB)
├── OPERATIONS_MANUAL.md    # Detailed developer manual
└── README.md               # This file
```

---

## 🧪 Testing

```bash
# Run all 87 tests
python Test/regression/run_all_tests.py

# Quick benchmark comparison
cd Test/bench && ./bench_new.exe && ./bench_old.exe

# Play a match at depth 6
cd Test/match && ./NewMatchmaker.exe 6 4

# Full benchmark with 200 tactical puzzles
python Test/scripts/run_multithreaded_benchmark.py
```

---

## 📖 Documentation

* **[OPERATIONS_MANUAL.md](OPERATIONS_MANUAL.md)** — Full developer manual: data encodings, file-by-file breakdown, include dependencies, search internals, evaluation pipeline.
* **[Test/README.md](Test/README.md)** — Complete test suite documentation.
* **[AGENTS.md](AGENTS.md)** — AI agent conventions for this project.
* **[SOP.md](SOP.md)** — Standard Operating Procedures.
