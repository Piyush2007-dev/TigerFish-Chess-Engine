# Chess Engine v0 — Full Session Log
> Date: 2026-07-08 | Session: Resuming after 3-week break

---

## 1. Project Recap (What existed before this session)

Before touching anything, I read the git history, file timestamps, and ENGINE_NOTES.md to reconstruct what was built previously:

- **Root `chess.cpp`** — original move generator (bitboard based), written first
- **Perft suite** — 4 tools (`perft.cpp`, `perft_split.cpp`, `perft_compare.cpp`, `perft_compare_dynamic.cpp`) built to validate the move generator against Stockfish
- **`tigerfish/` engine** — a named subdirectory created for the "real" engine:
  - `chess.cpp` (71KB, 2011 lines) — full engine library with Board, MoveGenerator, Engine class
  - `game.cpp` — CLI entry point that `#includes chess.cpp` and compiles to `chess.exe`
  - `bench.cpp` + `benchv2.cpp` — self-play benchmark tools
  - `pst_tables.cpp` — piece-square table data
  - `server.js` — Node.js HTTP server that spawns `chess_engine.exe` as a subprocess
  - `index.html` — web UI for playing against the engine
  - `ENGINE_NOTES.md` — detailed developer notes

**Engine benchmark results (from ENGINE_NOTES.md):**
- Depth 5, 20 plies self-play
- ~125M nodes total, ~4M nodes/second, ~1.5s per move

---

## 2. Created `benchmark/` Folder

**Reason:** Benchmark files (`bench.cpp`, `benchv2.cpp`, `bench_chess.cpp`) were sitting inside `tigerfish/` mixed with engine source. They serve a different purpose (speed testing) and should be separated.

**Files moved into `D:\chess_engine_v0\benchmark\`:**
- `tigerfish/bench.cpp` → `benchmark/bench.cpp`
- `tigerfish/benchv2.cpp` → `benchmark/benchv2.cpp`
- `tigerfish/bench.exe` → `benchmark/bench.exe`
- `tigerfish/benchv2.exe` → `benchmark/benchv2.exe`
- `bench_chess.cpp` (root) → `benchmark/bench_chess.cpp`
- `bench_chess.exe` (root) → `benchmark/bench_chess.exe`

**Command used:**
```powershell
New-Item -ItemType Directory -Path "D:\chess_engine_v0\benchmark"
Move-Item "D:\chess_engine_v0\tigerfish\bench.cpp" "D:\chess_engine_v0\benchmark\"
# ... (same for all 6 files)
```

---

## 3. Created `.gitignore`

**Reason:** No `.gitignore` existed. The repo had compiled `.exe` binaries, the Stockfish binary (114MB!), and no protection against accidentally committing editor files or Python caches.

**Created `D:\chess_engine_v0\.gitignore` with these sections:**

```gitignore
# Benchmark folder (not tracked)
benchmark/

# Compiled binaries
*.exe
*.o
*.obj
*.out
*.a *.lib *.dll *.so *.dylib

# Python virtual environment (uv)
.venv/
venv/
__pycache__/
*.py[cod]
*.egg-info/
dist/ build/

# uv lock / cache
.uv/
uv.lock

# Editor / IDE
.vscode/
.idea/
*.swp *.swo *~ .DS_Store Thumbs.db

# Build artifacts
*.d *.dSYM/ *.su *.idb *.pdb

# Logs and temp files
*.log *.tmp *.bak *.orig

# Stockfish binary
stockfish-windows-x86-64-avx2.exe
```

**Note:** First write had garbled emoji characters due to Windows UTF-8 BOM encoding issue. Rewrote using `[System.IO.File]::WriteAllText()` with explicit `UTF8` encoding to fix it.

---

## 4. Removed Files from Git History

**Reason:** `filter-repo` rewrites every commit to strip out specified files from all history — not just future commits. This permanently removes them so they can never be recovered from the git log.

**Installed `git-filter-repo`:**
```powershell
pip install git-filter-repo  # installed version 2.47.0
```

**Round 1 — Removed binaries and benchmark folder from history:**
```powershell
git filter-repo --path-glob "*.exe" --invert-paths --force
git filter-repo --path-glob "*.o" --invert-paths --force
git filter-repo --path "benchmark/" --invert-paths --force
```

> ⚠️ `filter-repo` automatically removes the `origin` remote as a safety measure every time it runs. Had to re-add it after each run.

**Round 2 — Removed `chess.py`, `rays.py`, `test.py` from history** (after user asked):
```powershell
git filter-repo --path chess.py --invert-paths --force
git filter-repo --path rays.py --invert-paths --force
git filter-repo --path test.py --invert-paths --force
```

**Re-added origin remote after each filter-repo run:**
```powershell
git remote add origin https://github.com/Piyush2007-dev/chess_engine_v0.git
```

---

## 5. Set Up Python Virtual Environment with `uv`

**Reason:** The project has Python scripts (`compare_perft.py`, etc.) and needs an isolated environment. `uv` is a modern, fast package manager (replacement for pip+venv).

**`uv` was already installed:** version `0.11.21`

**Commands:**
```powershell
uv init --no-readme --no-workspace   # created pyproject.toml + .python-version
uv venv .venv                        # created .venv/ using CPython 3.14.2
```

**Result:**
- `pyproject.toml` created (project name: `chess-engine-v0`)
- `.python-version` created
- `.venv/` created at `D:\chess_engine_v0\.venv\`
- Activate with: `.venv\Scripts\activate`

> Deleted `main.py` that `uv init` creates as boilerplate — not needed for this project.

---

## 6. Organized Root Files into Folders

**Reason:** After cleanup, many loose files remained in the root — old source versions, perft tools, Python test scripts. None of these should be in git.

**Created 3 new local-only folders:**

### `archive/` — Dead/old code
| File | Why archived |
|---|---|
| `chess.cpp` (root) | Old pre-tigerfish version of the engine |
| `chess.py` | Python prototype, superseded by C++ |
| `chess._ai_code.cpp` | Experimental scratch AI file |
| `chess_old_version.txt` | Manual backup of even older code |

### `perft/` — Validation tools
| File | Purpose |
|---|---|
| `perft.cpp` + `.exe` | Basic perft node counter |
| `perft_compare.cpp` + `.exe` | Compare vs Stockfish output |
| `perft_compare_dynamic.cpp` + `.exe` | Dynamic depth comparison |
| `perft_split.cpp` + `.exe` | Per-move split perft |
| `compare_perft.py` | Python automation script |

### `tools/` — Python utilities & tests
| File | Purpose |
|---|---|
| `rays.py` | Ray generation utility |
| `test.py` | Basic tests |
| `time_test.py` | Timing measurements |
| `test_game_ends.cpp` + `.exe` | Game termination tests |

**Also moved to `benchmark/`:**
- `stockfish-windows-x86-64-avx2.exe` (114MB Stockfish binary)

**Added all three folders to `.gitignore`:**
```gitignore
# Organised local-only folders (not tracked)
archive/
perft/
tools/
```

---

## 7. Clean Git Commit

After all the reorganization, staged only what should be tracked:

```powershell
git add .gitignore pyproject.toml .python-version tigerfish/
git commit -m "chore: clean repo structure - engine in tigerfish/, local-only folders gitignored"
```

**What got committed (9 files):**
```
.gitignore
.python-version
pyproject.toml
tigerfish/chess.cpp
tigerfish/game.cpp
tigerfish/ENGINE_NOTES.md
tigerfish/index.html
tigerfish/pst_tables.cpp   ← (later removed, see below)
tigerfish/server.js
```

**Commit hash:** `28ab917`

---

## 8. Analysed Tigerfish File Dependencies

Read all files in `tigerfish/` to map who uses what:

**Dependency chain discovered:**
```
chess.cpp   ← included by game.cpp (#include "chess.cpp")
game.cpp    → compiles to chess.exe / chess_engine.exe
server.js   → spawns chess_engine.exe via child_process.execFile()
index.html  → fetches /api/state and /api/move from server.js
```

**Bug found in `server.js` line 8:**
```js
// Was pointing to stale binary:
const ENGINE_EXE = path.join(__dirname, 'chess_engine.exe');
// Should point to fresh compiled output:
const ENGINE_EXE = path.join(__dirname, 'chess.exe');
```

---

## 9. Investigated `pst_tables.cpp`

Read the full contents of `pst_tables.cpp` and searched `chess.cpp` for any `#include` or reference to it.

**Finding:** `pst_tables.cpp` is a **complete orphan**. It is never `#include`d by any file.

The PST data (`PAWN_PST`, `KNIGHT_PST`, etc.) is **already defined inline** inside `chess.cpp` within the `Engine` class (lines 1834–1894) as `static constexpr` arrays.

**Extra thing `pst_tables.cpp` has that `chess.cpp` doesn't:** A `king_eg_pst` (endgame king table) — a future improvement that was designed but not integrated yet.

**Action:** Moved `pst_tables.cpp` → `archive/` since it's reference material, not live code.

---

## 10. Fixed `server.js` Exe Name

Changed `server.js` line 8:
```diff
- const ENGINE_EXE = path.join(__dirname, 'chess_engine.exe');
+ const ENGINE_EXE = path.join(__dirname, 'chess.exe');
```

**Then tested the server** — it didn't work properly with `chess.exe`.

**Reverted back:**
```diff
- const ENGINE_EXE = path.join(__dirname, 'chess.exe');
+ const ENGINE_EXE = path.join(__dirname, 'chess_engine.exe');
```

**Root cause analysis:** `chess_engine.exe` is a stale binary built from the old root `chess.cpp` (now in `archive/`). It works because the old code also had the same CLI interface. The new `chess.exe` (built from `game.cpp`) _should_ work but something in the compiled output differs.

**Best fix (not yet done):** Recompile `game.cpp` with output name `chess_engine.exe`:
```powershell
g++ -O2 -std=c++17 -o chess_engine.exe game.cpp
```

---

## 11. Server Test

Started `server.js` with Node.js v24.11.0:
```powershell
node server.js
# Output: TigerFish Chess server running at http://127.0.0.1:8080
```

Server confirmed running. Web UI accessible at `http://127.0.0.1:8080`.

---

## Final Project Structure

```
chess_engine_v0/
├── tigerfish/              ← 🟢 GIT TRACKED — the engine
│   ├── chess.cpp           ← 👑 MASTER FILE (2011 lines, full engine)
│   ├── game.cpp            ← entry point + CLI (#includes chess.cpp)
│   ├── server.js           ← Node.js HTTP server
│   ├── index.html          ← Web UI
│   └── ENGINE_NOTES.md     ← Developer documentation
│
├── .gitignore              ← 🟢 tracked
├── pyproject.toml          ← 🟢 tracked (uv project)
├── .python-version         ← 🟢 tracked (uv Python version pin)
│
├── benchmark/              ← 🔴 local only (gitignored)
│   ├── bench.cpp / benchv2.cpp / bench_chess.cpp
│   └── stockfish-windows-x86-64-avx2.exe
│
├── perft/                  ← 🔴 local only (gitignored)
│   └── perft*.cpp + compare_perft.py
│
├── archive/                ← 🔴 local only (gitignored)
│   ├── chess.cpp (old), chess.py, chess._ai_code.cpp
│   ├── chess_old_version.txt
│   └── pst_tables.cpp (orphan reference file)
│
├── tools/                  ← 🔴 local only (gitignored)
│   └── rays.py, test.py, time_test.py, test_game_ends.cpp
│
└── .venv/                  ← 🔴 local only (gitignored)
```

---

## Pending / Recommended Next Steps

| # | Task | Why |
|---|---|---|
| 1 | Recompile `game.cpp → chess_engine.exe` | Server uses fresh code |
| 2 | Implement **MVV-LVA move ordering** | Biggest engine speedup |
| 3 | Add **transposition table** | 30–50% fewer duplicate searches |
| 4 | Add **quiescence search** | Fix horizon effect |
| 5 | Add **iterative deepening** | Time-controlled search |
| 6 | `git push --force origin main` | Sync clean history to GitHub |
