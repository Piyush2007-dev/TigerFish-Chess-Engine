#!/usr/bin/env python3
"""
Multi-threaded Benchmark: Enhanced vs Old Engine
Runs both engines on standard positions + tactical puzzles via CLI runners.
Compares nodes, time, NPS, and tactical solve rate.
Usage: python run_multithreaded_benchmark.py
"""
import json
import os
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "Test"

OLD_RUNNER = TESTS / "bench" / "bench_runner_old.exe"
ENHANCED_RUNNER = TESTS / "bench" / "bench_runner_enhanced.exe"


def run_engine_instance(exe_path, fen, depth):
    cmd = [str(exe_path), fen, str(depth)]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if res.returncode == 0 and res.stdout.strip():
            return json.loads(res.stdout.strip())
    except Exception as e:
        pass
    return {"move": "none", "score": 0, "depth": 0, "nodes": 0, "time_ms": 0.0, "nps": 0}


def run_benchmark_suite(runner_exe, test_items, max_workers=8):
    results = [None] * len(test_items)
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        future_to_idx = {
            executor.submit(run_engine_instance, runner_exe, item["fen"], item.get("depth", 4)): i
            for i, item in enumerate(test_items)
        }
        for future in as_completed(future_to_idx):
            idx = future_to_idx[future]
            try:
                results[idx] = future.result()
            except Exception:
                results[idx] = {"move": "none", "score": 0, "depth": 0, "nodes": 0, "time_ms": 0.0, "nps": 0}
    return results


def main():
    print("=== Compiling Benchmark Runners ===")
    subprocess.run(["g++", "-O3", "-std=c++20", "-I", "engine", str(TESTS / "bench" / "bench_runner_old.cpp"), "-o", str(OLD_RUNNER)], cwd=ROOT, check=True)
    subprocess.run(["g++", "-O3", "-std=c++20", "-I", "engine", str(TESTS / "bench" / "bench_runner_enhanced.cpp"), "-o", str(ENHANCED_RUNNER)], cwd=ROOT, check=True)
    print("Compilation successful!\n")

    # ── 1. Standard Depth Test Positions ──────────────────────────────
    standard_positions = [
        {"name": "Starting Position", "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "depth": 6},
        {"name": "Sicilian Defense", "fen": "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "depth": 6},
        {"name": "Italian Game", "fen": "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3", "depth": 6},
        {"name": "Queen's Gambit", "fen": "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2", "depth": 6},
        {"name": "Middlegame Complex", "fen": "r1bqkb1r/pppppppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 4", "depth": 6},
        {"name": "Rook Endgame", "fen": "8/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1", "depth": 7},
        {"name": "Mate-in-2 Queen/Rook", "fen": "2k5/8/1Q6/8/8/8/8/4K2R w K - 0 1", "depth": 6},
        {"name": "Pawn Endgame", "fen": "8/4k3/8/4P3/8/8/4K3/8 w - - 0 1", "depth": 8},
    ]

    print(f"[*] Running Standard Depth Performance Benchmarks ({len(standard_positions)} positions, multi-threaded)...")
    old_std_results = run_benchmark_suite(OLD_RUNNER, standard_positions, max_workers=os.cpu_count() or 4)
    enhanced_std_results = run_benchmark_suite(ENHANCED_RUNNER, standard_positions, max_workers=os.cpu_count() or 4)

    # ── 2. Tactical Smartness Suite (All Positions) ───────────────────
    tactical_path = TESTS / "regression" / "positions" / "tactical_suite.json"
    tactical_items = json.loads(tactical_path.read_text(encoding="utf-8-sig"))
    # Evaluate tactical suite at depth 4
    for item in tactical_items:
        item["depth"] = 4

    print(f"[*] Running Full Tactical Benchmark Suite ({len(tactical_items)} tactical puzzles, multi-threaded across {os.cpu_count()} CPU threads)...")
    start_t = time.time()
    old_tactics = run_benchmark_suite(OLD_RUNNER, tactical_items, max_workers=os.cpu_count() or 8)
    old_tactics_time = time.time() - start_t

    start_t = time.time()
    enhanced_tactics = run_benchmark_suite(ENHANCED_RUNNER, tactical_items, max_workers=os.cpu_count() or 8)
    enhanced_tactics_time = time.time() - start_t

    # ── Compute Aggregates ───────────────────────────────────────────
    old_solved = sum(1 for i, res in enumerate(old_tactics) if res["move"] == tactical_items[i]["expected_best_move"])
    enhanced_solved = sum(1 for i, res in enumerate(enhanced_tactics) if res["move"] == tactical_items[i]["expected_best_move"])

    old_total_nodes = sum(res["nodes"] for res in old_std_results)
    enhanced_total_nodes = sum(res["nodes"] for res in enhanced_std_results)

    old_total_time = sum(res["time_ms"] for res in old_std_results)
    enhanced_total_time = sum(res["time_ms"] for res in enhanced_std_results)

    old_nps = int(old_total_nodes * 1000.0 / old_total_time) if old_total_time > 0 else 0
    enhanced_nps = int(enhanced_total_nodes * 1000.0 / enhanced_total_time) if enhanced_total_time > 0 else 0

    print("\n" + "=" * 80)
    print("             TIGERFISH BENCHMARK COMPARISON REPORT")
    print("=" * 80)
    print(f"{'Metric':<30} | {'oldsearch.cpp':<20} | {'search_enhanced.cpp':<20} | {'Difference / Gain':<15}")
    print("-" * 80)
    print(f"{'Tactics Solved':<30} | {f'{old_solved}/{len(tactical_items)} ({old_solved/len(tactical_items)*100:.1f}%)':<20} | {f'{enhanced_solved}/{len(tactical_items)} ({enhanced_solved/len(tactical_items)*100:.1f}%)':<20} | {f'+{enhanced_solved - old_solved} solved (+{(enhanced_solved-old_solved)/len(tactical_items)*100:.1f}%)':<15}")
    print(f"{'Total Search Nodes (Standard)':<30} | {f'{old_total_nodes:,}':<20} | {f'{enhanced_total_nodes:,}':<20} | {f'{enhanced_total_nodes/old_total_nodes:.2f}x nodes' if old_total_nodes else 'N/A':<15}")
    print(f"{'Total Time (Standard ms)':<30} | {f'{old_total_time:.1f} ms':<20} | {f'{enhanced_total_time:.1f} ms':<20} | {f'{old_total_time/enhanced_total_time:.2f}x faster' if enhanced_total_time else 'N/A':<15}")
    print(f"{'Average Speed (NPS)':<30} | {f'{old_nps:,} NPS':<20} | {f'{enhanced_nps:,} NPS':<20} | {f'{enhanced_nps/old_nps:.2f}x NPS' if old_nps else 'N/A':<15}")
    print(f"{'Tactical Suite Wall Clock':<30} | {f'{old_tactics_time:.2f}s':<20} | {f'{enhanced_tactics_time:.2f}s':<20} | {f'{old_tactics_time/enhanced_tactics_time:.2f}x parallel speed' if enhanced_tactics_time else 'N/A':<15}")
    print("=" * 80)

    print("\n[*] Standard Position Breakdown:")
    print(f"{'Position Name':<25} | {'Depth':<5} | {'Old Nodes':<12} | {'New Nodes':<12} | {'Old Time':<10} | {'New Time':<10} | {'New Move'}")
    print("-" * 95)
    for i, pos in enumerate(standard_positions):
        o = old_std_results[i]
        n = enhanced_std_results[i]
        print(f"{pos['name']:<25} | {pos['depth']:<5} | {o['nodes']:<12,d} | {n['nodes']:<12,d} | {o['time_ms']:<7.1f}ms | {n['time_ms']:<7.1f}ms | {n['move']}")

    # Save summary json
    summary = {
        "tactical_accuracy": {
            "total_positions": len(tactical_items),
            "oldsearch_solved": old_solved,
            "oldsearch_rate": old_solved / len(tactical_items),
            "enhanced_solved": enhanced_solved,
            "enhanced_rate": enhanced_solved / len(tactical_items),
        },
        "performance_standard_positions": {
            "oldsearch_nodes": old_total_nodes,
            "oldsearch_time_ms": old_total_time,
            "oldsearch_nps": old_nps,
            "enhanced_nodes": enhanced_total_nodes,
            "enhanced_time_ms": enhanced_total_time,
            "enhanced_nps": enhanced_nps,
            "speedup_ratio": round(old_total_time / enhanced_total_time, 2) if enhanced_total_time > 0 else 1.0,
        }
    }
    output_dir = TESTS / "output"
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "benchmark_comparison_result.json").write_text(json.dumps(summary, indent=2))
    print(f"\nSaved detailed summary to {output_dir / 'benchmark_comparison_result.json'}")


if __name__ == "__main__":
    main()
