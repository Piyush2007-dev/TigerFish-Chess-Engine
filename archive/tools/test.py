# main.py

from chess import Board

# Create a board.
# Board() automatically loads the starting position in __post_init__().
board = Board()

# Print the board.
#board.set_fen("8/8/8/8/8/8/8/8 w - - 0 1")
board.print_board()
