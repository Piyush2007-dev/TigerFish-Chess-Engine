# bench_python_chess.py — Benchmarks python-chess move generation speed
import time
import random
import chess

def benchmark():
    # Standard starting position + complex positions
    fens = [
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkb1r/1p2pppp/p2p1n2/6B1/3NP3/2N5/PPP2PPP/R2QKB1R w KQkq - 0 7",
        "r1b1kb1r/pp3ppp/1q2p3/8/3R4/1N2P3/PPP1Q1PP/3R2K1 b kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "8/5k2/3p4/1p5p/1P5P/3P4/5K2/8 w - - 0 1"
    ]

    boards = [chess.Board(fen) for fen in fens]
    iterations_per_board = 100_000

    print("======================================================")
    print("      python-chess (import chess) MoveGen Benchmark   ")
    print("======================================================")

    total_calls = 0
    total_time = 0.0

    for idx, board in enumerate(boards):
        t0 = time.perf_counter()
        count = 0
        for _ in range(iterations_per_board):
            moves = list(board.legal_moves)
            count = len(moves)
        t1 = time.perf_counter()

        elapsed = t1 - t0
        avg_us = (elapsed / iterations_per_board) * 1e6
        avg_ns = (elapsed / iterations_per_board) * 1e9
        cps = iterations_per_board / elapsed

        total_calls += iterations_per_board
        total_time += elapsed

        print(f"  Pos #{idx+1} ({count} moves) | Time: {elapsed*1000:.1f} ms | Avg: {avg_us:.2f} us ({avg_ns:.0f} ns) | Speed: {cps:,.0f} calls/sec")

    overall_avg_ns = (total_time / total_calls) * 1e9
    overall_avg_us = (total_time / total_calls) * 1e6
    overall_cps = total_calls / total_time

    print("======================================================")
    print(f"  TOTAL CALLS         : {total_calls:,}")
    print(f"  TOTAL TIME          : {total_time:.3f} seconds")
    print(f"  AVERAGE PER CALL    : {overall_avg_us:.2f} us ({overall_avg_ns:.0f} ns)")
    print(f"  THROUGHPUT SPEED    : {overall_cps:,.0f} calls/sec (~{overall_cps/1e3:.1f}K calls/sec)")
    print("══════════════════════════════════════════════════════════")

if __name__ == "__main__":
    benchmark()
