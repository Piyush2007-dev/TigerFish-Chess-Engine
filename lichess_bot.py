#!/usr/bin/env python3
"""
TigerFish Lichess Single-Game Bot Bridge
========================================
A high-performance asynchronous bot bridge for Lichess using asyncio,
aiohttp, and the persistent compiled TigerFish C++ engine executable (`game.exe`).

Features:
- Connects to Lichess API via stream events.
- Dynamically fetches account info to avoid hardcoded bot username bugs.
- Uses `game.exe interactive` session with persistent 32 MB Transposition Table.
- Auto-accepts standard challenges.
- Graceful HTTP health check server.
- Supports configurable search depth (default 7).
"""

import argparse
import asyncio
import json
import logging
import os
import sys
import time
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

import aiohttp

LICHESS_API = "https://lichess.org/api"
DEFAULT_ENGINE_PATH = "./game.exe" if os.name == "nt" else "./game"
START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("TigerFish-Bot")

# ── Lightweight Health Check HTTP Server ──────────────────────────────
class HealthCheckHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "text/plain")
        self.end_headers()
        self.wfile.write(b"TigerFish Bot Online")

    def log_message(self, format, *args):
        pass  # Suppress HTTP access logs

def start_health_server():
    port = int(os.getenv("PORT", 10000))
    try:
        server = HTTPServer(("0.0.0.0", port), HealthCheckHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        logger.info(f"Health check HTTP server active on port {port}")
    except Exception as e:
        logger.warning(f"Health check server could not start on port {port}: {e}")

# ── Per-Game Persistent Engine Subprocess ──────────────────────────────
class GameEngineProcess:
    """Manages a single persistent `game.exe interactive` subprocess with TT."""

    def __init__(self, engine_path: str, depth: int = 7):
        self.engine_path = engine_path
        self.depth = depth
        self._proc: asyncio.subprocess.Process = None

    async def start(self, initial_fen: str):
        """Spawn the subprocess and initialise the board."""
        self._proc = await asyncio.create_subprocess_exec(
            self.engine_path, "interactive",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        await self._send(f"newgame {initial_fen}")

    async def _send(self, command: str) -> dict:
        """Send one command line and read JSON response until ===READY===."""
        if self._proc is None or self._proc.returncode is not None:
            return {"error": "process not running"}
        self._proc.stdin.write((command + "\n").encode())
        await self._proc.stdin.drain()

        lines = []
        while True:
            raw = await self._proc.stdout.readline()
            if not raw:
                break
            text = raw.decode("utf-8", errors="ignore").strip()
            if text == "===READY===":
                break
            if text:
                lines.append(text)

        # Parse JSON response
        for line in reversed(lines):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
        return {}

    async def apply_move(self, uci_move: str) -> dict:
        """Apply opponent's move to the persistent board."""
        return await self._send(f"apply {uci_move}")

    async def best_move(self) -> dict:
        """Search from current position. Returns dict with 'best_move', 'fen', 'status'."""
        return await self._send(f"best {self.depth}")

    async def stop(self):
        """Gracefully terminate the engine subprocess."""
        if self._proc and self._proc.returncode is None:
            try:
                self._proc.stdin.write(b"quit\n")
                await self._proc.stdin.drain()
                await asyncio.wait_for(self._proc.wait(), timeout=3.0)
            except Exception:
                self._proc.kill()

# ── Lichess Bot Bridge Manager ─────────────────────────────────────────
class LichessBot:
    def __init__(self, token: str, engine_path: str, search_depth: int = 7):
        self.token = token
        self.headers = {
            "Authorization": f"Bearer {token}",
            "User-Agent": "TigerFish-Chess-Engine/1.0",
        }
        self.engine_path = engine_path
        self.search_depth = search_depth
        self.bot_username = ""
        self.session: aiohttp.ClientSession = None

        if not os.path.exists(self.engine_path):
            raise FileNotFoundError(f"Engine executable not found at: {self.engine_path}")

    async def start(self):
        start_health_server()
        connector = aiohttp.TCPConnector(limit=10)
        async with aiohttp.ClientSession(headers=self.headers, connector=connector) as session:
            self.session = session
            await self.upgrade_account()
            await self.fetch_bot_username()

            logger.info("=" * 65)
            logger.info(f" TigerFish Bot Online! Username: {self.bot_username}")
            logger.info(f" Search Depth: {self.search_depth} | Engine: {self.engine_path}")
            logger.info(" Listening for game challenges on Lichess... (Ctrl+C to stop)")
            logger.info("=" * 65)

            await self.listen_event_stream()

    async def upgrade_account(self):
        logger.info("Checking / upgrading account to official BOT status...")
        async with self.session.post(f"{LICHESS_API}/bot/account/upgrade") as resp:
            if resp.status == 200:
                logger.info("Account upgraded to BOT status.")
            else:
                body = await resp.text()
                logger.info(f"Account upgrade check status {resp.status}: {body[:100]}")

    async def fetch_bot_username(self):
        async with self.session.get(f"{LICHESS_API}/account") as resp:
            if resp.status == 200:
                data = await resp.json()
                self.bot_username = data.get("username", "TigerFish-BOT")
            else:
                logger.error(f"Failed to fetch account info: status {resp.status}")
                self.bot_username = "TigerFish-BOT"

    async def listen_event_stream(self):
        url = f"{LICHESS_API}/stream/event"
        backoff = 2
        while True:
            try:
                logger.info("Connecting to Lichess Event Stream...")
                async with self.session.get(url, timeout=None) as resp:
                    if resp.status != 200:
                        logger.error(f"Event stream HTTP {resp.status}, retrying in {backoff}s...")
                        await asyncio.sleep(backoff)
                        backoff = min(backoff * 2, 60)
                        continue

                    backoff = 2
                    async for line in resp.content:
                        if not line:
                            continue
                        line_str = line.decode("utf-8").strip()
                        if not line_str:
                            continue
                        try:
                            event = json.loads(line_str)
                            await self.handle_event(event)
                        except json.JSONDecodeError:
                            continue
            except asyncio.CancelledError:
                logger.info("Event stream listener stopped.")
                break
            except Exception as e:
                logger.error(f"Event stream connection error: {e}. Reconnecting in {backoff}s...")
                await asyncio.sleep(backoff)
                backoff = min(backoff * 2, 60)

    async def handle_event(self, event: dict):
        event_type = event.get("type")

        if event_type == "challenge":
            challenge = event.get("challenge", {})
            challenge_id = challenge.get("id")
            challenger = challenge.get("challenger", {}).get("name", "Unknown")
            variant = challenge.get("variant", {}).get("key", "standard")

            if challenger.lower() == self.bot_username.lower():
                return

            if variant != "standard":
                logger.info(f"[Challenge Declined] {challenge_id} from {challenger} (non-standard variant '{variant}')")
                await self.decline_challenge(challenge_id, "variant")
            else:
                logger.info(f"[Challenge Accepted] {challenge_id} vs {challenger}")
                await self.accept_challenge(challenge_id)

        elif event_type == "gameStart":
            game = event.get("game", {})
            game_id = game.get("id")
            if game_id:
                logger.info(f"[Game Started] https://lichess.org/{game_id}")
                asyncio.create_task(self.play_game(game_id))

    async def accept_challenge(self, challenge_id: str):
        url = f"{LICHESS_API}/challenge/{challenge_id}/accept"
        try:
            async with self.session.post(url) as resp:
                if resp.status == 200:
                    logger.info(f"Successfully accepted challenge {challenge_id}")
                else:
                    body = await resp.text()
                    logger.warning(f"Failed to accept challenge {challenge_id} (HTTP {resp.status}): {body[:100]}")
        except Exception as e:
            logger.error(f"Error accepting challenge {challenge_id}: {e}")

    async def decline_challenge(self, challenge_id: str, reason: str):
        url = f"{LICHESS_API}/challenge/{challenge_id}/decline"
        try:
            async with self.session.post(url, data={"reason": reason}) as resp:
                pass
        except Exception as e:
            logger.error(f"Error declining challenge {challenge_id}: {e}")

    async def play_game(self, game_id: str):
        url = f"{LICHESS_API}/bot/game/stream/{game_id}"
        my_color = None
        initial_fen = START_FEN
        processed_moves_count = 0
        engine_proc = GameEngineProcess(self.engine_path, depth=self.search_depth)

        try:
            async with self.session.get(url, timeout=None) as resp:
                if resp.status != 200:
                    logger.error(f"[{game_id}] Stream HTTP error {resp.status}")
                    return

                async for line in resp.content:
                    if not line:
                        continue
                    line_str = line.decode("utf-8").strip()
                    if not line_str:
                        continue

                    try:
                        msg = json.loads(line_str)
                    except json.JSONDecodeError:
                        continue

                    msg_type = msg.get("type")

                    if msg_type == "gameFull":
                        white_user = msg.get("white", {}).get("name", "")
                        my_color = "white" if white_user.lower() == self.bot_username.lower() else "black"
                        raw_fen = msg.get("initialFen", "startpos")
                        initial_fen = START_FEN if (not raw_fen or raw_fen == "startpos") else raw_fen

                        await engine_proc.start(initial_fen)
                        logger.info(f"[{game_id}] Game initialized | Playing as {my_color.upper()}")

                        state = msg.get("state", {})
                        new_count = await self.process_game_state(
                            game_id, state, my_color, engine_proc, processed_moves_count
                        )
                        if new_count is not None:
                            processed_moves_count = new_count

                    elif msg_type == "gameState":
                        if my_color is None:
                            continue
                        status = msg.get("status", "")
                        if status in ["mate", "resign", "outoftime", "stalemate",
                                      "draw", "noStart", "cheat", "variantEnd", "aborted"]:
                            self._log_game_result(game_id, msg, my_color)
                            break

                        new_count = await self.process_game_state(
                            game_id, msg, my_color, engine_proc, processed_moves_count
                        )
                        if new_count is not None:
                            processed_moves_count = new_count

        except asyncio.CancelledError:
            logger.info(f"[{game_id}] Game stream cancelled.")
        except Exception as e:
            logger.error(f"[{game_id}] Game stream exception: {e}")
        finally:
            await engine_proc.stop()

    def _log_game_result(self, game_id: str, state: dict, my_color: str):
        status = state.get("status", "unknown")
        winner = state.get("winner", "")

        STATUS_LABELS = {
            "mate":       "Checkmate",
            "resign":     "Resignation",
            "outoftime":  "Time forfeit",
            "stalemate":  "Stalemate (draw)",
            "draw":       "Draw (agreed/repetition)",
            "aborted":    "Game aborted",
            "noStart":    "No start",
            "cheat":      "Cheat detected",
        }
        reason = STATUS_LABELS.get(status, status)

        if not winner:
            result_str = f"Draw — {reason}"
        elif winner == my_color:
            result_str = f"WIN 🏆 — {reason}"
        else:
            result_str = f"LOSS — {reason} (opponent won)"

        logger.info(f"[{game_id}] Game over: {result_str}")

    async def process_game_state(
        self, game_id: str, state: dict,
        my_color: str, engine_proc: GameEngineProcess,
        processed_moves_count: int
    ):
        status = state.get("status", "")
        if status in ["mate", "resign", "outoftime", "stalemate",
                      "draw", "noStart", "cheat", "variantEnd", "aborted"]:
            self._log_game_result(game_id, state, my_color)
            return None

        moves_str = state.get("moves", "").strip()
        moves_list = moves_str.split() if moves_str else []
        total_moves = len(moves_list)

        is_white_turn = (total_moves % 2 == 0)
        is_my_turn = (is_white_turn and my_color == "white") or (not is_white_turn and my_color == "black")

        if not is_my_turn:
            return None

        if total_moves < processed_moves_count:
            return None

        start_time = time.time()
        logger.info(f"[{game_id}] TigerFish thinking at depth {self.search_depth} (move #{total_moves + 1})...")

        new_moves = moves_list[processed_moves_count:]
        for move in new_moves:
            res = await engine_proc.apply_move(move)
            if "error" in res:
                logger.error(f"[{game_id}] apply {move} failed: {res['error']}")
                return None

        res = await engine_proc.best_move()
        elapsed_ms = int((time.time() - start_time) * 1000)

        bot_move = res.get("best_move", "")
        if bot_move:
            logger.info(f"[{game_id}] TigerFish plays: {bot_move} ({elapsed_ms}ms)")
            await self.send_move(game_id, bot_move)
            return total_moves + 1
        else:
            logger.warning(f"[{game_id}] Engine produced no move")
            return None

    async def send_move(self, game_id: str, move_uci: str):
        url = f"{LICHESS_API}/bot/game/{game_id}/move/{move_uci}"
        try:
            async with self.session.post(url) as resp:
                if resp.status != 200:
                    body = await resp.text()
                    logger.warning(f"[{game_id}] Move submission {move_uci} failed (HTTP {resp.status}): {body[:100]}")
        except Exception as e:
            logger.error(f"[{game_id}] Error posting move {move_uci}: {e}")

# ── Environment Loading ───────────────────────────────────────────────
def load_env_file(filepath=".env"):
    if os.path.exists(filepath):
        with open(filepath, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, val = line.split("=", 1)
                    key = key.strip()
                    val = val.strip().strip('"').strip("'")
                    os.environ[key] = val

# ── Main Entry Point ──────────────────────────────────────────────────
def main():
    load_env_file()
    parser = argparse.ArgumentParser(description="TigerFish Lichess Bot Bridge")
    parser.add_argument("--token", type=str, help="Lichess API Token", default=os.getenv("LICHESS_TOKEN"))
    parser.add_argument("--engine", type=str, help="Path to game.exe", default=DEFAULT_ENGINE_PATH)
    parser.add_argument("--depth", type=int, help="Engine search depth", default=7)
    args = parser.parse_args()

    token = args.token
    if not token:
        logger.error("No Lichess API Token found!")
        logger.error("Please add your token to the .env file as LICHESS_TOKEN=lip_xxxxxxxxx")
        sys.exit(1)

    bot = LichessBot(token=token, engine_path=args.engine, search_depth=args.depth)

    try:
        asyncio.run(bot.start())
    except KeyboardInterrupt:
        logger.info("TigerFish Bot shutdown requested by user.")

if __name__ == "__main__":
    main()
