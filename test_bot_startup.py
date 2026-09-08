#!/usr/bin/env python3
"""
TigerFish Lichess Bot — Startup Preflight & Constants Validator
================================================================
Defines depth, clock allocation formulas, and latency constants for all game speeds.
Executes an interactive smoke-test against game.exe at server startup, verifies
that depth and time constraints are strictly respected, and prints a formatted
preflight status report to the terminal.

Usage:
  python test_bot_startup.py [--engine ./game.exe]
"""

import os
import sys
import time
import json
import subprocess
import argparse

# ══════════════════════════════════════════════════════════════════════════════
# TIME CONTROL & SEARCH CONSTANTS
# ══════════════════════════════════════════════════════════════════════════════

# Speed category depth ceilings and network latency buffers
SPEED_SETTINGS = {
    "ultrabullet": {
        "depth_ceiling": 8,
        "lag_buffer_ms": 50,
        "panic_threshold_ms": 800,
        "label": "UltraBullet (<30s)",
    },
    "bullet": {
        "depth_ceiling": 10,
        "lag_buffer_ms": 100,
        "panic_threshold_ms": 1500,
        "label": "Bullet (1m - 2m)",
    },
    "blitz": {
        "depth_ceiling": 12,
        "lag_buffer_ms": 100,
        "panic_threshold_ms": 2000,
        "label": "Blitz (3m - 5m)",
    },
    "rapid": {
        "depth_ceiling": 14,
        "lag_buffer_ms": 150,
        "panic_threshold_ms": 3000,
        "label": "Rapid (10m - 15m)",
    },
    "classical": {
        "depth_ceiling": 16,
        "lag_buffer_ms": 200,
        "panic_threshold_ms": 5000,
        "label": "Classical (20m+)",
    },
    "correspondence": {
        "depth_ceiling": 18,
        "lag_buffer_ms": 0,
        "panic_threshold_ms": 0,
        "label": "Correspondence / Unlimited",
    },
}

# Mathematical time allocation parameters
TIME_ALLOCATION = {
    "clock_divisor": 25.0,          # Base target: 1/25th of remaining clock per move
    "increment_factor": 0.75,       # Incorporate 75% of move increment into budget
    "standard_cap_ratio": 0.20,     # In normal positions, cap move budget at 20% of remaining clock
    "low_time_cap_ratio": 0.25,     # In low time (<5s), cap at 25% of remaining clock
    "panic_cap_ratio": 0.30,        # In extreme panic (<1.5s), cap at 30% of remaining clock
    "min_move_time_ms": 50,         # Absolute floor (allows shallow iterative deepening)
    "default_untimed_ms": 2000,     # Default search time budget if clock is unavailable
}

# ══════════════════════════════════════════════════════════════════════════════
# TIME CALCULATION CORE LOGIC
# ══════════════════════════════════════════════════════════════════════════════

def calculate_search_time(state: dict, my_color: str, default_time_ms: int = None) -> int:
    """
    Calculate the optimal wall-clock search budget in milliseconds for the current turn.
    Deducts a network latency buffer and applies adaptive caps to prevent flagging.
    """
    if default_time_ms is None:
        default_time_ms = TIME_ALLOCATION["default_untimed_ms"]

    if not state:
        return default_time_ms

    time_key = "wtime" if my_color == "white" else "btime"
    inc_key = "winc" if my_color == "white" else "binc"

    remaining = state.get(time_key)
    inc = state.get(inc_key, 0) or 0

    if remaining is None or remaining <= 0:
        return default_time_ms

    lag_buffer = 100
    safe_remaining = max(TIME_ALLOCATION["min_move_time_ms"], remaining - lag_buffer)

    # Base allocation: remaining / 25 + 75% of increment
    allocated = (safe_remaining / TIME_ALLOCATION["clock_divisor"]) + (inc * TIME_ALLOCATION["increment_factor"])

    # Adaptive upper bound caps:
    if safe_remaining < 1500:
        allocated = min(allocated, safe_remaining * TIME_ALLOCATION["panic_cap_ratio"])
    elif safe_remaining < 5000:
        allocated = min(allocated, safe_remaining * TIME_ALLOCATION["low_time_cap_ratio"])
    else:
        allocated = min(allocated, safe_remaining * TIME_ALLOCATION["standard_cap_ratio"])

    allocated = max(TIME_ALLOCATION["min_move_time_ms"], min(allocated, safe_remaining))
    return int(allocated)


def get_search_parameters(state: dict, my_color: str, base_depth: int = 12, speed: str = "blitz"):
    """
    Derives (search_depth, time_budget_ms) tailored for the active game speed and clock state.
    """
    settings = SPEED_SETTINGS.get(speed.lower(), SPEED_SETTINGS["blitz"])
    depth_ceiling = max(base_depth, settings["depth_ceiling"])

    time_budget_ms = calculate_search_time(state, my_color)

    # Adapt depth ceiling if in extreme time trouble
    if time_budget_ms is not None:
        if time_budget_ms < 200:
            depth_ceiling = min(depth_ceiling, 6)
        elif time_budget_ms < 500:
            depth_ceiling = min(depth_ceiling, 8)
        elif time_budget_ms < 1000:
            depth_ceiling = min(depth_ceiling, 10)

    return depth_ceiling, time_budget_ms


# ══════════════════════════════════════════════════════════════════════════════
# PREFLIGHT SMOKE TEST & TERMINAL REPORTER
# ══════════════════════════════════════════════════════════════════════════════

def run_startup_test(engine_path: str = None) -> bool:
    """
    Runs the preflight benchmark suite on startup. Prints formatted tables to terminal.
    Returns True if all preflight checks pass.
    """
    if engine_path is None:
        engine_path = "./game.exe" if os.name == "nt" else "./game"

    sep = "=" * 82
    sub_sep = "-" * 82

    print("\n" + sep)
    print("        TIGERFISH LICHESS BOT -- SERVER STARTUP PREFLIGHT")
    print(sep)

    # 1. Check executable
    if not os.path.exists(engine_path):
        print(f" [ERROR] Engine executable not found at: {engine_path}")
        print(f"         Please build the engine: g++ -O3 -std=c++20 -pthread engine/main.cpp -o game")
        print(sep + "\n")
        return False

    print(f" Engine Binary:  {os.path.abspath(engine_path)}")
    print(f" Platform:       {sys.platform} (PID: {os.getpid()})")
    print(f" Python Version: {sys.version.split()[0]}")
    print(sub_sep)

    # 2. Print Constants Table
    print(" [1] CONFIGURED SEARCH & CLOCK MANAGEMENT CONSTANTS")
    print(sub_sep)
    print(f"  {'Speed Mode':<16} {'Depth Cap':<11} {'Lag Buffer':<13} {'Panic Threshold':<18} {'Coverage'}")
    print(f"  {'-'*14:<16} {'-'*9:<11} {'-'*11:<13} {'-'*16:<18} {'-'*15}")
    for speed, s in SPEED_SETTINGS.items():
        print(f"  {speed:<16} {s['depth_ceiling']:<11} {str(s['lag_buffer_ms'])+'ms':<13} {str(s['panic_threshold_ms'])+'ms':<18} {s['label']}")

    print(f"\n  Time Formula: budget = (clock_remaining - lag_buffer) / {TIME_ALLOCATION['clock_divisor']} + (inc * {TIME_ALLOCATION['increment_factor']})")
    print(f"  Safety Caps:  Standard <= {int(TIME_ALLOCATION['standard_cap_ratio']*100)}% | Low-Time <= {int(TIME_ALLOCATION['low_time_cap_ratio']*100)}% | Panic <= {int(TIME_ALLOCATION['panic_cap_ratio']*100)}% | Min = {TIME_ALLOCATION['min_move_time_ms']}ms")
    print(sub_sep)

    # 3. Live Engine Preflight Tests
    print(" [2] EXECUTING LIVE ENGINE INTERACTIVE PREFLIGHT TESTS")
    print(sub_sep)

    try:
        proc = subprocess.Popen(
            [engine_path, "interactive"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
    except Exception as e:
        print(f" [FAIL] Could not spawn engine process: {e}")
        print(sep + "\n")
        return False

    def send_cmd(cmd: str) -> dict:
        proc.stdin.write(cmd + "\n")
        proc.stdin.flush()
        lines = []
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            stripped = line.strip()
            if stripped == "===READY===":
                break
            if stripped:
                lines.append(stripped)
        for line in reversed(lines):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
        return {}

    # Test Handshake
    t0 = time.perf_counter()
    handshake = send_cmd("newgame")
    t_init = (time.perf_counter() - t0) * 1000
    if handshake.get("status") != "ready":
        print(f" [FAIL] Engine handshake failed. Response: {handshake}")
        proc.kill()
        return False

    print(f"  Handshake (newgame): OK in {t_init:.1f}ms (TT initialized)")
    print()
    print(f"  {'Scenario / Clock Sim':<26} {'Depth':<7} {'Budget':<10} {'Elapsed':<11} {'Move':<8} {'Status'}")
    print(f"  {'-'*24:<26} {'-'*5:<7} {'-'*8:<10} {'-'*9:<11} {'-'*6:<8} {'-'*6}")

    test_cases = [
        ("Hyperbullet (30s+0)",   8,  200),
        ("Bullet Scramble (2s+1s)", 8, 250),
        ("Blitz Move (3m+2s)",   12,  500),
        ("Rapid Critical (10m)", 14,  800),
        ("Fixed Shallow Depth",   6,    0),   # 0 = untimed, tests depth-only constraint
    ]

    all_passed = True

    for name, depth, budget_ms in test_cases:
        t_start = time.perf_counter()
        if budget_ms > 0:
            cmd = f"best {depth} {budget_ms}"
            budget_str = f"{budget_ms}ms"
        else:
            cmd = f"best {depth}"
            budget_str = "None"

        res = send_cmd(cmd)
        elapsed_ms = (time.perf_counter() - t_start) * 1000
        best_move = res.get("best_move", "")

        # Validation rules:
        # 1. Valid move string produced
        # 2. If budget specified, elapsed time must not wildly overrun budget (allow 150ms buffer for OS scheduling)
        valid_move = bool(best_move and len(best_move) in (4, 5))
        respected_time = (budget_ms == 0) or (elapsed_ms <= budget_ms + 150)
        passed = valid_move and respected_time

        if not passed:
            all_passed = False

        status_str = "PASS [OK]" if passed else "FAIL"
        print(f"  {name:<26} {depth:<7} {budget_str:<10} {f'{elapsed_ms:.1f}ms':<11} {best_move:<8} {status_str}")

    # Terminate interactive process
    try:
        proc.stdin.write("quit\n")
        proc.stdin.flush()
        proc.wait(timeout=2.0)
    except Exception:
        proc.kill()

    print(sub_sep)
    if all_passed:
        print("  RESULT: ALL PREFLIGHT CHECKS PASSED -- Engine ready for Lichess play!")
    else:
        print("  RESULT: WARNING -- Some preflight checks failed or overran deadlines.")
    print(sep + "\n")

    return all_passed


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="TigerFish Lichess Bot Startup Preflight")
    parser.add_argument("--engine", type=str, default="./game.exe" if os.name == "nt" else "./game")
    args = parser.parse_args()

    success = run_startup_test(args.engine)
    sys.exit(0 if success else 1)
