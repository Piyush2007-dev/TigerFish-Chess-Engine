#!/usr/bin/env python3
"""
Transposition Table Benchmark: Enhanced vs Old Engine
Runs both engines on 10 positions at depths 3-8, compares TT hit rates.
Usage: python tt_benchmark.py
"""
import subprocess
import json
import sys
import os

# Test positions — varied openings, middlegame, endgame
POSITIONS = [
    ("Starting Pos",      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
    ("Sicilian",          "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"),
    ("Italian Game",      "r1bqkbnr/pppppppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 3"),
    ("Queen's Gambit",    "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 2"),
    ("Ruy Lopez",         "r1bqkbnr/pppppppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3"),
    ("Middlegame",        "r2qk2r/ppp2ppp/2n1bn2/2bpp3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 6"),
    ("Rook Endgame",      "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"),
    ("Pawn Endgame",      "8/5pk1/4p1p1/8/4P1P1/5PK1/8/8 w - - 0 1"),
    ("Mate in 2",         "r1b1kb1r/ppppqppp/2n2n2/4N3/2B1P3/8/PPPP1PPP/RNBQK2R w KQkq - 0 6"),
    ("Complex Position",  "r1bq1rk1/1ppn1ppp/p2pbn2/2b1p3/2BPP3/5N2/PPP2PPP/RNBQR1K1 w - - 0 8"),
]

DEPTHS = [3, 4, 5, 6, 7, 8]

def run_engine(exe_path, fen, depth):
    """Run engine and parse JSON output."""
    try:
        result = subprocess.run(
            [exe_path, fen, str(depth)],
            capture_output=True, text=True, timeout=120,
            encoding='utf-8', errors='replace'
        )
        if result.returncode != 0:
            return None
        return json.loads(result.stdout.strip())
    except (subprocess.TimeoutExpired, json.JSONDecodeError, Exception) as e:
        return None

def format_pct(n, total):
    if total == 0: return "  -"
    return f"{100.0*n/total:5.1f}%"

def format_num(n):
    if n >= 1_000_000: return f"{n/1_000_000:8.1f}M"
    if n >= 1_000:     return f"{n/1_000:8.1f}K"
    return f"{n:8d}"

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    old_exe = str(Path(__file__).parent.parent / "bench" / "bench_runner_old.exe")
    new_exe = str(Path(__file__).parent.parent / "bench" / "bench_runner_enhanced.exe")
    
    # Check executables exist
    for exe in [old_exe, new_exe]:
        if not os.path.exists(exe):
            print(f"Error: {exe} not found. Build with:")
            print(f"  g++ -O3 -std=c++20 -I ../../engine bench_runner_old.cpp -o {old_exe}")
            print(f"  g++ -O3 -std=c++20 -I ../../engine bench_runner_enhanced.cpp -o {new_exe}")
            sys.exit(1)
    
    print("=" * 90)
    print("              TRANSPOSITION TABLE EFFICIENCY BENCHMARK")
    print("                    Enhanced vs Old Search Engine")
    print("=" * 90)
    print()
    
    # Collect all results
    all_results = {}  # {position_name: {depth: {old: data, new: data}}}
    
    for pos_name, fen in POSITIONS:
        all_results[pos_name] = {}
        for depth in DEPTHS:
            old_data = run_engine(old_exe, fen, depth)
            new_data = run_engine(new_exe, fen, depth)
            all_results[pos_name][depth] = {"old": old_data, "new": new_data}
    
    # ── 1. Per-Position TT Hit Rates ──
    print("┌─────────────────────────────────────────────────────────────────────────────────┐")
    print("│  1. TT HIT RATE BY POSITION (depth {max_depth}, iterative deepening)                    │")
    print("├──────────────────────┬────────────────┬────────────────┬───────────────────────┤")
    print("│ Position             │ Old Hits/Miss  │ New Hits/Miss  │ Old Rate │ New Rate   │")
    print("├──────────────────────┼────────────────┼────────────────┼───────────────────────┤")
    
    max_depth = max(DEPTHS)
    for pos_name, _ in POSITIONS:
        d = all_results.get(pos_name, {}).get(max_depth, {})
        if d is None: d = {}
        od = d.get("old")
        nd = d.get("new")
        if od and nd:
            oh, om = od["tt_hits"], od["tt_misses"]
            nh, nm = nd["tt_hits"], nd["tt_misses"]
            print(f"│ {pos_name:<20} │ {oh:>8}/{om:<8} │ {nh:>8}/{nm:<8} │ {format_pct(oh,oh+om):>8} │ {format_pct(nh,nh+nm):>8} │")
        elif od:
            oh, om = od["tt_hits"], od["tt_misses"]
            print(f"│ {pos_name:<20} │ {oh:>8}/{om:<8} │ {'N/A':>15} │ {format_pct(oh,oh+om):>8} │ {'N/A':>8} │")
    
    print("└──────────────────────┴────────────────┴────────────────┴───────────────────────┘")
    print()
    
    # ── 2. TT Hit Rate by Depth (averaged across positions) ──
    print("┌─────────────────────────────────────────────────────────────────────────────────┐")
    print("│  2. TT HIT RATE BY DEPTH (averaged across all positions)                      │")
    print("├───────┬────────────────┬────────────────┬────────────────┬───────────────────┤")
    print("│ Depth │ Old Nodes      │ New Nodes      │ Old TT Rate    │ New TT Rate      │")
    print("├───────┼────────────────┼────────────────┼────────────────┼───────────────────┤")
    
    for depth in DEPTHS:
        old_total_hits = 0
        old_total_miss = 0
        new_total_hits = 0
        new_total_miss = 0
        old_total_nodes = 0
        new_total_nodes = 0
        count = 0
        
        for pos_name, _ in POSITIONS:
            d = all_results[pos_name].get(depth, {})
            od = d.get("old")
            nd = d.get("new")
            if od:
                old_total_hits += od["tt_hits"]
                old_total_miss += od["tt_misses"]
                old_total_nodes += od["nodes"]
            if nd:
                new_total_hits += nd["tt_hits"]
                new_total_miss += nd["tt_misses"]
                new_total_nodes += nd["nodes"]
            if od or nd:
                count += 1
        
        if count > 0:
            old_rate = old_total_hits / max(1, old_total_hits + old_total_miss)
            new_rate = new_total_hits / max(1, new_total_hits + new_total_miss)
            old_avg_nodes = old_total_nodes // count
            new_avg_nodes = new_total_nodes // count
            print(f"│  {depth:>3}  │ {format_num(old_avg_nodes):>14} │ {format_num(new_avg_nodes):>14} │ {format_pct(old_total_hits, old_total_hits+old_total_miss):>14} │ {format_pct(new_total_hits, new_total_hits+new_total_miss):>14} │")
    
    print("└───────┴────────────────┴────────────────┴────────────────┴───────────────────┘")
    print()
    
    # ── 3. TT Contribution to Pruning ──
    print("┌─────────────────────────────────────────────────────────────────────────────────┐")
    print("│  3. TT CONTRIBUTION TO PRUNING (depth {max_depth})                                      │")
    print("│  How much of the total search space did the TT eliminate?                     │")
    print("├──────────────────────┬────────────────┬────────────────┬───────────────────────┤")
    print("│ Position             │ Old TT Saves   │ New TT Saves   │ Old Pruned │ New Pruned│")
    print("├──────────────────────┼────────────────┼────────────────┼───────────────────────┤")
    
    for pos_name, _ in POSITIONS:
        d = all_results.get(pos_name, {}).get(max_depth, {})
        if d is None: d = {}
        od = d.get("old")
        nd = d.get("new")
        if od and nd:
            old_saved = od["tt_hits"]
            new_saved = nd["tt_hits"]
            old_nodes = od["nodes"]
            new_nodes = nd["nodes"]
            old_pct = 100.0 * old_saved / max(1, old_nodes + old_saved)
            new_pct = 100.0 * new_saved / max(1, new_nodes + new_saved)
            print(f"│ {pos_name:<20} │ {format_num(old_saved):>14} │ {format_num(new_saved):>14} │ {old_pct:>8.1f}%   │ {new_pct:>8.1f}%  │")
    
    print("└──────────────────────┴────────────────┴────────────────┴───────────────────────┘")
    print()
    
    # ── 4. Summary ──
    print("┌─────────────────────────────────────────────────────────────────────────────────┐")
    print("│  4. SUMMARY                                                                   │")
    print("└─────────────────────────────────────────────────────────────────────────────────┘")
    
    # Aggregate at max depth
    def safe_get(p, d, side, key, default=0):
        dd = all_results.get(p, {}).get(d)
        if dd is None: return default
        s = dd.get(side)
        if s is None: return default
        return s.get(key, default)
    
    old_h8 = sum(safe_get(p, max_depth, "old", "tt_hits") for p, _ in POSITIONS)
    old_m8 = sum(safe_get(p, max_depth, "old", "tt_misses") for p, _ in POSITIONS)
    new_h8 = sum(safe_get(p, max_depth, "new", "tt_hits") for p, _ in POSITIONS)
    new_m8 = sum(safe_get(p, max_depth, "new", "tt_misses") for p, _ in POSITIONS)
    old_n8 = sum(safe_get(p, max_depth, "old", "nodes") for p, _ in POSITIONS)
    new_n8 = sum(safe_get(p, max_depth, "new", "nodes") for p, _ in POSITIONS)
    
    print(f"  Old engine (depth {max_depth} avg):")
    print(f"    Total TT hits:   {old_h8:>12,}   ({format_pct(old_h8, old_h8+old_m8)} hit rate)")
    print(f"    Total TT misses: {old_m8:>12,}")
    print(f"    Total nodes:     {old_n8:>12,}")
    print(f"    Nodes pruned by TT: {100.0*old_h8/max(1,old_h8+old_n8):.1f}%")
    print()
    print(f"  Enhanced engine (depth {max_depth} avg):")
    print(f"    Total TT hits:   {new_h8:>12,}   ({format_pct(new_h8, new_h8+new_m8)} hit rate)")
    print(f"    Total TT misses: {new_m8:>12,}")
    print(f"    Total nodes:     {new_n8:>12,}")
    print(f"    Nodes pruned by TT: {100.0*new_h8/max(1,new_h8+new_n8):.1f}%")
    print()
    
    ratio = old_n8 / max(1, new_n8)
    print(f"  Enhanced search efficiency: {ratio:.1f}x fewer nodes")
    print(f"  Enhanced TT hit rate: {format_pct(new_h8, new_h8+new_m8)} vs Old {format_pct(old_h8, old_h8+old_m8)}")
    print()

if __name__ == "__main__":
    main()
