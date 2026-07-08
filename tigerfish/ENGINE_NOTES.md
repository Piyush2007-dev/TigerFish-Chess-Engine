# TigerFish Engine — Developer Notes
## What Was Added, How It Works, and Why

---

## Overview of Changes

Three major additions were made to the TigerFish codebase:

1. **`Engine` class** — static evaluation + minimax search added to `chess.cpp`
2. **`game.cpp`** — entry point separated from engine logic
3. **`bench.cpp`** — self-play speed benchmark

---

## 1. The `Engine` Class (`chess.cpp`)

### What it does
The `Engine` class is responsible for one thing: **given a board position, find the best move**.

It does this in two steps:
- **`evaluate()`** — scores a position statically (no lookahead)
- **`minimax()`** — searches future positions recursively using evaluate() as its ground truth

---

### Part A: `evaluate()` — Static Evaluation

```cpp
int evaluate(Board& board);
```

Scans all 64 squares. For each piece found, adds its value to the score.

**Formula:**
```
score = Σ(white piece values) - Σ(black piece values)
```

- Positive score → White is better
- Negative score → Black is better
- Zero → Equal position

#### Piece Base Values (centipawns)
```
Pawn   = 100    Knight = 320    Bishop = 330
Rook   = 500    Queen  = 900    King   = 20,000
```

**Why is King = 20,000?**  
It's larger than the sum of every other piece combined (~4,000). This makes the engine
always prefer avoiding checkmate over any material gain. The engine never needs a special
"checkmate check" — losing the king simply produces an astronomically bad score that no
combination of captures can compensate for.

#### Piece-Square Tables (PST)

A flat bonus/penalty added to each piece based on *where it stands* on the board.

```cpp
static constexpr int PAWN_PST[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,  // rank 1 (impossible for pawn)
     5, 10, 10,-20,-20, 10, 10,  5,  // rank 2 — penalise early wing pushes
     5, -5,-10,  0,  0,-10, -5,  5,  // rank 3
     0,  0,  0, 20, 20,  0,  0,  0,  // rank 4 — reward e4/d4
    ...
    50, 50, 50, 50, 50, 50, 50, 50,  // rank 7 — near promotion, huge bonus
};
```

**Why PSTs?** A lone pawn on e4 is worth more than a pawn cowering on a2. PSTs encode
positional intuition (central control, king safety, pawn advancement) into a simple lookup.

**Mirroring for Black:**  
White's PST is indexed from a1=0. For Black pieces, the square is mirrored vertically so
Black's "good" squares match White's structure:

```cpp
static int mirror(int sq) {
    return (7 - sq / 8) * 8 + (sq % 8);
}
```

Example: White pawn bonus on e4 (square 28) → Black pawn gets same bonus on e5 (square 36).

---

### Part B: `minimax()` — Tree Search with Alpha-Beta

```cpp
int minimax(Board& board, int depth, int alpha, int beta, bool maximizing);
```

This is the core search algorithm. It works by recursively asking:
> "If both players play perfectly from here, what score does this position reach?"

#### How it works

```
Position at depth N
├── Try move 1 → Position at depth N-1
│   ├── Try move 1.1 → ... (recurse)
│   └── Try move 1.2 → ... (recurse)
├── Try move 2 → Position at depth N-1
│   └── ...
└── ...
At depth 0: call evaluate() — no more lookahead
```

**Maximizing vs Minimizing:**
- White's turn (`maximizing = true`) → picks the move with the **highest** score
- Black's turn (`maximizing = false`) → picks the move with the **lowest** score

This models both players playing as well as they can.

#### Alpha-Beta Pruning

Without pruning, minimax at depth 5 checks ~35^5 = 52 million nodes. Alpha-Beta
cuts branches that **cannot possibly** change the result, reducing this to ~35^2.5 = ~6,000
nodes in the best case.

```cpp
if (beta <= alpha) break;  // ← the "pruning" — skip rest of this branch
```

- **Alpha** = best score White has found so far (his floor)
- **Beta**  = best score Black has found so far (his ceiling)
- If `beta ≤ alpha`, the opponent would never let us reach this branch — skip it

**Real-world result:** At depth 5, the engine searched ~6 million nodes per move
(vs ~52M for pure minimax). Alpha-beta gave roughly a 9× speedup.

#### Terminal cases

```cpp
if (depth == 0) return evaluate(board);           // depth limit → static eval

if (moves.size() == 0) {
    if (board.is_in_check())
        return maximizing ? (-20000 - depth) : (20000 + depth);  // checkmate
    return 0;  // stalemate
}
```

**Why `20000 - depth`?**  
Checkmate in 1 move is better than checkmate in 3. By subtracting depth, shallower
checkmates get higher scores, so the engine automatically prefers faster mates.

---

### Part C: `best_move()` — Root Search

```cpp
Move best_move(Board& board, int depth);
```

The entry point. Generates all legal moves, calls `minimax()` for each, and returns
the Move with the best score.

```cpp
for each legal move:
    make the move
    score = minimax(board, depth-1, ...)
    unmake the move
    if score is best so far → remember this move
return best move
```

---

### Critical Bug Found During Testing

**Symptom:** `minimax` was only searching 5 nodes per call instead of millions.

**Root cause:** `Board::make_move(Move move, MoveList& moves)` calls `moves.clear()`
at the end (to signal the move list is now stale). When `minimax` passed its own
iteration list to `make_move`, the list was wiped **after the first move**, breaking
the loop instantly.

**Fix:** Pass a throwaway `MoveList dummy` to absorb the clear:

```cpp
// WRONG — clears the list we're iterating!
board.make_move(moves.move_list[i], moves);

// CORRECT — dummy absorbs the clear
MoveList dummy;
board.make_move(moves.move_list[i], dummy);
```

This was the single most impactful fix — without it, the engine was doing a depth-5
search but only ever exploring 1 branch.

---

## 2. `game.cpp` — Clean Entry Point Separation

### Why split from chess.cpp?

Before, `main()` lived inside `chess.cpp`. This meant:
- `chess.cpp` could not be `#include`d by other files (duplicate `main`)
- Logic and CLI were tangled together

**After the split:**

| File | Role |
|---|---|
| `chess.cpp` | Pure library: `Board`, `MoveGenerator`, `Engine` |
| `game.cpp`  | `#include "chess.cpp"` + `main()` |
| `bench.cpp` | `#include "chess.cpp"` + benchmark `main()` |

### CLI Commands in game.cpp

```
chess.exe moves "<fen>"           → JSON: board state + all legal moves
chess.exe make   "<fen>" "<uci>" → JSON: new board state after applying move
chess.exe best   "<fen>" [depth] → JSON: { "best_move": "e2e4" }
```

The `server.js` spawns `chess.exe` as a subprocess and parses its JSON output.

### Compile command
```powershell
g++ -O2 -std=c++20 -o chess.exe game.cpp
```

---

## 3. `bench.cpp` — Self-Play Speed Benchmark

### What it does
Plays the engine against itself for N plies at a given depth, measuring:
- Nodes searched per move
- Time per move (milliseconds)
- NPS (nodes per second)
- Final board evaluation

### Why self-play?
Self-play is the standard way to benchmark a chess engine because:
- It exercises the engine on real game positions (not just the opening)
- Both sides get harder as the game progresses (more complex positions)
- Node counts grow as the position opens up — shows how search scales

### How node counting works

`BenchEngine` is a standalone struct (not inheriting from `Engine`) with a `nodes`
counter incremented at the top of every `minimax()` call:

```cpp
int minimax(Board& board, int depth, int alpha, int beta, bool maximizing) {
    nodes++;   // ← counted every time we visit a node
    if (depth == 0) return evaluate(board);
    ...
}
```

Between moves, `reset_nodes()` zeroes the counter so per-move stats are accurate.

### Benchmark Results (Depth 5, 20 Plies)

```
Total nodes : 125,639,725 (~125M)
Total time  : 30.36 seconds
Avg time/ply: 1,518 ms
Overall NPS : ~4 Million nodes/second
```

The engine played real chess:
```
1. e2e4  Nc6   2. Nc3  Nf6
3. d4    e5    4. dxe5  Nxe5
5. Bf4   Nc6   6. e5   Nxe5
7. Bxe5  Qe7   8. Qe2  O-O-O
9. Nd5   Nxd5  10. O-O-O  Nb4
```

---

## What's Next

To improve the engine without changing its structure:

| Improvement | Expected effect |
|---|---|
| **MVV-LVA move ordering** | Reduce nodes 6M → ~1M per move at depth 5 |
| **Transposition table** | Cache positions, cut 30-50% of duplicate searches |
| **Quiescence search** | Stop evaluating positions mid-capture sequence |
| **Iterative deepening** | Make search time-based instead of depth-based |

The foundation (`evaluate` + `minimax` + `alpha-beta`) is the correct base.
Every future improvement builds on top of it without rewriting anything.
