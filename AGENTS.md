# TigerFish Agent Engineering Contract

> **MANDATORY FOR ALL AI AGENTS / MODELS**: Read this file before changing TigerFish. It defines how parallel changes are owned, implemented, verified, handed off, committed, and integrated. For engine internals, also read `OPERATIONS_MANUAL.md`; for branch and deployment policy, read `SOP.md`.

---

## 1. Operating Principles

- Change the smallest surface area that completely solves the assigned task.
- The existing source is authoritative. When a document and the executable disagree, preserve working behaviour, report the drift, and update the document as part of the relevant approved change.
- Correct chess rules and API compatibility are more important than speed. Measure performance before claiming an optimisation.
- Do not make a change merely because it is adjacent, stylistically different, or technically tempting. Refactors need an explicit task.

---

## 2. Parallel-Agent Protocol

### Before Editing

1. Inspect `git status`, the target files, their callers, and the closest relevant tests.
2. Announce a file claim in the task handoff: task, branch, files/interfaces owned, and expected external behaviour.
3. Check for overlapping work. A claimed file has one writer. Other agents may review it, propose a patch, or work in a separate module, but may not concurrently edit it.
4. Identify the boundary contract first when work crosses modules: input, output, error behaviour, ownership, and tests.

### While Editing

- Keep patches narrow and logically ordered. First establish or update the test/contract, then implement, then document the observable change.
- Do not overwrite, revert, stage, format, or move another agent's work. Stop and coordinate if a necessary change overlaps an owned file.
- A shared interface has a single designated owner. Consumers must not create local copies or incompatible interpretations of it.
- Do not use global mutable state as a shortcut for agent integration. Pass data through the existing layer boundary or define a deliberate interface.

### Handoff Template

Every agent finishes with this exact information:

```text
Scope: <completed task>
Branch: <branch name>
Files: <created/modified files>
Contract impact: <none | compatible addition | breaking change>
Validation: <commands and concise results>
Known limits: <none or unresolved item>
Commit: <hash or not created>
Remote action: <none unless explicitly authorized>
```

---

## 3. Git and Integration Rules

### Branch Routing

| Work type | Working branch | Integration destination |
|---|---|---|
| Engine feature, evaluation, search, tests, web-app work | `dev` or `codex/<scope>` branched from `dev` | `dev`, then approved release flow |
| Lichess bridge (`lichess_bot.py`, `lichess_multibot.py`) | `lichess` | `lichess` |
| Production release | No direct feature work on `main` | Merge only after verification |
| Cross-branch engine release | Authorized integration branch/worktree | `dev`, `main`, and `lichess` together |

- Use `codex/<short-scope>` for an isolated agent branch unless the task specifies an existing branch.
- Do not make feature changes directly on `main`.
- Do not merge, rebase shared history, force-push, delete a branch, modify Git configuration, or resolve another agent's conflict without explicit task-owner authorization.

### Commits and Pushes

- Create a focused, imperative commit only when the assigned checks pass, for example: `Add perft regression cases`.
- Stage only the task's files. Never commit `.env`, secrets, binaries, caches, archives, temporary logs, or unowned changes.
- Local commits are the normal handoff. Do not push, create a pull request, merge, tag, deploy, or change hosting configuration without explicit authorization.
- A finalized engine change is not considered released until the required `engine/` source and relevant engine documentation have been synchronized across `dev`, `main`, and `lichess`, as specified by `SOP.md`.

---

## 4. Engine Architecture Is a Contract

TigerFish currently compiles through one linear C++20 include cascade:

```text
main.cpp
  -> search.cpp
      -> rules.cpp
          -> board.cpp
              -> magic_lut.cpp
      -> eval_lut.cpp
```

| Layer | Owns | May depend on |
|---|---|---|
| Lookup tables | magic attacks, evaluation tables | C++ standard library only |
| Board | bitboards, mailbox, packed moves, FEN, make/unmake, Zobrist state | lookup tables |
| Rules | attacks, check/pin logic, legal moves, game result | board |
| Search | evaluation, transposition table, search policy | rules, evaluation tables |
| API / CLI | parsing, command dispatch, response serialization, process lifecycle | search API |

- Dependencies flow downward only. Do not introduce circular includes, duplicate a `Board`, or make lower layers depend on command parsing, JSON, Python, or application concerns.
- `engine/main.cpp` owns the production CLI and structured analysis command. Keep it focused on parsing command input, calling the engine, serializing responses, and managing process exit codes. Deep benchmark experiments belong under `engine_optimized/deep_engine_benchmark.cpp` and must not redefine the production boundary.
- Preserve the 32-bit move layout, square indexing, `Piece` encoding, FEN semantics, and Zobrist-history symmetry. Any intentional change needs a migration note, explicit tests, and documentation.
- `make_move()` and `unmake_move()` are paired operations. State changes involving captures, promotion, castling, en passant, side to move, counters, repetition, or hash keys require direct reversible-state tests.
- Avoid allocations, I/O, randomness, locks, and unbounded containers in hot search or move-generation paths unless the task explicitly justifies them with measurement.

---

## 5. API Routing and Compatibility

### Ownership Boundary

```text
External client (browser or Lichess)
    -> backend / bot bridge
        -> engine process or engine API
            -> Board + legal move generation
                -> search and evaluation
            -> structured response
```

- The browser never executes or trusts the engine directly. The backend owns authentication, authorization, rate limits, request-size limits, timeouts, process lifecycle, and transport concerns.
- The engine owns chess legality, state mutation, search, evaluation, and deterministic response semantics. UI and bridge code must not reimplement or bypass chess-rule validation.
- Treat FEN, UCI moves, CLI arguments, JSON fields, and remote events as untrusted at the boundary. Validate before altering board state or spawning work.
- Write machine-readable protocol output to `stdout` only and diagnostics to `stderr` only. Persistent sessions must preserve documented framing/sentinel behaviour and must not mix logs into responses.

### Changing an API

Before changing a command, JSON schema, response field, error condition, or persistent-protocol framing:

1. Locate every consumer: tests, Python bridges, backend code, documentation, and deployment scripts.
2. Decide whether the change is internal, backward-compatible, or breaking.
3. For a compatible change, preserve existing fields and behaviour; add the new contract and test both old and new paths.
4. For a breaking change, stop for explicit approval with a migration plan. The plan must name consumers, compatibility strategy, rollback plan, and release order.
5. Update the command/API documentation in the same patch.

New application routes require a written request schema, response schema, authorization rule, validation/error behaviour, resource limit, and integration test at the owning boundary before implementation.

---

## 6. File Changes and Formatting

- Preserve the local formatting, line endings, file layout, identifier style, and include order of each target file. Never run a whole-repository formatter or line-ending conversion on this legacy codebase unless explicitly asked.
- C++ follows the present style: four-space indentation, existing brace placement, `snake_case` for functions/variables, minimal includes, and compact aligned `switch` cases. Keep the single-translation-unit include model unless an approved architecture task changes it.
- Python follows the present style: four-space indentation, `snake_case`, module docstrings, section separators, and non-blocking `asyncio` I/O. Never add blocking network, subprocess, or long CPU work to the event loop.
- Markdown follows the existing project style: `#` title, optional concise blockquote, `---` between major sections, `##` headings, fenced language blocks, and restrained bullet lists. Amend existing tables/headings instead of rewriting documents wholesale.
- Update `README.md` for user-visible commands or setup; update `OPERATIONS_MANUAL.md` for engine architecture, data representation, or protocol changes; update `SOP.md` for workflow/deployment policy; and update `LICHESS_BOT_MANUAL.md` immediately when either Lichess bot script changes.

---

## 7. Test Matrix and Exit Criteria

| Changed area | Minimum verification |
|---|---|
| `board.cpp`, `rules.cpp`, `magic_lut.cpp` | Build; perft; focused legality cases for changed rules; make/unmake and hash consistency where state changes |
| `search.cpp`, `eval_lut.cpp` | Build; search/tactical regression; evaluation sanity cases; fixed-condition benchmark for performance claims |
| `main.cpp` | Build; valid request; invalid request; command/JSON compatibility; persistent protocol test when applicable |
| Lichess bot scripts | Syntax/import validation; bridge contract test; documented bot workflow updated |
| Documentation only | Verify command names, file paths, schema fields, and branch rules against source/configuration |

- Start with the smallest meaningful test and then run affected regression coverage.
- Record exact commands and results in the handoff. If an expected test cannot run, report the reason and the remaining risk.
- Benchmarks must state command, depth, node count, elapsed time, NPS, hash/thread settings, and relevant machine difference. Never call a change faster or stronger without comparable evidence.

---

## 8. Prohibited Actions

- Do not erase, reset, checkout over, or discard unowned work.
- Do not commit credentials, tokens, `.env`, machine-specific configuration, or private game data.
- Do not silently change output consumed by bots, backends, tests, operators, or deployment systems.
- Do not loosen legality checks, ignore invalid input, or mask a failed test to make a feature appear complete.
- Do not report an untested implementation, estimated benchmark, or speculative chess-strength claim as verified.

---

## 9. Definition of Done

- [ ] Scope is complete and unrelated changes are excluded.
- [ ] File/interface ownership was coordinated; unowned work is preserved.
- [ ] Public behaviour, documentation, and consumers are consistent.
- [ ] Required build/tests passed, or outstanding gaps are explicitly recorded.
- [ ] Handoff uses the required template.
- [ ] The change is ready for a focused local commit; no remote action occurred without authorization.
