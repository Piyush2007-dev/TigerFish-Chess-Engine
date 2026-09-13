#!/usr/bin/env python3
"""
Chess Engine Match Runner
Plays games between two engine executables via UCI protocol.
Tracks wins/losses/draws and estimates ELO difference.

Usage:
    python match.py --engine1 game.exe --engine2 old_game.exe --games 100 --depth 8
    python match.py --engine1 game.exe --engine2 old_game.exe --games 20 --movetime 1000
"""

import subprocess
import sys
import time
import random
import argparse
import os
import threading
from dataclasses import dataclass, field
from typing import Optional

# ── Position Bank ─────────────────────────────────────────────────
START_POSITIONS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pp2pppp/3p4/2p5/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4",
    "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
    "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
    "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pp2pppp/2p5/3p4/2PP4/4PN2/PP3PPP/RNBQKB1R b KQkq - 0 4",
    "rnbqkb1r/pppppp1p/5np1/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkb1r/ppp1pp1p/3p1np1/4P3/2PP4/2N5/PP3PPP/R1BQKBNR b KQkq - 0 5",
    "r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 4 4",
    "r1bqkb1r/1ppp1ppp/p1n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 6",
    "rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 0 3",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/2p5/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/2P1P3/8/PP1P1PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2",
    "rnbqkbnr/ppp1pppp/8/3p4/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 4",
    "rnbqkb1r/ppp1pp1p/3p1np1/8/2PPP3/2N5/PP3PPP/R1BQKBNR b KQkq - 0 4",
    "r1bq1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7",
    "r2q1rk1/pppb1ppp/2np1n2/2b1p3/2B1P3/2NP1N2/PPP1QPPP/R1B2RK1 w - - 0 8",
]


@dataclass
class GameResult:
    winner: str
    moves: list
    result_code: str
    termination: str
    engine1_color: str


@dataclass
class MatchStats:
    engine1_wins: int = 0
    engine2_wins: int = 0
    draws: int = 0
    total_games: int = 0
    engine1_white_wins: int = 0
    engine1_white_losses: int = 0

    @property
    def engine1_score(self):
        return self.engine1_wins + self.draws * 0.5

    @property
    def engine2_score(self):
        return self.engine2_wins + self.draws * 0.5

    def win_rate(self):
        if self.total_games == 0:
            return 0.0
        return self.engine1_score / self.total_games * 100

    def elo_difference(self):
        import math
        if self.engine1_score == 0 or self.engine2_score == 0:
            return 0
        ratio = self.engine1_score / self.total_games
        if ratio >= 0.99:
            return 9999
        if ratio <= 0.01:
            return -9999
        elo = -400 * math.log10(1.0 / ratio - 1.0)
        return int(elo)


class UCIEngine:
    """Manages a single UCI engine process."""

    def __init__(self, path: str, name: str = ""):
        self.path = path
        self.name = name or os.path.basename(path)
        self.process: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()
        self._buffer = []

    def start(self, threads: int = 1):
        self.process = subprocess.Popen(
            [self.path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        # Start reader thread
        self._reader_thread = threading.Thread(target=self._reader, daemon=True)
        self._reader_thread.start()

        self._send("uci")
        self._wait_for_token("uciok", timeout=10)
        if threads > 1:
            self._send(f"setoption name Threads value {threads}")
        self._send("isready")
        self._wait_for_token("readyok", timeout=10)

    def _reader(self):
        """Background thread that reads all stdout lines into buffer."""
        try:
            while self.process and self.process.poll() is None:
                line = self.process.stdout.readline()
                if not line:
                    break
                with self._lock:
                    self._buffer.append(line.strip())
        except:
            pass

    def _drain_buffer(self) -> list:
        """Get all buffered lines."""
        with self._lock:
            lines = list(self._buffer)
            self._buffer.clear()
            return lines

    def _wait_for_token(self, token: str, timeout: float = 10.0) -> bool:
        start = time.time()
        while time.time() - start < timeout:
            lines = self._drain_buffer()
            for line in lines:
                if token in line:
                    return True
            time.sleep(0.005)
        return False

    def _send(self, cmd: str):
        if self.process and self.process.stdin:
            try:
                self.process.stdin.write(cmd + "\n")
                self.process.stdin.flush()
            except (OSError, BrokenPipeError):
                pass

    def stop(self):
        if self.process:
            try:
                self._send("quit")
            except Exception:
                pass
            try:
                self.process.wait(timeout=3)
            except (subprocess.TimeoutExpired, Exception):
                try:
                    self.process.kill()
                except Exception:
                    pass
            self.process = None

    def new_game(self):
        # Drain any leftover output
        self._drain_buffer()
        self._send("ucinewgame")
        self._send("isready")
        self._wait_for_token("readyok", timeout=5)

    def set_position(self, fen: str, moves: list = None):
        cmd = f"position fen {fen}"
        if moves:
            cmd += " moves " + " ".join(moves)
        self._send(cmd)

    def go(self, depth: int = 0, movetime: int = 0) -> tuple:
        """Send go and wait for bestmove. Returns (bestmove, score_cp)."""
        # Drain old output
        self._drain_buffer()

        cmd = "go"
        if depth > 0:
            cmd += f" depth {depth}"
        if movetime > 0:
            cmd += f" movetime {movetime}"
        self._send(cmd)

        # Wait for bestmove
        start = time.time()
        best_move = None
        score_cp = 0

        while time.time() - start < 60:
            lines = self._drain_buffer()
            for line in lines:
                if line.startswith("bestmove"):
                    parts = line.split()
                    best_move = parts[1] if len(parts) > 1 else "0000"
                elif line.startswith("info") and "score cp" in line:
                    parts = line.split()
                    try:
                        idx = parts.index("score")
                        score_cp = int(parts[idx + 2])
                    except (ValueError, IndexError):
                        pass
            if best_move is not None:
                break
            time.sleep(0.005)

        return best_move or "0000", score_cp


def play_game(engine1: UCIEngine, engine2: UCIEngine, fen: str,
              depth: int = 0, movetime: int = 0, max_moves: int = 300,
              engine1_color: str = "white") -> GameResult:
    import chess
    board = chess.Board(fen)

    engine1.new_game()
    engine2.new_game()

    white = engine1 if engine1_color == "white" else engine2
    black = engine2 if engine1_color == "white" else engine1

    moves = []
    termination = "normal"
    result_code = "1/2-1/2"
    winner = "draw"

    for _ in range(max_moves * 2):
        if board.is_game_over(claim_draw=True):
            break

        current_engine = white if board.turn == chess.WHITE else black
        current_name = "engine1" if current_engine == engine1 else "engine2"

        current_engine.set_position(fen, moves)
        move_str, score = current_engine.go(depth=depth, movetime=movetime)

        if not move_str or move_str == "0000":
            winner = "engine2" if current_name == "engine1" else "engine1"
            result_code = "0-1" if board.turn == chess.WHITE else "1-0"
            termination = f"illegal/null move by {current_name}"
            break

        try:
            m = chess.Move.from_uci(move_str)
            if m not in board.legal_moves:
                winner = "engine2" if current_name == "engine1" else "engine1"
                result_code = "0-1" if board.turn == chess.WHITE else "1-0"
                termination = f"illegal move: {move_str} by {current_name}"
                break
        except Exception:
            winner = "engine2" if current_name == "engine1" else "engine1"
            termination = f"invalid move string: {move_str}"
            break

        board.push(m)
        moves.append(move_str)
        print(f"{move_str} ", end="", flush=True)

    if board.is_checkmate():
        # Side whose turn it is has been checkmated
        if board.turn == chess.WHITE:
            winner = "engine1" if engine1_color == "black" else "engine2"
            result_code = "0-1"
        else:
            winner = "engine1" if engine1_color == "white" else "engine2"
            result_code = "1-0"
        termination = "checkmate"
    elif board.is_stalemate():
        winner = "draw"
        result_code = "1/2-1/2"
        termination = "stalemate"
    elif board.can_claim_threefold_repetition():
        winner = "draw"
        result_code = "1/2-1/2"
        termination = "threefold repetition"
    elif board.can_claim_fifty_moves():
        winner = "draw"
        result_code = "1/2-1/2"
        termination = "50-move rule"
    elif board.is_insufficient_material():
        winner = "draw"
        result_code = "1/2-1/2"
        termination = "insufficient material"
    elif len(moves) >= max_moves * 2:
        winner = "draw"
        result_code = "1/2-1/2"
        termination = "max moves"

    print()
    return GameResult(
        winner=winner,
        moves=moves,
        result_code=result_code,
        termination=termination,
        engine1_color=engine1_color,
    )


def run_match(args):
    print(f"Engine 1: {args.engine1}")
    print(f"Engine 2: {args.engine2}")
    print(f"Games: {args.games}")
    if args.depth > 0:
        print(f"Depth: {args.depth}")
    if args.movetime > 0:
        print(f"Time/move: {args.movetime}ms")
    if args.threads > 1:
        print(f"Threads: {args.threads}")
    print()

    eng1 = UCIEngine(args.engine1, "Engine1")
    eng2 = UCIEngine(args.engine2, "Engine2")

    try:
        print("Starting engines...", flush=True)
        eng1.start(threads=args.threads)
        eng2.start(threads=args.threads)
        print("Engines ready!\n", flush=True)
    except Exception as e:
        print(f"Error starting engines: {e}")
        return

    stats = MatchStats()

    for game_num in range(1, args.games + 1):
        fen = random.choice(START_POSITIONS)
        engine1_color = "white" if game_num % 2 == 1 else "black"

        print(f"Game {game_num}/{args.games} ", end="", flush=True)

        try:
            depth_val = 0 if args.movetime > 0 else args.depth
            result = play_game(
                eng1, eng2, fen,
                depth=depth_val,
                movetime=args.movetime,
                engine1_color=engine1_color,
            )

            stats.total_games += 1

            if result.winner == "engine1":
                stats.engine1_wins += 1
                if engine1_color == "white":
                    stats.engine1_white_wins += 1
                else:
                    stats.engine1_white_losses += 1
                marker = "W1"
            elif result.winner == "engine2":
                stats.engine2_wins += 1
                if engine1_color == "white":
                    stats.engine1_white_losses += 1
                marker = "W2"
            else:
                stats.draws += 1
                marker = "DD"

            print(f"{result.result_code} ({result.termination}) [{marker}] "
                  f"Moves: {len(result.moves)}")

            print(f"  Score: {stats.engine1_wins}W / {stats.draws}D / "
                  f"{stats.engine2_wins}L  "
                  f"({stats.win_rate():.1f}%)",
                  flush=True)

        except Exception as e:
            print(f"Error: {e}", flush=True)

    # Final results
    print("\n" + "=" * 60)
    print("  MATCH RESULTS")
    print("=" * 60)
    print(f"  Engine 1: {args.engine1}")
    print(f"  Engine 2: {args.engine2}")
    print(f"  Games played: {stats.total_games}")
    print(f"  Result: {stats.engine1_wins}W / {stats.draws}D / "
          f"{stats.engine2_wins}L")
    print(f"  Score: {stats.engine1_score} - {stats.engine2_score}")
    print(f"  Win Rate (Engine1): {stats.win_rate():.1f}%")
    print(f"  ELO Difference: {stats.elo_difference():+d}")
    if stats.engine1_white_wins + stats.engine1_white_losses > 0:
        w_rate = stats.engine1_white_wins / (stats.engine1_white_wins + stats.engine1_white_losses) * 100
        print(f"  As White: {stats.engine1_white_wins}W / "
              f"{stats.engine1_white_losses}L ({w_rate:.0f}%)")
    print("=" * 60, flush=True)

    eng1.stop()
    eng2.stop()


def main():
    parser = argparse.ArgumentParser(description="Chess Engine Match Runner")
    parser.add_argument("--engine1", "-e1", required=True)
    parser.add_argument("--engine2", "-e2", required=True)
    parser.add_argument("--games", "-g", type=int, default=20)
    parser.add_argument("--depth", "-d", type=int, default=8)
    parser.add_argument("--movetime", "-t", type=int, default=0,
                        help="Time per move in ms (0 = use depth)")
    parser.add_argument("--threads", type=int, default=1,
                        help="Threads per engine")
    parser.add_argument("--seed", "-s", type=int, default=None)
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    run_match(args)


if __name__ == "__main__":
    main()
