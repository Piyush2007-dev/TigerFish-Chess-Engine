from time import perf_counter
import random
from chess import Board,Piece,Color

NUM_PLIES = 400  # 20 half-moves = 10 moves each side

board = Board()

total_time = 0.0
move_counts = []

for ply in range(NUM_PLIES):
    # Measure move generation time
    start = perf_counter()

    if board.side_to_move == 'w':
        moves = board.generate_white_moves()
    else:
        moves = board.generate_black_moves()

    end = perf_counter()

    elapsed = end - start
    total_time += elapsed
    move_counts.append(len(moves))

    # No legal moves
    if not moves:
        print(f"No moves available at ply {ply + 1}")
        break

    # Choose random move
    move = random.choice(moves)

    print(
        f"Ply {ply + 1:2d} | "
        f"Side: {board.side_to_move} | "
        f"Moves: {len(moves):3d} | "
        f"Time: {elapsed * 1_000_000:8.2f} µs | "
        f"Chosen: {move}"
    )

    # Make move
    board.make_move(move)
    board.print_board()
    print("White occupancy:", bin(board._occupancy[Color.WHITE].value))
    print("Black occupancy:", bin(board._occupancy[Color.BLACK].value))

# Final statistics
num_generated = len(move_counts)

if num_generated > 0:
    avg_time = total_time / num_generated
    avg_moves = sum(move_counts) / num_generated

    print("\n===== SUMMARY =====")
    print(f"Plies played           : {num_generated}")
    print(f"Average moves generated: {avg_moves:.2f}")
    print(f"Total time             : {total_time * 1000:.3f} ms")
    print(f"Average time per ply   : {avg_time * 1_000_000:.2f} µs")