// ═══ OLD SEARCH BENCHMARK ═════════════════════════════════════════
// Runs the old engine (plain minimax, no pruning tricks)
// on the same 10 test positions for direct comparison with bench_new.
// Compile: g++ -O3 -std=c++20 -o bench_old.exe bench_old.cpp
// Run:     ./bench_old.exe

#include "../../engine/oldsearch.cpp"
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
using namespace std;
using Engine = OldEngine;
#include "bench_cases.cpp"
int main() { run_benchmark("OLD SEARCH (minimax, no quiescence, no killers, no LMR)"); }
