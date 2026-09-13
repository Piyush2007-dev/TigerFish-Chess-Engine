#!/usr/bin/env python3
"""
run_15_perft_tests.py
====================
Runs high-performance parallel Perft tests across 15 benchmark and random positions
at target depth (up to depth 8) using all available CPU threads (e.g. 24 threads).

Uses perft_compare_dynamic / perft_compare executables.
"""

import os
import sys
import subprocess
import time
import argparse

# 15 Benchmark & Diverse Chess Positions
TEST_POSITIONS = [
    {
        "name": "Pos 1: Standard Startpos",
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "depth": 8,
    },
    {
        "name": "Pos 2: Kiwipete",
        "fen": "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "depth": 8,
    },
    {
        "name": "Pos 3: Endgame / EP / Pins",
        "fen": "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "depth": 8,
    },
    {
        "name": "Pos 4: Mirrored Promotions & Checks",
        "fen": "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "depth": 8,
    },
    {
        "name": "Pos 5: Talkchess Position",
        "fen": "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "depth": 8,
    },
    {
        "name": "Pos 6: Highly Symmetric Midgame",
        "fen": "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "depth": 8,
    },
    {
        "name": "Pos 7: Pin & Discovery Attack",
        "fen": "r1b1kb1r/pp3ppp/1q2p3/8/3R4/1N2P3/PPP1Q1PP/3R2K1 b kq - 0 1",
        "depth": 8,
    },
    {
        "name": "Pos 8: Complex Queen & Knight Tangle",
        "fen": "r2q1rk1/ppp2ppp/2n1bn2/2b1p3/3pP3/3P1N2/PPP1BPPP/R1BQ1RK1 w - - 0 8",
        "depth": 8,
    },
    {
        "name": "Pos 9: Sharp Pawn Storm",
        "fen": "r3k2r/p1pp1ppp/1b3nbN/nB6/1B2P3/q4N2/Pp3PPP/R2Q1RK1 b kq - 0 1",
        "depth": 8,
    },
    {
        "name": "Pos 10: Deep Pawn Endgame",
        "fen": "8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1",
        "depth": 8,
    },
    {
        "name": "Pos 11: French Defence Structure",
        "fen": "r1b1k2r/pp3ppp/2n1p3/2b5/8/2N1PN2/PPP3PP/R2Q1RK1 w kq - 0 11",
        "depth": 8,
    },
    {
        "name": "Pos 12: King Safety Under Fire",
        "fen": "2rq1rk1/pp1b1ppp/1n2p3/3pP3/1b1P4/1P1B1N2/P1P2PPP/R1BQ1RK1 w - - 0 12",
        "depth": 8,
    },
    {
        "name": "Pos 13: Heavy Piece Midgame Chaos",
        "fen": "r1b2rk1/2q1bppp/p2ppn2/1p6/3BPP2/2N2B2/PPP3PP/R2Q1R1K w - - 0 13",
        "depth": 8,
    },
    {
        "name": "Pos 14: Sicilian Dragon Tension",
        "fen": "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 3 9",
        "depth": 8,
    },
    {
        "name": "Pos 15: Open Center Tactics",
        "fen": "r1bqk2r/pppp1ppp/2n5/4p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R b KQkq - 0 4",
        "depth": 8,
    },
]

def main():
    parser = argparse.ArgumentParser(description="Run 15 Perft Benchmark Tests across 24 Threads")
    parser.add_argument("--exe", default="./old_files/perft/perft_compare_dynamic.exe", help="Path to perft executable")
    parser.add_argument("--max-depth-override", type=int, default=None, help="Override depth for all positions")
    parser.add_argument("--threads", type=int, default=os.cpu_count() or 24, help="Number of CPU threads to use")
    args = parser.parse_args()

    exe_path = args.exe
    if not os.path.exists(exe_path):
        print(f"Error: Executable not found at {exe_path}")
        sys.exit(1)

    print("=" * 80)
    print(f"  TIGERFISH PARALLEL PERFT SUITE (15 POSITIONS @ UP TO DEPTH 8)")
    print(f"  Executable : {exe_path}")
    print(f"  Threads    : {args.threads} logical CPU cores")
    print("=" * 80)
    print()

    total_suite_nodes = 0
    start_suite_time = time.time()
    results_table = []

    for idx, test in enumerate(TEST_POSITIONS, 1):
        depth = args.max_depth_override if args.max_depth_override is not None else test["depth"]
        fen = test["fen"]
        name = test["name"]

        print(f"[{idx}/15] {name} | Depth: {depth}")
        print(f"       FEN: {fen}")

        cmd = [exe_path, fen, str(depth), "--threads", str(args.threads), "--no-sf"]
        t0 = time.time()
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
            elapsed = time.time() - t0
            output = res.stdout

            # Parse total nodes from stdout
            total_nodes = 0
            for line in output.splitlines():
                if "TOTAL" in line:
                    parts = line.split()
                    for p in parts:
                        if p.isdigit():
                            total_nodes = int(p)

            mnps = (total_nodes / elapsed / 1e6) if elapsed > 0 else 0
            total_suite_nodes += total_nodes

            print(f"       -> Nodes: {total_nodes:,} | Time: {elapsed:.3f}s | Speed: {mnps:.2f} Mnps\n")
            results_table.append((name, depth, total_nodes, elapsed, mnps))
        except subprocess.TimeoutExpired:
            print(f"       -> TIMED OUT (>120s)\n")
            results_table.append((name, depth, 0, 120.0, 0.0))
        except Exception as e:
            print(f"       -> ERROR: {e}\n")
            results_table.append((name, depth, 0, 0.0, 0.0))

    elapsed_suite = time.time() - start_suite_time
    avg_mnps = (total_suite_nodes / elapsed_suite / 1e6) if elapsed_suite > 0 else 0

    print("=" * 80)
    print(f"                     SUITE SUMMARY RESULTS")
    print("=" * 80)
    print(f"{'Position Name':<35} | {'Depth':<5} | {'Nodes':<14} | {'Time (s)':<8} | {'Speed (Mnps)':<12}")
    print("-" * 80)
    for name, depth, nodes, t_sec, speed in results_table:
        print(f"{name:<35} | {depth:<5} | {nodes:<14,} | {t_sec:<8.3f} | {speed:<12.2f}")
    print("-" * 80)
    print(f"Total Nodes Processed : {total_suite_nodes:,}")
    print(f"Total Suite Time      : {elapsed_suite:.2f}s")
    print(f"Average Engine Speed  : {avg_mnps:.2f} Mnps across {args.threads} threads")
    print("=" * 80)

if __name__ == "__main__":
    main()
