# bench_python_search.py — Benchmarks minimax search in pure Python using python-chess
import time
import chess

node_count = 0

# Simple piece values for python evaluation
PIECE_VALUES = {
    chess.PAWN: 100,
    chess.KNIGHT: 300,
    chess.BISHOP: 300,
    chess.ROOK: 500,
    chess.QUEEN: 900,
    chess.KING: 0
}

def evaluate(board):
    score = 0
    for square, piece in board.piece_map().items():
        val = PIECE_VALUES.get(piece.piece_type, 0)
        if piece.color == chess.WHITE:
            score += val
        else:
            score -= val
    return score

def minimax(board, depth, alpha, beta, maximizing):
    global node_count
    node_count += 1

    if depth == 0 or board.is_game_over():
        if board.is_checkmate():
            return -20000 - depth if maximizing else 20000 + depth
        if board.is_stalemate():
            return 0
        return evaluate(board)

    moves = list(board.legal_moves)
    if maximizing:
        best = -100000
        for move in moves:
            board.push(move)
            score = minimax(board, depth - 1, alpha, beta, False)
            board.pop()
            if score > best: best = score
            if best > alpha: alpha = best
            if beta <= alpha: break
        return best
    else:
        best = 100000
        for move in moves:
            board.push(move)
            score = minimax(board, depth - 1, alpha, beta, True)
            board.pop()
            if score < best: best = score
            if best < beta: beta = score
            if beta <= alpha: break
        return best

def benchmark_search():
    # Position from screenshot
    fen = "r1b1kb1r/pp3ppp/1q2p3/8/3R4/1N2P3/PPP1Q1PP/3R2K1 b kq - 0 1"
    board = chess.Board(fen)

    print("======================================================")
    print("   python-chess (Python + Minimax) Search Benchmark   ")
    print("======================================================")

    for depth in range(1, 6): # Depths 1 to 4 (5/6 would take too long in Python)
        global node_count
        node_count = 0

        t0 = time.perf_counter()
        best_score = 100000 if board.turn == chess.BLACK else -100000
        best_move = None

        for move in board.legal_moves:
            board.push(move)
            score = minimax(board, depth - 1, -100000, 100000, board.turn == chess.WHITE)
            board.pop()

            if board.turn == chess.BLACK:
                if score < best_score:
                    best_score = score
                    best_move = move
            else:
                if score > best_score:
                    best_score = score
                    best_move = move

        t1 = time.perf_counter()
        elapsed_ms = (t1 - t0) * 1000.0

        nps = (node_count / (t1 - t0)) if (t1 - t0) > 0 else 0
        print(f"  Depth {depth} | Time: {elapsed_ms:.1f} ms | Nodes: {node_count:,} | Speed: {nps:,.0f} nps | Move: {best_move}")

    print("======================================================")

if __name__ == "__main__":
    benchmark_search()
