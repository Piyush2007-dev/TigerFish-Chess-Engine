#!/usr/bin/env python3
"""
Full Regression Test Suite for TigerFish Chess Engine
Compiles all test binaries, runs perft/rules/eval/tactics/search tests.
Saves auto-incrementing baseline JSON snapshots for comparison.
Usage: python Test/regression/run_all_tests.py
"""
import json
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "Test"


def run_command(command, *, input_text=None):
    result = subprocess.run(
        command,
        cwd=ROOT,
        input=input_text,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip(), result.stderr.strip(), result.returncode


def compile_test(compiler, output_path, source_path):
    return run_command([
        compiler,
        "-O2",
        "-std=c++20",
        "-I",
        "engine",
        "-o",
        str(output_path),
        str(source_path),
    ])


def count_named_results(output):
    results = re.findall(r"^(.+?):(?: -?\d+)? (PASS|FAIL)(?:\s*\(.*\))?\s*$", output, re.MULTILINE)
    return {
        "total": len(results),
        "passed": sum(status == "PASS" for _, status in results),
        "results": [{"name": name, "passed": status == "PASS"} for name, status in results],
    }


def main():
    compiler = shutil.which("g++")
    if compiler is None:
        print("ERROR: g++ was not found on PATH.")
        return 1

    print("=== TigerFish Phase 0 Baseline & Regression Suite ===")
    compiler_output, _, _ = run_command([compiler, "--version"])

    cases = TESTS / "regression" / "cases"
    sources = {
        "perft": cases / "test_perft.cpp",
        "rules": cases / "test_rules.cpp",
        "evaluation": cases / "test_evaluation.cpp",
        "tactics": cases / "test_search_tactics.cpp",
        "engine": ROOT / "engine" / "main.cpp",
        "eval_advanced": cases / "test_eval_advanced.cpp",
        "rules_advanced": cases / "test_rules_advanced.cpp",
        "search_advanced": cases / "test_search_advanced.cpp",
    }

    positions = TESTS / "regression" / "positions"
    tactical_suite = positions / "tactical_suite.json"
    positional_suite = positions / "positional_suite.json"
    endgame_suite = positions / "endgame_suite.json"

    build_dir = TESTS / "regression" / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    binaries = {name: build_dir / f"{name}.exe" for name in sources}

    for name in sources:
        print(f"Compiling {name}...")
        _, stderr, code = compile_test(compiler, binaries[name], sources[name])
        if code != 0:
            print(stderr)
            return 1

    perft_output, perft_stderr, perft_code = run_command([str(binaries["perft"])])
    rules_output, rules_stderr, rules_code = run_command([str(binaries["rules"])])
    evaluation_output, evaluation_stderr, evaluation_code = run_command([str(binaries["evaluation"])])
    tactics_output, tactics_stderr, tactics_code = run_command([str(binaries["tactics"])])
    eval_adv_output, eval_adv_stderr, eval_adv_code = run_command([str(binaries["eval_advanced"])])
    rules_adv_output, rules_adv_stderr, rules_adv_code = run_command([str(binaries["rules_advanced"])])
    search_adv_output, search_adv_stderr, search_adv_code = run_command([str(binaries["search_advanced"])])
    uci_output, uci_stderr, uci_code = run_command(
        [str(binaries["engine"])],
        input_text="uci\nisready\nposition startpos moves e2e4 e7e5\ngo depth 1\nquit\n",
    )
    analysis_output, analysis_stderr, analysis_code = run_command([
        str(binaries["engine"]),
        "analyze",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "5",
    ])
    suite_payload = {
        "tactical": json.loads(tactical_suite.read_text(encoding="utf-8-sig")) if tactical_suite.exists() else [],
        "positional": json.loads(positional_suite.read_text(encoding="utf-8-sig")) if positional_suite.exists() else [],
        "endgame": json.loads(endgame_suite.read_text(encoding="utf-8-sig")) if endgame_suite.exists() else [],
    }
    try:
        perft_results = json.loads(perft_output)
        tactics_results = json.loads(tactics_output)
        analysis_result = json.loads(analysis_output)
    except json.JSONDecodeError as error:
        print(f"ERROR: Invalid JSON from a test runner: {error}")
        return 1

    rules_results = count_named_results(rules_output)
    evaluation_results = count_named_results(evaluation_output)
    perft_passed = sum(result.get("passed", False) for result in perft_results)
    tactics_solved = sum(result.get("solved", False) for result in tactics_results)
    uci_ok = all(token in uci_output for token in ("uciok", "readyok", "bestmove ")) and uci_code == 0
    analysis_ok = analysis_code == 0 and bool(analysis_result.get("bestMove")) and analysis_result.get("depth") == 5

    report = {
        "engineVersion": "0.1",
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "environment": {
            "compiler": compiler_output.splitlines()[0] if compiler_output else compiler,
            "os": platform.platform(),
            "python": sys.version.split()[0],
            "engineSettings": {"threads": 1, "hashMb": 32},
        },
        "perft": {
            "total": len(perft_results),
            "passed": perft_passed,
            "results": perft_results,
        },
        "rules": rules_results,
        "evaluation": evaluation_results,
        "tactical": {
            "positions": len(tactics_results),
            "solved": tactics_solved,
            "accuracy": round(tactics_solved / len(tactics_results), 4) if tactics_results else 0,
            "results": tactics_results,
        },
        "protocol": {
            "uciHandshake": uci_ok,
            "analysisCli": analysis_ok,
            "output": uci_output.splitlines(),
        },
        "suites": suite_payload,
        "performance": {
            "position": "initial",
            "depth": analysis_result.get("depth", 0),
            "nodes": analysis_result.get("nodes", 0),
            "timeMs": analysis_result.get("timeMs", 0),
            "nps": analysis_result.get("nps", 0),
        },
        "eval_advanced": count_named_results(eval_adv_output),
        "rules_advanced": count_named_results(rules_adv_output),
        "search_advanced": count_named_results(search_adv_output),
        "failures": {
            "perftExitCode": perft_code,
            "rulesExitCode": rules_code,
            "evaluationExitCode": evaluation_code,
            "tacticsExitCode": tactics_code,
            "evalAdvancedExitCode": eval_adv_code,
            "rulesAdvancedExitCode": rules_adv_code,
            "searchAdvancedExitCode": search_adv_code,
            "analysisExitCode": analysis_code,
            "stderr": [perft_stderr, rules_stderr, evaluation_stderr, tactics_stderr, eval_adv_stderr, rules_adv_stderr, search_adv_stderr, uci_stderr, analysis_stderr],
        },
        "exitCode": 0 if (
            perft_code == 0
            and rules_code == 0
            and evaluation_code == 0
            and eval_adv_code == 0
            and rules_adv_code == 0
            and search_adv_code == 0
            and uci_ok
            and analysis_ok
        ) else 1,
    }

    output_dir = TESTS / "regression" / "output"
    output_dir.mkdir(parents=True, exist_ok=True)
    existing = list(output_dir.glob("baseline_v*.json"))
    max_ver = -1
    for f in existing:
        m = re.search(r"baseline_v(\d+)", f.name)
        if m:
            max_ver = max(max_ver, int(m.group(1)))
    baseline_path = output_dir / f"baseline_v{max_ver + 1}.json"
    baseline_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    eval_adv = count_named_results(eval_adv_output)
    rules_adv = count_named_results(rules_adv_output)
    search_adv = count_named_results(search_adv_output)

    print(f"Perft: {perft_passed}/{len(perft_results)} passed")
    print(f"Rules: {rules_results['passed']}/{rules_results['total']} passed")
    print(f"Evaluation: {evaluation_results['passed']}/{evaluation_results['total']} passed")
    print(f"Eval Advanced: {eval_adv['passed']}/{eval_adv['total']} passed")
    print(f"Rules Advanced: {rules_adv['passed']}/{rules_adv['total']} passed")
    print(f"Search Advanced: {search_adv['passed']}/{search_adv['total']} passed")
    print(f"Tactics baseline: {tactics_solved}/{len(tactics_results)} solved")
    print(f"UCI handshake: {'PASS' if uci_ok else 'FAIL'}")
    print(f"Analysis CLI: {'PASS' if analysis_ok else 'FAIL'}")
    print(f"Saved: {baseline_path}")

    return 0 if (
        perft_code == 0
        and rules_code == 0
        and evaluation_code == 0
        and eval_adv_code == 0
        and rules_adv_code == 0
        and search_adv_code == 0
        and uci_ok
        and analysis_ok
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
