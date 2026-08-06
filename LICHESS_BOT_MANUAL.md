# TigerFish Lichess Bot Bridge — Operations & Usage Manual

> **Scope**: Operating, testing, and understanding the Python Lichess Bot Bridges (`lichess_bot.py` and `lichess_multibot.py`).
> **Prerequisites**: Python 3.10+, `aiohttp`, compiled `game.exe` binary in root, valid `.env` file containing `LICHESS_TOKEN=lip_xxxx`.

---

## Table of Contents
1. [Overview & Architecture](#1-overview--architecture)
2. [Single-Game Bot (`lichess_bot.py`)](#2-single-game-bot-lichess_botpy)
3. [Multi-Game Bot Farmer (`lichess_multibot.py`)](#3-multi-game-bot-farmer-lichess_multibotpy)
4. [IPC Protocol with `game.exe interactive`](#4-ipc-protocol-with-gameexe-interactive)
5. [Resilient Move Posting & Retry Mechanism](#5-resilient-move-posting--retry-mechanism)
6. [How to Test & Challenge Bots (Browser & CLI)](#6-how-to-test--challenge-bots-browser--cli)

---

## 1. Overview & Architecture

```
                       Lichess API (Event Stream / REST)
                                      |
                           (aiohttp Event Stream)
                                      |
                      lichess_bot.py / lichess_multibot.py
                                      |
                           (stdin / stdout IPC)
                                      |
                   game.exe interactive (32 MB TT)
```

The bot bridge connects to the Lichess Bot API stream and manages persistent engine sessions. Each game played spawns its own `game.exe interactive` process. The **32 MB Transposition Table** stays alive inside `game.exe` for the duration of the match to preserve evaluation depth.

---

## 2. Single-Game Bot (`lichess_bot.py`)

Handles **1 game at a time**. Ideal for testing, casual play, or debugging.

### Features:
- Auto-upgrades account to BOT status on Lichess if needed.
- Dynamically queries `/api/account` on startup to detect the account's username.
- Auto-accepts all incoming standard variant challenges.
- Runs a lightweight health check HTTP server on port 10000.
- Configurable search depth (default 6).

### Usage Command:
```bash
python lichess_bot.py --depth 6
```

---

## 3. Multi-Game Bot Farmer (`lichess_multibot.py`)

Handles **up to 10 parallel games simultaneously** and actively seeks games against online bots.

### Features:
- **Auto-Seeker**: Continuously seeks matches against online bots across 11 time controls (1+0 bullet to 5+3 blitz).
- **Rejection Memory**: Blacklists bots that refuse bot challenges and skips time controls previously declined by specific bots.
- **Graceful Shutdown**: Pressing `q` + Enter or sending `Ctrl+C` halts new challenges and lets active matches finish cleanly before exiting.

### Usage Command:
```bash
python lichess_multibot.py --max-games 10 --depth 7
```

---

## 4. IPC Protocol with `game.exe interactive`

For every match, the bridge spawns a dedicated long-running `game.exe interactive` process.

| Command | Direction | Description | Response |
| :--- | :--- | :--- | :--- |
| `newgame [fen]` | Python $\to$ Engine | Sets up board position. Retains Transposition Table. | `{"status": "ready"}` + `===READY===` |
| `apply <uci>` | Python $\to$ Engine | Applies opponent move to internal board (depth 0). | `{"fen": "...", "status": "..."}` + `===READY===` |
| `best <depth>` | Python $\to$ Engine | Evaluates position at depth, plays best move, advances board. | `{"best_move": "uci", ...}` + `===READY===` |
| `quit` | Python $\to$ Engine | Terminates engine process gracefully. | Process exits |

---

## 5. Resilient Move Posting & Retry Mechanism

To protect against transient Windows network socket timeouts (`WinError 121: The semaphore timeout period has expired`), move submissions use a **3-attempt retry loop** with exponential backoff:

```python
async def send_move(self, game_id: str, move_uci: str):
    url = f"{LICHESS_API}/bot/game/{game_id}/move/{move_uci}"
    for attempt in range(1, 4):
        try:
            async with self.session.post(url) as resp:
                if resp.status == 200:
                    return True
        except Exception as e:
            logger.warning(f"[{game_id}] Move submission attempt {attempt} network glitch: {e}")
        if attempt < 3:
            await asyncio.sleep(0.5) # Retry after 0.5s
    return False
```

---

## 6. Local Override & Render Standby Handshake

To prevent dual-instance connection conflicts, duplicate move submissions, and `HTTP 429 Rate Limiting` when launching `lichess_bot.py` locally while a Render deployment is active:

1. **`HealthCheckHandler` Endpoints**:
   - `GET /status`: Returns JSON status (`{"status": "ACTIVE" | "STANDBY"}`).
   - `POST /standby`: Pauses event stream handling on remote instance.
   - `POST /resume`: Resumes active event stream handling on remote instance.

2. **Automatic Local Override**:
   - Run locally with `--remote-url`:
     ```bash
     python lichess_bot.py --remote-url https://tigerfish-bot.onrender.com
     ```
   - On startup, the local process sends a `POST /standby` handshake to Render.
   - Render pauses its stream processor (`BOT_MODE = "STANDBY"`).
   - On local shutdown (Ctrl+C), local automatically sends `POST /resume` to reactivate Render!

---

## 7. How to Test & Challenge Bots (Browser & CLI)

### Method A: Challenge a Bot from the Lichess Website (Browser)
1. Run `python lichess_bot.py` in your terminal.
2. Open **[https://lichess.org/player/bots](https://lichess.org/player/bots)** in your browser (logged into your Bot account).
3. Search for any online bot (e.g. `maia1`, `v7p3r_bot`, `sunfish-engine`).
4. Click on the bot's name to open their profile.
5. Click **⚔️ Challenge to a game** -> Standard Variant -> 3+2 or 5+3 Blitz -> **Challenge**.
6. As soon as the bot accepts, `lichess_bot.py` picks up the game stream, spawns `game.exe interactive`, and plays live!

### Method B: Challenge a Bot via Command Line
```bash
python lichess_bot.py --challenge maia1
```
