#!/usr/bin/env python3
"""
TigerFish Lichess Parallel Multi-Bot (10 Concurrent Games @ Depth 7)
===================================================================
A high-performance asynchronous bot bridge for Lichess using asyncio,
aiohttp, and the compiled TigerFish C++ engine executable (`game.exe`).

Features:
- Handles up to 10 parallel games concurrently.
- Enforces depth 7 search on all engine evaluations.
- Auto-accepts standard challenges across all time controls.
- Uses `game.exe apply` + `game.exe best` for clean FEN tracking.
- Graceful shutdown via 'q' + Enter or Ctrl+C (finishes active games).
- Clean terminal status reporting and HTTP health check server.
"""

import argparse
import asyncio
import json
import logging
import os
import random
import sys
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer
import threading
import subprocess

import aiohttp

# ── Configuration & Defaults ──────────────────────────────────────────
LICHESS_API = "https://lichess.org/api"
DEFAULT_ENGINE_PATH = "./game.exe" if os.name == "nt" else "./game"
START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)
    except Exception:
        pass

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
    stream=sys.stdout,
    force=True,
)
logger = logging.getLogger("TigerFish-MultiBot")

# ── Lightweight Health Check HTTP Server ──────────────────────────────
class HealthCheckHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "text/plain")
        self.end_headers()
        self.wfile.write(b"TigerFish MultiBot Online")

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

def calculate_search_time(state: dict, my_color: str, default_time_ms: int = 2000) -> int:
    """
    Calculate optimal search time budget in milliseconds based on remaining clock and increment.
    Protects against flagging with adaptive panic caps and network latency buffers.
    """
    if not state:
        return default_time_ms

    time_key = "wtime" if my_color == "white" else "btime"
    inc_key = "winc" if my_color == "white" else "binc"

    remaining = state.get(time_key)
    inc = state.get(inc_key, 0) or 0

    if remaining is None or remaining <= 0:
        return default_time_ms

    # Network latency safety buffer (100ms for HTTP transmission round-trip)
    lag_buffer = 100
    safe_remaining = max(50, remaining - lag_buffer)

    # Base target allocation: 1/25th of remaining time + 75% of increment
    allocated = (safe_remaining / 25.0) + (inc * 0.75)

    # Adaptive upper bound caps:
    if safe_remaining < 1500:
        # Emergency scramble: spend at most 30% of remaining to never flag
        allocated = min(allocated, safe_remaining * 0.30)
    elif safe_remaining < 5000:
        # Low time: spend at most 25% of remaining
        allocated = min(allocated, safe_remaining * 0.25)
    else:
        # Standard time: cap at 20% of remaining
        allocated = min(allocated, safe_remaining * 0.20)

    # Minimum threshold: at least 50ms so engine completes shallow iterations
    allocated = max(50, min(allocated, safe_remaining))

    return int(allocated)

# ── Per-Game Engine Process ────────────────────────────────────────────
# One `game.exe interactive` process lives for the entire game.
# The 128MB TT inside it accumulates knowledge across all moves.
class GameEngineProcess:
    """Manages a single persistent `game.exe interactive` subprocess."""

    def __init__(self, engine_path: str, depth: int = 12):
        self.engine_path = engine_path
        self.depth = depth
        self._proc: asyncio.subprocess.Process = None

    async def start(self, initial_fen: str):
        """Spawn the subprocess and initialise the board."""
        # On Windows, put the engine in a new process group so it doesn't instantly die on Ctrl+C.
        # This allows the Python script to finish ongoing games during graceful shutdown.
        kwargs = {}
        if sys.platform == "win32":
            kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
            
        self._proc = await asyncio.create_subprocess_exec(
            self.engine_path, "interactive",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            **kwargs
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

        # Last non-empty line should be the JSON payload
        for line in reversed(lines):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
        return {}

    async def apply_move(self, uci_move: str) -> dict:
        """Apply opponent's move to the persistent board."""
        return await self._send(f"apply {uci_move}")

    async def best_move(self, depth: int = None, time_ms: int = None) -> dict:
        """Search from current position with depth and optional time limit in ms."""
        d = depth if depth is not None else self.depth
        if time_ms is not None and time_ms > 0:
            return await self._send(f"best {d} {int(time_ms)}")
        return await self._send(f"best {d}")

    async def stop(self):
        """Gracefully terminate the engine subprocess."""
        if self._proc and self._proc.returncode is None:
            try:
                self._proc.stdin.write(b"quit\n")
                await self._proc.stdin.drain()
                await asyncio.wait_for(self._proc.wait(), timeout=3.0)
            except Exception:
                self._proc.kill()

# ── Shared engine config (path + depth, no TT) ────────────────────────
class EngineConfig:
    """Lightweight config used to spawn per-game GameEngineProcess instances."""
    def __init__(self, engine_path: str, default_depth: int = 12):
        self.engine_path = engine_path
        self.default_depth = default_depth
        
    def new_game_process(self, speed: str, clock: dict = None) -> "GameEngineProcess":
        speed_depths = {
            "ultraBullet": 8,
            "bullet": 10,
            "blitz": 12,
            "rapid": 14,
            "classical": 16
        }
        depth = speed_depths.get(speed.lower(), self.default_depth)
        return GameEngineProcess(self.engine_path, depth)


# ── Lichess Multi-Bot Manager ──────────────────────────────────────────
class LichessMultiBot:
    def __init__(self, token: str, engine_path: str, max_games: int = 16, search_depth: int = 7, auto_challenge: bool = True):
        self.token = token
        self.headers = {
            "Authorization": f"Bearer {token}",
            "User-Agent": "TigerFish-Chess-Engine/1.0",
        }
        self.engine_cfg = EngineConfig(engine_path, default_depth=search_depth)
        self.max_games = max_games
        self.search_depth = search_depth
        self.auto_challenge = auto_challenge

        self.bot_username = ""
        self.bot_ratings = {}
        self.active_games = set()
        self.active_tasks = {}
        self.recently_challenged = {}  # {username: timestamp}
        self.pending_challenge_ids = set()  # outgoing challenge IDs we sent (not yet accepted/declined)
        self.bot_blacklist = set()  # bots that permanently refuse bot-vs-bot challenges
        self.challenge_id_to_target = {}  # {challenge_id: target_username} for tracking declines
        self.challenge_id_to_tc = {}  # {challenge_id: tc_tuple} so we know which TC was rejected
        self.bot_tc_rejections = {}  # {username: set of (limit, increment) tuples rejected by that bot}
        # Time controls to challenge with — randomly sampled each time
        self.time_controls = [
            # Bullet (less likely)
            ("60",   "0",  "1+0 bullet"),
            ("60",   "1",  "1+1 bullet"),
            ("120",  "0",  "2+0 bullet"),
            ("120",  "1",  "2+1 bullet"),
            
            # Blitz (less likely)
            ("180",  "0",  "3+0 blitz"),
            ("180",  "2",  "3+2 blitz"),
            ("300",  "0",  "5+0 blitz"),
            ("300",  "3",  "5+3 blitz"),
            ("300",  "5",  "5+5 blitz"),
            ("480",  "0",  "8+0 blitz"),
            
            # Rapid (Heavy Weight - 3x)
            ("600",  "0",  "10+0 rapid"),
            ("600",  "0",  "10+0 rapid"),
            ("600",  "0",  "10+0 rapid"),
            ("600",  "5",  "10+5 rapid"),
            ("600",  "5",  "10+5 rapid"),
            ("600",  "5",  "10+5 rapid"),
            ("900",  "10", "15+10 rapid"),
            ("900",  "10", "15+10 rapid"),
            ("900",  "10", "15+10 rapid"),
            ("1200", "0",  "20+0 rapid"),
            ("1200", "0",  "20+0 rapid"),
            ("1200", "0",  "20+0 rapid"),
            
            # Classical (Massive Weight - 6x)
            ("1800", "0",  "30+0 classical"),
            ("1800", "0",  "30+0 classical"),
            ("1800", "0",  "30+0 classical"),
            ("1800", "0",  "30+0 classical"),
            ("1800", "0",  "30+0 classical"),
            ("1800", "0",  "30+0 classical"),
            
            ("1800", "20", "30+20 classical"),
            ("1800", "20", "30+20 classical"),
            ("1800", "20", "30+20 classical"),
            ("1800", "20", "30+20 classical"),
            ("1800", "20", "30+20 classical"),
            ("1800", "20", "30+20 classical"),
            
            ("2700", "45", "45+45 classical"),
            ("2700", "45", "45+45 classical"),
            ("2700", "45", "45+45 classical"),
            ("2700", "45", "45+45 classical"),
            ("2700", "45", "45+45 classical"),
            ("2700", "45", "45+45 classical"),
            
            ("3600", "0",  "60+0 classical"),
            ("3600", "0",  "60+0 classical"),
            ("3600", "0",  "60+0 classical"),
            ("3600", "0",  "60+0 classical"),
            ("3600", "0",  "60+0 classical"),
            ("3600", "0",  "60+0 classical"),
        ]
        self.tc_index = 0  # fallback index for sequential rotation on declines
        self.shutdown_event = asyncio.Event()  # set to trigger graceful shutdown
        self.lock = asyncio.Lock()
        self.session: aiohttp.ClientSession = None

    async def start(self):
        # Run startup preflight checks & display search/clock constants in terminal
        try:
            from test_bot_startup import run_startup_test
            run_startup_test(self.engine_path)
        except Exception as e:
            logger.warning(f"Startup preflight test encountered error: {e}")

        start_health_server()
        # Use a read timeout so stalled TCP connections (Lichess server silent gaps)
        # are detected and trigger a reconnect rather than freezing the bot forever.
        timeout = aiohttp.ClientTimeout(
            total=None,
            connect=30,
            sock_read=90,   # 90 s — Lichess sends a keepalive newline every ~60 s
        )
        connector = aiohttp.TCPConnector(limit=100)
        async with aiohttp.ClientSession(
            headers=self.headers,
            connector=connector,
            timeout=timeout,
        ) as session:
            self.session = session
            await self.upgrade_account()
            await self.fetch_bot_username()

            logger.info("=" * 65)
            logger.info(f" TigerFish MultiBot Online! Username: {self.bot_username}")
            logger.info(f" Max Concurrency: {self.max_games} games | Depth: {self.search_depth}")
            logger.info(f" Auto-Challenge Bot Seeker: {'ENABLED' if self.auto_challenge else 'DISABLED'}")
            logger.info(" Listening for game challenges on Lichess...")
            logger.info("=" * 65)

            tasks = [
                asyncio.create_task(self.listen_event_stream(), name="event-stream"),
                asyncio.create_task(self.watch_stdin(), name="stdin-watcher"),
            ]
            if self.auto_challenge:
                tasks.append(asyncio.create_task(self.auto_challenge_loop(), name="auto-challenge"))

            # Run until shutdown is requested (or cancelled by Ctrl+C)
            try:
                await self.shutdown_event.wait()
            except asyncio.CancelledError:
                logger.info("[Shutdown] Interrupt received — initiating graceful shutdown...")
                self.shutdown_event.set()

            logger.info("=" * 65)
            logger.info(" Graceful shutdown initiated — stopping new challenges.")
            logger.info(f" Waiting for {len(self.active_games)} active game(s) to finish...")
            logger.info("=" * 65)

            # Cancel background tasks (event stream, auto-challenge, stdin watcher)
            for t in tasks:
                t.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)

            # Wait for all active game tasks to complete
            async with self.lock:
                game_tasks = list(self.active_tasks.values())
            if game_tasks:
                await asyncio.gather(*game_tasks, return_exceptions=True)

            logger.info("All games finished. TigerFish MultiBot shut down cleanly.")

    async def watch_stdin(self):
        """Watches stdin for 'q' to trigger graceful shutdown.
        On cloud/server environments there is no TTY so stdin.readline() raises
        immediately — we catch that and just park the task silently so the bot
        keeps running.
        """
        # Detect no-TTY environments (Render, Railway, Docker, etc.)
        if not sys.stdin or not hasattr(sys.stdin, 'fileno'):
            logger.info("[Stdin watcher] No TTY detected — running in server mode (Ctrl+C to stop).")
            try:
                await self.shutdown_event.wait()
            except asyncio.CancelledError:
                pass
            return
        try:
            import os as _os
            _os.get_terminal_size()   # Raises OSError if no real terminal
        except OSError:
            logger.info("[Stdin watcher] No TTY detected — running in server mode (Ctrl+C to stop).")
            try:
                await self.shutdown_event.wait()
            except asyncio.CancelledError:
                pass
            return

        loop = asyncio.get_event_loop()
        try:
            while not self.shutdown_event.is_set():
                line = await loop.run_in_executor(None, sys.stdin.readline)
                if not line or line.strip().lower() == "q":
                    logger.info("[Shutdown] 'q' received — initiating graceful shutdown...")
                    self.shutdown_event.set()
                    break
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.debug(f"[Stdin watcher] {e}")

    async def auto_challenge_loop(self):
        logger.info("Auto-Challenge Seeker started. Will continuously seek matches against online bots...")
        await asyncio.sleep(5)  # Initial startup delay

        while True:
            try:
                async with self.lock:
                    current_count = len(self.active_games)
                
                needed = self.max_games - current_count
                if needed > 0:
                    # Fetch all currently online bots
                    async with self.session.get(f"{LICHESS_API}/bot/online") as resp:
                        if resp.status == 200:
                            text = await resp.text()
                            online_bots = [json.loads(line) for line in text.split('\n') if line.strip()]
                            
                            now = time.time()
                            # Clean up old challenge timestamps (> 10 mins ago)
                            self.recently_challenged = {u: t for u, t in self.recently_challenged.items() if now - t < 600}
                            
                            # Filter bots starting from the bottom of the online list (least busy bots first)
                            async with self.lock:
                                blacklist_snap = set(self.bot_blacklist)
                                # Also skip bots where all TCs have been rejected
                                tc_rejections_snap = dict(self.bot_tc_rejections)
                            candidates = [
                                b for b in reversed(online_bots)
                                if b.get("username")
                                and b.get("username").lower() != self.bot_username.lower()
                                and b.get("username") not in self.recently_challenged
                                and b.get("username") not in blacklist_snap
                                and len(tc_rejections_snap.get(b.get("username"), set())) < len(self.time_controls)
                            ]

                            # Only issue 1 or 2 challenges per cycle to avoid flooding Lichess API
                            max_to_issue = min(needed, 2)
                            issued = 0

                            for target_bot in candidates:
                                if issued >= max_to_issue:
                                    break
                                
                                target = target_bot.get("username")
                                logger.info(f"[Auto-Seeker] Issuing challenge to online bot '{target}'...")
                                self.recently_challenged[target] = now
                                status_code, challenge_success = await self.issue_challenge(target_bot)

                                if status_code == 429:
                                    logger.warning("[Auto-Seeker] Lichess API Rate Limit (429) hit! Backing off for 60 seconds...")
                                    await asyncio.sleep(60)
                                    break  # Stop trying candidates in this cycle

                                if challenge_success:
                                    issued += 1
                                    await asyncio.sleep(6)  # Space out challenge API requests safely
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"[Auto-Seeker Error] {e}")

            await asyncio.sleep(20)  # Check every 20 seconds

    async def issue_challenge(self, target_bot: dict) -> tuple[int, bool]:
        target_username = target_bot.get("username")
        url = f"{LICHESS_API}/challenge/{target_username}"
        # Pick a random TC, excluding any this bot has already rejected or are outside rating range
        async with self.lock:
            rejected = self.bot_tc_rejections.get(target_username, set())
            
        available = []
        for tc in self.time_controls:
            if (tc[0], tc[1]) in rejected:
                continue
                
            category = tc[2].split()[-1]
            my_rating = self.bot_ratings.get(category, 1500)
            their_rating = target_bot.get("perfs", {}).get(category, {}).get("rating")
            
            if their_rating is None:
                their_rating = 1500
                
            if abs(my_rating - their_rating) > 300:
                continue
                
            available.append(tc)
            
        if not available:
            logger.info(f"[Auto-Seeker] No valid TCs (rejected or rating out of bounds) for '{target_username}', skipping.")
            async with self.lock:
                self.recently_challenged[target_username] = time.time()  # cool off
            return 200, False
            
        tc = random.choice(available)
        
        data = {
            "rated": "true",
            "clock.limit": tc[0],
            "clock.increment": tc[1],
            "color": "random",
            "variant": "standard"
        }
        
        tc_label = tc[2]
        try:
            async with self.session.post(url, data=data) as resp:
                if resp.status == 200:
                    res_json = await resp.json()
                    c_id = res_json.get("id") or res_json.get("challenge", {}).get("id")
                    if c_id:
                        async with self.lock:
                            self.pending_challenge_ids.add(c_id)
                            self.challenge_id_to_target[c_id] = target_username
                            self.challenge_id_to_tc[c_id] = tc
                    logger.info(f"[Auto-Seeker] Challenge ({tc_label}) sent to '{target_username}' (ID: {c_id})")
                    return resp.status, True
                elif resp.status == 429:
                    return 429, False
                else:
                    body = await resp.text()
                    logger.warning(f"[Auto-Seeker] Challenge to '{target_username}' status {resp.status}: {body[:100]}")
                    return resp.status, False
        except Exception as e:
            logger.error(f"[Auto-Seeker] Failed to challenge '{target_username}': {e}")
            return 500, False

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
                self.bot_ratings = {
                    k: v.get("rating", 1500)
                    for k, v in data.get("perfs", {}).items()
                    if isinstance(v, dict) and "rating" in v
                }
            else:
                logger.error(f"Failed to fetch account info: status {resp.status}")
                self.bot_username = "TigerFish-BOT"
                self.bot_ratings = {}

    async def listen_event_stream(self):
        url = f"{LICHESS_API}/stream/event"
        # Override the session-level sock_read for this stream.
        # Lichess sends a keepalive newline every ~60 s. If we get nothing for
        # 120 s the connection is dead and we should reconnect.
        stream_timeout = aiohttp.ClientTimeout(total=None, connect=30, sock_read=120)
        backoff = 2
        while True:
            try:
                logger.info("Connecting to Lichess Event Stream...")
                async with self.session.get(url, timeout=stream_timeout) as resp:
                    if resp.status != 200:
                        logger.error(f"Event stream HTTP {resp.status}, retrying in {backoff}s...")
                        await asyncio.sleep(backoff)
                        backoff = min(backoff * 2, 60)
                        continue

                    backoff = 2
                    logger.info("Event stream connected — waiting for events...")
                    async for line in resp.content:
                        if not line:
                            continue
                        line_str = line.decode('utf-8').strip()
                        if not line_str:
                            continue
                        try:
                            event = json.loads(line_str)
                            await self.handle_event(event)
                        except json.JSONDecodeError:
                            continue
                # Stream ended cleanly (server closed it) — reconnect
                logger.warning("Event stream ended by server. Reconnecting in 3s...")
                await asyncio.sleep(3)
            except asyncio.CancelledError:
                logger.info("Event stream listener cancelled.")
                break
            except asyncio.TimeoutError:
                logger.warning(f"Event stream timed out (no keepalive). Reconnecting in {backoff}s...")
                await asyncio.sleep(backoff)
                backoff = min(backoff * 2, 30)
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
            dest_user = challenge.get("destUser", {}).get("name", "")
            variant = challenge.get("variant", {}).get("key", "standard")
            speed = challenge.get("speed", "unknown")
            rated = challenge.get("rated", False)

            # If WE are the challenger (outgoing challenge we sent), ignore it — don't self-accept
            if challenger.lower() == self.bot_username.lower():
                async with self.lock:
                    self.pending_challenge_ids.discard(challenge_id)
                logger.info(f"[Challenge] Outgoing challenge {challenge_id} to '{dest_user}' is pending their response.")
                return

            async with self.lock:
                current_count = len(self.active_games)

            logger.info(
                f"[Challenge] From '{challenger}' | Variant: {variant} | Speed: {speed} | Rated: {rated} | Active: {current_count}/{self.max_games}"
            )

            if variant != "standard":
                logger.info(f"[Challenge Declined] {challenge_id} (Reason: Non-standard variant '{variant}')")
                await self.decline_challenge(challenge_id, "variant")
            elif current_count >= self.max_games:
                logger.info(f"[Challenge Declined] {challenge_id} (Reason: Capacity reached {current_count}/{self.max_games})")
                await self.decline_challenge(challenge_id, "tooManyGames")
            else:
                logger.info(f"[Challenge Accepted] {challenge_id} vs {challenger}")
                await self.accept_challenge(challenge_id)

        elif event_type == "gameStart":
            game = event.get("game", {})
            game_id = game.get("id")
            if not game_id:
                return

            async with self.lock:
                if game_id in self.active_games:
                    return
                self.active_games.add(game_id)
                task = asyncio.create_task(self.play_game(game_id))
                self.active_tasks[game_id] = task
                logger.info(f"[Game Started] https://lichess.org/{game_id} | Active Games: {len(self.active_games)}/{self.max_games}")

        elif event_type == "challengeDeclined":
            challenge = event.get("challenge", {})
            challenge_id = challenge.get("id")
            dest = challenge.get("destUser", {}).get("name", "?")
            reason = challenge.get("declineReason", "")
            reason_key = challenge.get("declineReasonKey", "")
            logger.info(f"[Challenge Declined by '{dest}'] ID: {challenge_id} — {reason}")
            async with self.lock:
                self.pending_challenge_ids.discard(challenge_id)
                target = self.challenge_id_to_target.pop(challenge_id, dest)
                rejected_tc = self.challenge_id_to_tc.pop(challenge_id, None)
                if reason_key == "noBot" or "not accepting challenges from bots" in reason.lower():
                    self.bot_blacklist.add(target)
                    logger.info(f"[Blacklisted] '{target}' added to permanent blacklist (no bot challenges).")
                elif reason_key == "timeControl" or "time control" in reason.lower():
                    # Record this specific TC as rejected for this bot
                    if rejected_tc:
                        if target not in self.bot_tc_rejections:
                            self.bot_tc_rejections[target] = set()
                        self.bot_tc_rejections[target].add((rejected_tc[0], rejected_tc[1]))
                        remaining = len(self.time_controls) - len(self.bot_tc_rejections[target])
                        logger.info(f"[Auto-Seeker] '{target}' rejected {rejected_tc[2]} — {remaining} TC(s) left to try.")
                    # Short cooldown so we retry this bot soon with a different TC
                    self.recently_challenged[target] = time.time() - 540  # ~1 min cooldown
                else:
                    # Generic decline — standard 10-min cooldown
                    self.recently_challenged[target] = time.time()

        elif event_type == "challengeCancelled":
            challenge = event.get("challenge", {})
            challenge_id = challenge.get("id")
            async with self.lock:
                self.pending_challenge_ids.discard(challenge_id)
                self.challenge_id_to_target.pop(challenge_id, None)
                self.challenge_id_to_tc.pop(challenge_id, None)

        elif event_type == "gameFinish":
            game = event.get("game", {})
            game_id = game.get("id")
            if game_id:
                await self.cleanup_game(game_id, reason="finished")

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
        data = {"reason": reason}
        try:
            async with self.session.post(url, data=data) as resp:
                pass
        except Exception as e:
            logger.error(f"Error declining challenge {challenge_id}: {e}")

    async def play_game(self, game_id: str):
        url = f"{LICHESS_API}/bot/game/stream/{game_id}"
        my_color = None
        initial_fen = START_FEN
        processed_moves_count = 0
        engine_proc: GameEngineProcess = None
        # Game streams also need a timeout — active games always have regular
        # state events; 5 minutes of silence means the stream is dead.
        game_timeout = aiohttp.ClientTimeout(total=None, connect=30, sock_read=300)

        try:
            async with self.session.get(url, timeout=game_timeout) as resp:
                if resp.status != 200:
                    logger.error(f"[{game_id}] Stream HTTP error {resp.status}")
                    return

                async for line in resp.content:
                    if not line:
                        continue
                    line_str = line.decode('utf-8').strip()
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
                        speed = msg.get("speed", "unknown")
                        clock_info = msg.get("clock", {})

                        # Spawn one persistent engine process for this game
                        engine_proc = self.engine_cfg.new_game_process(speed, clock_info)
                        await engine_proc.start(initial_fen)
                        logger.info(f"[{game_id}] Game initialized | Color: {my_color.upper()} | Speed: {speed.upper()} | Depth: {engine_proc.depth} | Engine process started")

                        state = msg.get("state", {})
                        new_count = await self.process_game_state(
                            game_id, state, my_color, engine_proc, processed_moves_count
                        )
                        if new_count is not None:
                            processed_moves_count = new_count

                    elif msg_type == "gameState":
                        if my_color is None or engine_proc is None:
                            continue
                        status = msg.get("status", "")
                        if status in ["mate", "resign", "outoftime", "stalemate",
                                      "draw", "noStart", "cheat", "variantEnd", "aborted"]:
                            await self._log_game_result(game_id, msg, my_color)
                            break
                        new_count = await self.process_game_state(
                            game_id, msg, my_color, engine_proc, processed_moves_count
                        )
                        if new_count is not None:
                            processed_moves_count = new_count

        except asyncio.CancelledError:
            logger.info(f"[{game_id}] Task cancelled.")
        except Exception as e:
            logger.error(f"[{game_id}] Stream exception: {e}")
        finally:
            if engine_proc:
                await engine_proc.stop()
            await self.cleanup_game(game_id, reason="stream closed")

    async def _log_game_result(self, game_id: str, state: dict, my_color: str):
        """Log a human-readable result when a game ends."""
        status = state.get("status", "unknown")
        winner = state.get("winner", "")  # "white" | "black" | "" (draw)

        STATUS_LABELS = {
            "mate":       "Checkmate",
            "resign":     "Resignation",
            "outoftime":  "Time forfeit",
            "stalemate":  "Stalemate (draw)",
            "draw":       "Draw (agreed/repetition)",
            "aborted":    "Game aborted",
            "noStart":    "No start",
            "cheat":      "Cheat detected",
            "variantEnd": "Variant end",
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
            await self._log_game_result(game_id, state, my_color)
            return None

        moves_str = state.get("moves", "").strip()
        moves_list = moves_str.split() if moves_str else []
        total_moves = len(moves_list)

        # Determine whose turn it is
        is_white_turn = (total_moves % 2 == 0)
        is_my_turn = (is_white_turn and my_color == "white") or (not is_white_turn and my_color == "black")

        if not is_my_turn:
            return None

        # Skip if we already submitted a move for this position
        if total_moves < processed_moves_count:
            return None

        # Compute dynamic time allocation based on clock & increment
        allocated_time_ms = calculate_search_time(state, my_color)

        # Adaptive search depth ceiling based on time budget
        search_depth = engine_proc.depth
        if allocated_time_ms is not None:
            if allocated_time_ms < 200:
                search_depth = min(search_depth, 6)
            elif allocated_time_ms < 500:
                search_depth = min(search_depth, 8)
            elif allocated_time_ms < 1000:
                search_depth = min(search_depth, 10)
            elif allocated_time_ms >= 1000:
                search_depth = max(search_depth, 14)

        start_time = time.time()
        budget_str = f" ({allocated_time_ms}ms budget)" if allocated_time_ms is not None else ""
        logger.info(f"[{game_id}] Engine thinking at depth {search_depth}{budget_str} (move #{total_moves + 1})...")

        # Replay any moves the engine process hasn't seen yet
        # (moves_list[processed_moves_count - 1] was the last move we told the engine about
        #  when we called 'best'; moves after that need to be 'apply'-ed)
        # The engine already applied our last move internally via 'best', so we only
        # need to send opponent moves played since then.
        new_moves = moves_list[processed_moves_count:]
        for move in new_moves:
            res = await engine_proc.apply_move(move)
            if "error" in res:
                logger.error(f"[{game_id}] apply {move} failed: {res['error']}")
                return None

        # Ask engine for best move with depth and allocated time limit
        res = await engine_proc.best_move(depth=search_depth, time_ms=allocated_time_ms)
        elapsed_ms = int((time.time() - start_time) * 1000)

        bot_move = res.get("best_move", "")
        if bot_move:
            logger.info(f"[{game_id}] TigerFish plays: {bot_move} ({elapsed_ms}ms)")
            await self.send_move(game_id, bot_move)
            return total_moves + 1
        else:
            engine_status = res.get("status", "")
            logger.warning(f"[{game_id}] Engine produced no move (status: {engine_status})")
            return None

    async def send_move(self, game_id: str, move_uci: str):
        url = f"{LICHESS_API}/bot/game/{game_id}/move/{move_uci}"
        for attempt in range(1, 4):
            try:
                async with self.session.post(url) as resp:
                    if resp.status == 200:
                        return True
                    body = await resp.text()
                    logger.warning(f"[{game_id}] Move submission {move_uci} attempt {attempt} failed (HTTP {resp.status}): {body[:100]}")
            except Exception as e:
                logger.warning(f"[{game_id}] Move submission {move_uci} attempt {attempt} network glitch: {e}")
            if attempt < 3:
                await asyncio.sleep(0.5)
        logger.error(f"[{game_id}] Failed to post move {move_uci} after 3 attempts.")
        return False

    async def cleanup_game(self, game_id: str, reason: str = ""):
        async with self.lock:
            if game_id in self.active_games:
                self.active_games.remove(game_id)
                self.active_tasks.pop(game_id, None)
                logger.info(f"[{game_id}] Cleaned up ({reason}) | Active Games: {len(self.active_games)}/{self.max_games}")
                # Refresh bot ratings in the background so limits are perfectly up-to-date
                asyncio.create_task(self.fetch_bot_username())

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
    parser = argparse.ArgumentParser(description="TigerFish Lichess Multi-Bot Bridge (16 Parallel Games)")
    parser.add_argument("--token", type=str, help="Lichess API Token", default=os.getenv("LICHESS_TOKEN"))
    parser.add_argument("--engine", type=str, help="Path to game.exe", default=DEFAULT_ENGINE_PATH)
    parser.add_argument("--max-games", type=int, help="Max parallel games", default=16)
    parser.add_argument("--depth", type=int, help="Engine search depth ceiling (default: 12)", default=12)
    parser.add_argument("--time", type=int, help="Fallback search time budget in ms for untimed games (default: 2000)", default=2000)
    parser.add_argument("--auto-challenge", action=argparse.BooleanOptionalAction, default=True, help="Automatically seek and challenge online bots to keep 16 parallel games active")
    args = parser.parse_args()

    token = args.token
    if not token:
        logger.error("No Lichess API Token found!")
        logger.error("Please add your token to the .env file as:")
        logger.error("  LICHESS_TOKEN=lip_xxxxxxxxx")
        logger.error("Or pass it via command line:")
        logger.error("  python lichess_multibot.py --token lip_xxxxxxxxx")
        sys.exit(1)

    bot = LichessMultiBot(
        token=token,
        engine_path=args.engine,
        max_games=args.max_games,
        search_depth=args.depth,
        auto_challenge=args.auto_challenge
    )

    try:
        asyncio.run(bot.start())
    except KeyboardInterrupt:
        pass  # graceful shutdown handled inside start() via CancelledError

if __name__ == "__main__":
    main()
