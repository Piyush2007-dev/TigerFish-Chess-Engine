"""
compare_perft.py
Runs your engine's split perft and Stockfish side by side, highlights differences.

Usage:
    python compare_perft.py --engine ./perft.exe --stockfish ./stockfish-windows-x86-64-avx2.exe --fen "startpos" --depth 3

    python compare_perft.py --engine ./perft.exe --stockfish ./stockfish-windows-x86-64-avx2.exe --fen "rnbqkbnr/pppppppp/8/8/8/7P/PPPPPPP1/RNBQKBNR b KQkq - 0 1" --depth 2
    
    python compare_perft.py --engine ./perft.exe --stockfish ./stockfish-windows-x86-64-avx2.exe --fen "startpos" --moves "h2h3" --depth 2
"""

import subprocess
import argparse
import sys
import re
from collections import OrderedDict

# ── ANSI colours ────────────────────────────────────────────────────────────
RED    = "\033[91m"
GREEN  = "\033[92m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
BOLD   = "\033[1m"
RESET  = "\033[0m"

def supports_color():
    return sys.stdout.isatty()

def col(code, text):
    return code + text + RESET if supports_color() else text


# ── Run your engine ──────────────────────────────────────────────────────────
# Your engine must accept a FEN on stdin or as an argument and print:
#   <move>: <count>
#   ...
#   Total: <N>
#
# The script calls your engine as:
#   ./perft.exe <fen> <depth> [moves...]
#
# Adjust run_engine() below if your interface differs.

def run_engine(engine_path: str, fen: str, depth: int, moves: list[str]) -> dict[str, int]:
    """
    Call your engine.  Expects output like:
        a2a3: 380
        b2b3: 420
        ...
        Total: 8902
    """
    cmd = [engine_path, fen, str(depth)] + moves
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        print(col(RED, f"ERROR: engine not found at '{engine_path}'"))
        sys.exit(1)
    return parse_split_output(result.stdout)


# ── Run Stockfish ────────────────────────────────────────────────────────────
def run_stockfish(sf_path: str, fen: str, depth: int, moves: list[str]) -> dict[str, int]:
    """Send UCI commands to Stockfish and parse its perft output."""
    if fen.strip().lower() == "startpos":
        pos_cmd = "position startpos"
    else:
        pos_cmd = f"position fen {fen}"

    if moves:
        pos_cmd += " moves " + " ".join(moves)

    commands = f"uci\nisready\n{pos_cmd}\ngo perft {depth}\nquit\n"

    try:
        result = subprocess.run(
            [sf_path],
            input=commands,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        print(col(RED, f"ERROR: Stockfish not found at '{sf_path}'"))
        sys.exit(1)
    return parse_split_output(result.stdout)


# ── Parsers ──────────────────────────────────────────────────────────────────
def parse_split_output(text: str) -> dict[str, int]:
    """
    Parse lines of the form:
        a2a3: 380
        a2a3a1q: 4       (promotions)
    Also handles Stockfish's format which is identical.
    Returns an ordered dict move -> count.
    """
    result = OrderedDict()
    for line in text.splitlines():
        line = line.strip()
        # match   <move>: <number>   optionally preceded by spaces
        m = re.match(r'^([a-h][1-8][a-h][1-8][qrbnQRBN]?)\s*:\s*(\d+)$', line)
        if m:
            move  = m.group(1).lower()
            count = int(m.group(2))
            result[move] = result.get(move, 0) + count  # handle duplicates safely
    return result


# ── Comparison ───────────────────────────────────────────────────────────────
def compare(engine: dict[str, int], stockfish: dict[str, int]) -> int:
    """
    Print a side-by-side table.
    Returns number of differences found.
    """
    all_moves = sorted(set(list(engine.keys()) + list(stockfish.keys())))
    diffs = 0

    col_w = 10  # move column width

    header = (
        f"{'MOVE':<{col_w}}  {'YOUR ENGINE':>14}  {'STOCKFISH':>14}  {'DIFF':>10}"
    )
    print(col(BOLD, header))
    print("─" * len(header))

    for move in all_moves:
        eng_val = engine.get(move)
        sf_val  = stockfish.get(move)

        if eng_val is None:
            # missing from engine
            line = f"{move:<{col_w}}  {'MISSING':>14}  {sf_val:>14}  {'← missing':>10}"
            print(col(RED, line))
            diffs += 1
        elif sf_val is None:
            # extra in engine
            line = f"{move:<{col_w}}  {eng_val:>14}  {'MISSING':>14}  {'← EXTRA':>10}"
            print(col(RED, line))
            diffs += 1
        elif eng_val != sf_val:
            diff = eng_val - sf_val
            sign = "+" if diff > 0 else ""
            line = f"{move:<{col_w}}  {eng_val:>14}  {sf_val:>14}  {sign}{diff:>9}"
            print(col(YELLOW, line))
            diffs += 1
        else:
            line = f"{move:<{col_w}}  {eng_val:>14}  {sf_val:>14}  {'✓':>10}"
            print(line)

    print("─" * len(header))

    eng_total = sum(engine.values())
    sf_total  = sum(stockfish.values())
    total_diff = eng_total - sf_total
    sign = "+" if total_diff > 0 else ""
    total_line = (
        f"{'TOTAL':<{col_w}}  {eng_total:>14}  {sf_total:>14}  "
        f"{sign}{total_diff:>9}"
    )
    if total_diff != 0:
        print(col(BOLD + RED, total_line))
    else:
        print(col(BOLD + GREEN, total_line))

    return diffs


# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Compare your engine's split-perft against Stockfish"
    )
    parser.add_argument("--engine",    required=True,  help="Path to your engine exe")
    parser.add_argument("--stockfish", required=True,  help="Path to Stockfish exe")
    parser.add_argument("--fen",       default="startpos", help="FEN string or 'startpos'")
    parser.add_argument("--depth",     type=int, default=3, help="Perft depth")
    parser.add_argument("--moves",     nargs="*", default=[],
                        help="Moves to play before running perft (e.g. h2h3)")
    args = parser.parse_args()

    fen    = args.fen
    depth  = args.depth
    moves  = args.moves or []

    print(col(CYAN, f"\n{'='*60}"))
    print(col(CYAN, f"  FEN   : {fen}"))
    if moves:
        print(col(CYAN, f"  MOVES : {' '.join(moves)}"))
    print(col(CYAN, f"  DEPTH : {depth}"))
    print(col(CYAN, f"{'='*60}\n"))

    print("Running your engine...")
    engine_data = run_engine(args.engine, fen, depth, moves)
    if not engine_data:
        print(col(RED, "ERROR: engine produced no parseable output."))
        print(col(YELLOW, "Make sure your engine prints lines like:  a2a3: 380"))
        sys.exit(1)

    print(f"  → {len(engine_data)} moves parsed\n")

    print("Running Stockfish...")
    sf_data = run_stockfish(args.stockfish, fen, depth, moves)
    if not sf_data:
        print(col(RED, "ERROR: Stockfish produced no parseable output."))
        sys.exit(1)

    print(f"  → {len(sf_data)} moves parsed\n")

    diffs = compare(engine_data, sf_data)

    print()
    if diffs == 0:
        print(col(GREEN + BOLD, "✓  All moves match perfectly!"))
    else:
        print(col(RED + BOLD, f"✗  {diffs} difference(s) found."))
        print()
        print(col(YELLOW, "Tip: re-run with --moves to drill down into the bad position."))
        print(col(YELLOW, f"     e.g.  python compare_perft.py --engine {args.engine} "
                          f"--stockfish {args.stockfish} --fen \"{fen}\" "
                          f"--moves {' '.join(moves)} <bad_move> --depth {depth-1}"))
    print()


if __name__ == "__main__":
    main()