# lichess_bot.py — Zero-dependency Lichess Bot Bridge for TigerFish Engine
# Usage: python lichess_bot.py --token "lip_your_token_here"

import argparse
import json
import os
import subprocess
import sys
import urllib.request
import urllib.parse

LICHESS_API = "https://lichess.org/api"

class TigerFishBridge:
    def __init__(self, token, engine_path="./game.exe"):
        self.token = token
        self.headers = {"Authorization": f"Bearer {token}"}
        self.engine_path = engine_path
        self.engine_proc = None

    def start_engine(self):
        print(f"[TigerFish] Launching C++ engine: {self.engine_path}")
        self.engine_proc = subprocess.Popen(
            [self.engine_path, "interactive"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1
        )

    def send_engine_command(self, cmd):
        self.engine_proc.stdin.write(cmd + "\n")
        self.engine_proc.stdin.flush()
        
        output_lines = []
        while True:
            line = self.engine_proc.stdout.readline()
            if not line:
                break
            if "===READY===" in line:
                break
            output_lines.append(line)
        return "".join(output_lines).strip()

    def make_request(self, endpoint, data=None, method=None):
        url = f"{LICHESS_API}{endpoint}"
        encoded_data = None
        if data:
            encoded_data = urllib.parse.urlencode(data).encode('utf-8')
        
        req = urllib.request.Request(url, data=encoded_data, headers=self.headers, method=method)
        try:
            with urllib.request.urlopen(req) as resp:
                return json.loads(resp.read().decode('utf-8'))
        except Exception as e:
            print(f"[API Error] {endpoint}: {e}")
            return None

    def upgrade_account_to_bot(self):
        print("[Lichess] Converting account to official BOT status...")
        res = self.make_request("/bot/account/upgrade", data={}, method="POST")
        if res and res.get("ok"):
            print("[Lichess] Success! Account upgraded to BOT.")
        else:
            print("[Lichess] Note: Account is already a BOT or upgrade returned:", res)

    def listen_event_stream(self):
        url = f"{LICHESS_API}/stream/event"
        req = urllib.request.Request(url, headers=self.headers)
        print("[TigerFish] Listening for game challenges on Lichess... (Press Ctrl+C to stop)")
        
        try:
            with urllib.request.urlopen(req) as response:
                for line in response:
                    if not line.strip():
                        continue
                    event = json.loads(line.decode('utf-8'))
                    event_type = event.get("type")
                    
                    if event_type == "challenge":
                        challenge_id = event["challenge"]["id"]
                        challenger = event["challenge"]["challenger"]["name"]
                        variant = event["challenge"]["variant"]["key"]
                        print(f"[Challenge Received] From {challenger} (Variant: {variant})")
                        
                        if variant == "standard":
                            self.make_request(f"/challenge/{challenge_id}/accept", data={}, method="POST")
                            print(f"[Challenge Accepted] Playing against {challenger}")
                        else:
                            self.make_request(f"/challenge/{challenge_id}/decline", data={"reason": "variant"}, method="POST")
                    
                    elif event_type == "gameStart":
                        game_id = event["game"]["id"]
                        print(f"\n[Game Started] Game ID: https://lichess.org/{game_id}")
                        self.play_game(game_id)
        except KeyboardInterrupt:
            print("\n[TigerFish] Bot shutting down.")
            if self.engine_proc:
                self.engine_proc.terminate()
        except Exception as e:
            print(f"[Stream Error] {e}")

    def play_game(self, game_id):
        url = f"{LICHESS_API}/bot/game/stream/{game_id}"
        req = urllib.request.Request(url, headers=self.headers)
        
        my_color = None
        current_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        
        try:
            with urllib.request.urlopen(req) as response:
                for line in response:
                    if not line.strip():
                        continue
                    msg = json.loads(line.decode('utf-8'))
                    msg_type = msg.get("type")
                    
                    if msg_type == "gameFull":
                        my_color = "white" if msg["white"].get("name") == "TigerFish-BOT" else "black"
                        print(f"[Game Init] Playing as {my_color.upper()}")
                        state = msg["state"]
                        self.handle_game_state(game_id, state, my_color)
                    
                    elif msg_type == "gameState":
                        self.handle_game_state(game_id, msg, my_color)
        except Exception as e:
            print(f"[Game Error] {e}")

    def handle_game_state(self, game_id, state, my_color):
        status = state.get("status")
        if status in ["mate", "resign", "outoftime", "stalemate", "draw"]:
            print(f"[Game Ended] Status: {status}")
            return
        
        moves_str = state.get("moves", "").strip()
        moves_list = moves_str.split() if moves_str else []
        
        # Determine whose turn it is
        is_my_turn = (len(moves_list) % 2 == 0) if my_color == "white" else (len(moves_list) % 2 == 1)
        
        if is_my_turn:
            print(f"[TigerFish Thinking] Move #{len(moves_list) + 1}...")
            
            # Play moves sequentially on engine to get current state
            # Simple approach: build FEN or send moves list
            # We use interactive mode 'make' command
            cmd = f"make {moves_list[-1]} 6 {self.reconstruct_fen(moves_list[:-1])}" if moves_list else "moves rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
            
            res_raw = self.send_engine_command(cmd)
            try:
                res = json.loads(res_raw)
                bot_move = res.get("bot_move")
                if not bot_move and "moves" in res and res["moves"]:
                    bot_move = res["moves"][0]
                
                if bot_move:
                    print(f"[TigerFish Plays] Move: {bot_move}")
                    self.make_request(f"/bot/game/{game_id}/move/{bot_move}", data={}, method="POST")
            except Exception as e:
                print(f"[Parse Error] {e}")

    def reconstruct_fen(self, moves_list):
        # Sends moves to engine to get current FEN
        cmd = "moves rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        res_raw = self.send_engine_command(cmd)
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        for m in moves_list:
            res_raw = self.send_engine_command(f"make {m} 0 {fen}")
            try:
                res = json.loads(res_raw)
                fen = res.get("fen", fen)
            except:
                pass
        return fen

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

if __name__ == "__main__":
    load_env_file()
    parser = argparse.ArgumentParser(description="TigerFish Lichess Bot Bridge")
    parser.add_argument("--token", type=str, help="Lichess API Token", default=os.getenv("LICHESS_TOKEN"))
    args = parser.parse_args()
    
    if not args.token:
        print("[Error] No Lichess token found!")
        print("Please paste your token into the .env file as:")
        print("LICHESS_TOKEN=lip_your_token_here")
        print("Or run: python lichess_bot.py --token \"lip_xxxx\"")
        sys.exit(1)
        
    bridge = TigerFishBridge(token=args.token)
    bridge.start_engine()
    bridge.upgrade_account_to_bot()
    bridge.listen_event_stream()
