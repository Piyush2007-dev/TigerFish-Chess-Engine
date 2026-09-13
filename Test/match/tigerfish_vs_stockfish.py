#!/usr/bin/env python3
"""
TigerFish vs Stockfish — single game, 2 seconds per move, from startpos.
Usage: python tigerfish_vs_stockfish.py [time_per_move_sec]
"""
import subprocess, sys, time, threading

THREADS = int(sys.argv[2]) if len(sys.argv) > 2 else 16
SF_THREADS = int(sys.argv[3]) if len(sys.argv) > 3 else 1
TIME_SEC = int(sys.argv[1]) if len(sys.argv) > 1 else 2
TIME_MS = TIME_SEC * 1000

ROOT = __import__('pathlib').Path(__file__).resolve().parents[2]
SF_CANDIDATES = [
    ROOT / "tools" / "bin" / "stockfish.exe",
    r"D:\PROJECTS\stockfish\stockfish-windows-x86-64-avx2.exe"
]
SF_PATH = str(next((p for p in SF_CANDIDATES if __import__('os').path.exists(p)), SF_CANDIDATES[0]))
TF_CANDIDATES = [
    ROOT / "game.exe"
]
TF_PATH = str(next((p for p in TF_CANDIDATES if __import__('os').path.exists(p)), TF_CANDIDATES[0]))

class UCIEngine:
    def __init__(self, name, path, time_ms):
        self.name = name
        self.proc = subprocess.Popen(
            [path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1
        )
        self.time_ms = time_ms
        self.total_nodes = 0
        self.total_time_ms = 0
        self.last_depth = 0
        self._send("uci")
        self._wait("uciok")
        self._send(f"setoption name Threads value {THREADS}")
        self._send("isready")
        self._wait("readyok")

    def _send(self, cmd):
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def _wait(self, token, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.proc.stdout.readline().strip()
            if not line:
                time.sleep(0.01)
                continue
            if token in line:
                return line
            # Capture info lines
            if line.startswith("info"):
                self._parse_info(line)
        return None

    def _parse_info(self, line):
        parts = line.split()
        try:
            di = parts.index("depth")
            self.last_depth = int(parts[di + 1])
        except (ValueError, IndexError):
            pass
        try:
            ni = parts.index("nodes")
            self.total_nodes = int(parts[ni + 1])
        except (ValueError, IndexError):
            pass
        try:
            ti = parts.index("time")
            self.total_time_ms = int(parts[ti + 1])
        except (ValueError, IndexError):
            pass

    def newgame(self):
        self._send("ucinewgame")
        self._send("isready")
        self._wait("readyok")

    def set_position(self, fen, moves=None):
        if fen == "startpos":
            cmd = "position startpos"
        else:
            cmd = f"position fen {fen}"
        if moves:
            cmd += " moves " + " ".join(moves)
        self._send(cmd)

    def go(self):
        self._send(f"go movetime {self.time_ms}")
        # Wait for bestmove
        deadline = time.time() + 10
        while time.time() < deadline:
            line = self.proc.stdout.readline().strip()
            if not line:
                time.sleep(0.01)
                continue
            if line.startswith("info"):
                self._parse_info(line)
            if line.startswith("bestmove"):
                move = line.split()[1] if len(line.split()) > 1 else "0000"
                return move
        return "0000"

    def quit(self):
        try:
            self._send("quit")
            self.proc.wait(timeout=3)
        except:
            self.proc.kill()

def uci_move_to_san(board, uci_move):
    """Convert UCI move to algebraic-ish for display."""
    if len(uci_move) < 4:
        return uci_move
    return uci_move[:2] + "-" + uci_move[2:4] + (uci_move[4:] if len(uci_move) > 4 else "")

def is_checkmate_hint(line):
    """Check if info line mentions mate."""
    return "mate" in line

def main():
    print(f"=== TigerFish vs Stockfish ===")
    print(f"Time per move: {TIME_SEC}s | Engine paths:")
    print(f"  TigerFish: {TF_PATH}")
    print(f"  Stockfish: {SF_PATH}")
    print()

    tf = UCIEngine("TigerFish", TF_PATH, TIME_MS)
    sf = UCIEngine("Stockfish", SF_PATH, TIME_MS)

    # Force Stockfish to 1 thread, 64MB hash for fair comparison
    sf._send(f"setoption name Threads value {SF_THREADS}")
    sf._send("setoption name Hash value 64")
    sf._send("isready")
    sf._wait("readyok")

    tf._send(f"setoption name Threads value {THREADS}")
    time.sleep(0.1)
    tf._send("isready")
    tf._wait("readyok")
    tf.newgame()
    sf.newgame()

    moves_uci = []
    half_moves = 0
    max_half = 200

    tf_nodes_total = 0
    sf_nodes_total = 0
    tf_time_total = 0
    sf_time_total = 0

    print("Starting game...\n")
    start = time.time()

    while half_moves < max_half:
        # Check 3-fold repetition (simple move count)
        if half_moves >= 6 and moves_uci[-6:] == moves_uci[-12:-6]:
            print("\n>>> 3-fold repetition by move count")
            break

        # 50-move rule
        # We'd need halfmove clock tracking, skip for now

        is_white = (half_moves % 2 == 0)
        engine = tf if is_white else sf
        label = f"{'White (TF)' if is_white else 'Black (SF)'}"

        # Set position for both engines
        tf.set_position("startpos", moves_uci)
        sf.set_position("startpos", moves_uci)

        # Reset per-move stats
        engine.total_nodes = 0
        engine.total_time_ms = 0
        engine.last_depth = 0

        move_start = time.time()
        best_move = engine.go()
        move_time = (time.time() - move_start) * 1000

        if best_move == "0000" or best_move == "(none)":
            print(f"\n>>> {label} resigned or no legal moves (bestmove={best_move})")
            break

        moves_uci.append(best_move)
        half_moves += 1

        if is_white:
            tf_nodes_total += engine.total_nodes
            tf_time_total += max(move_time, 1)
        else:
            sf_nodes_total += engine.total_nodes
            sf_time_total += max(move_time, 1)

        san = uci_move_to_san(None, best_move)
        nodes = engine.total_nodes
        depth = engine.last_depth
        tm = int(move_time)

        # Check for mate score
        mate_info = ""
        if engine.last_depth > 0 and depth >= 15:
            mate_info = " *"  # might be near mate

        print(f"{half_moves:3d}. {label:12s} {san:8s} "
              f"[{nodes:>10,} nodes, {tm:>5}ms] depth={depth}{mate_info}", flush=True)

    elapsed = time.time() - start
    print(f"\n{'='*50}")
    print(f"RESULT: {len(moves_uci)} half-moves in {elapsed:.1f}s")
    print(f"TigerFish total: {tf_nodes_total:,} nodes, {tf_time_total/1000:.1f}s")
    print(f"Stockfish total: {sf_nodes_total:,} nodes, {sf_time_total/1000:.1f}s")
    if tf_time_total > 0:
        print(f"TigerFish NPS: {tf_nodes_total * 1000 // tf_time_total:,}")
    if sf_time_total > 0:
        print(f"Stockfish NPS: {sf_nodes_total * 1000 // sf_time_total:,}")
    print(f"Move sequence: {' '.join(moves_uci)}")
    print(f"{'='*50}")

    tf.quit()
    sf.quit()

if __name__ == "__main__":
    main()
