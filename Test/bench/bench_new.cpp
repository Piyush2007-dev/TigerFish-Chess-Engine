// ═══ ENHANCED SEARCH BENCHMARK ══════════════════════════════════════
// Runs the enhanced engine (LMR + PVS + killers + null move + aspiration)
// on 10 test positions and prints nodes/time/NPS per position.
// Compile: g++ -O3 -std=c++20 -o bench_new.exe bench_new.cpp
// Run:     ./bench_new.exe

#include "../../engine/search.cpp"
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
using namespace std;
#include "bench_cases.cpp"
int main() { run_benchmark("NEW SEARCH (quiescence + killers + history + null move + LMR + PVS + aspiration)"); }
