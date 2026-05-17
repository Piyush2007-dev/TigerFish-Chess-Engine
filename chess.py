from dataclasses import dataclass, field
from ctypes import c_uint64
from enum import IntEnum
from rays import Direction, RayTable

@dataclass(slots=True)
class Bitboard:
    _value: c_uint64 = field(init=False, repr=False)
    #init=False  removes _value from argumnet list of defalut constructor 
    #repr ??

    def __init__(self, value: int = 0):
        self._value = c_uint64(value)

    @property
    def value(self) -> int:
        return self._value.value

    @value.setter
    def value(self, new_value: int):
        self._value.value = new_value

class Piece(IntEnum):
    # White pieces
    P = 0   # Pawn
    N = 1   # Knight
    B = 2   # Bishop
    R = 3   # Rook
    Q = 4   # Queen
    K = 5   # King
    
    # Black pieces
    p = 6   # Pawn
    n = 7   # Knight
    b = 8   # Bishop
    r = 9   # Rook
    q = 10  # Queen
    k = 11  # King
    
@dataclass(slots=True)
class Board:
    
    START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    
    # Fixed-size container of 12 Bitboards
    _bitboards: tuple[Bitboard, ...] = field(
        default_factory=lambda: tuple(Bitboard() for _ in range(12)),
        repr=False,
    )
    _occupancy: tuple[Bitboard, ...] = field(
        default_factory=lambda: tuple(Bitboard() for _ in range(3)),
        repr=False,
    )
    side_to_move: str = 'w'        # 'w' or 'b'
    castling_rights: str = '-'     # e.g. 'KQkq'
    en_passant: str = '-'          # e.g. 'e3' or '-'
    halfmove_clock: int = 0
    fullmove_number: int = 1
    
    def __post_init__(self):
        self.set_fen(self.START_FEN)
        
    def set_fen(self, fen_string):
        
        for bb in self._bitboards:
            bb.value = 0
            
        self.side_to_move = "w"
        self.castling_rights = "-"
        self.en_passant = "-"
        self.halfmove_clock = 0
        self.fullmove_number = 1

        rank = 7
        file = 0

        fields = fen_string.split()
        placement = fields[0]
        
        for c in placement:
            square= rank*8 + file
            match c:
                case 'p':
                    self._bitboards[Piece.p].value |= 1 << square
                    file += 1
                case 'n':
                    self._bitboards[Piece.n].value |= 1 << square
                    file += 1
                case 'b':
                    self._bitboards[Piece.b].value |= 1 << square
                    file += 1
                case 'r':
                    self._bitboards[Piece.r].value |= 1 << square
                    file += 1
                case 'q':
                    self._bitboards[Piece.q].value |= 1 << square
                    file += 1
                case 'k':
                    self._bitboards[Piece.k].value |= 1 << square
                    file += 1
                case 'P':
                    self._bitboards[Piece.P].value |= 1 << square
                    file += 1
                case 'N':
                    self._bitboards[Piece.N].value |= 1 << square
                    file += 1
                case 'B':
                    self._bitboards[Piece.B].value |= 1 << square
                    file += 1
                case 'R':
                    self._bitboards[Piece.R].value |= 1 << square
                    file += 1
                case 'Q':
                    self._bitboards[Piece.Q].value |= 1 << square
                    file += 1
                case 'K':
                    self._bitboards[Piece.K].value |= 1 << square
                    file += 1
                case '/':
                    rank-= 1 
                    file= 0
                case '1':
                    file += 1
                case '2':
                    file += 2
                case '3':
                    file += 3
                case '4':
                    file += 4
                case '5':
                    file += 5
                case '6':
                    file += 6
                case '7':
                    file += 7
                case '8':
                    file += 8
                
        if len(fields) > 1:
            self.side_to_move = fields[1]

        if len(fields) > 2:
            self.castling_rights = fields[2]

        if len(fields) > 3:
          self.en_passant = fields[3]

        if len(fields) > 4:
            self.halfmove_clock = int(fields[4])

        if len(fields) > 5:
            self.fullmove_number = int(fields[5])
    
    def update_occupancy(self) -> None:
        self._occupancy[0].value=0
        for i in range(6):
            self._occupancy[0].value |=self._bitboards[i].value
            
        self._occupancy[1].value=0
        for i in range(6,12):
            self._occupancy[1].value|=self._bitboards[i].value
            
        self._occupancy[3].value=self._occupancy[0].value | self._occupancy[1].value
        
    def has_piece(self, piece: Piece, square: int) -> bool:
        return ((self._bitboards[piece].value >> square) & 1) == 1
        
    def print_board(self):
        for rank in range(7, -1, -1):
            print(f"{rank + 1} ", end="")
            for file in range(8):
                square = rank * 8 + file
                piece_char = "."
                for piece in Piece: #Python enums are iterable
                    if (self._bitboards[piece].value >> square) & 1:# check bit at position of square by shifting to lsb then AND 1 =1 piece found 
                        piece_char = piece.name
                        break
                print(piece_char, end=" ")

            print()

        print("  a b c d e f g h")  
    
class MoveGenerator:
    def __init__(self, board: Board):
        self.board = board
        
    @staticmethod
    def get_ray(direction: Direction | int, square: int) -> int:
        return RayTable.get(direction, square)
    
    def WhitePawnMoves(self)->list[tuple[int,int]] :
        
        white_pawn=self.board._bitboards[Piece.P].value
        white_pawn_move =[]
        
        # single push 
        single_push_target=white_pawn<<8
        single_push_target&=~self.board._occupancy[2].value
            
        #double push 
        rank_2_pawns=white_pawn & 0x000000000000FF00
        first_step = rank_2_pawns << 8
        first_step &= ~self.board._occupancy[2].value
        double_push_targets = first_step << 8
        double_push_targets &= ~self.board._occupancy[2].value
        
        mask= single_push_target | double_push_targets
        
        while mask:
            
            lsb =mask & -mask
            to_square=lsb.bit_length() -1 # opposite lsb = 1<<square
            
            if (double_push_targets >> to_square) & 1:
                from_square = to_square - 16
                
            else:
                from_square = to_square - 8
            white_pawn_move.append((from_square, to_square))
            
            mask &= mask - 1
            
        # all possible pesudo legal moves are generated 
        # capture promotion en passent is left 
    
        return white_pawn_move

    def WhiteKnightMoves(self)->list[tuple[int,int]] :
        
        white_knight=self.board._bitboards[Piece.N].value
        white_knight_move =[]
        
        mask=white_knight
        while mask:
            
            lsb =mask & -mask
            from_square=lsb.bit_length() -1
            
            l1=(lsb & 0x7f7f7f7f7f7f7f7f)<< 1 
            l2=(lsb & 0x3f3f3f3f3f3f3f3f)<< 2
            r1=(lsb & 0xfefefefefefefefe)>> 1
            r2=(lsb & 0xfcfcfcfcfcfcfcfc)>> 2
        
            h1= l1|r1
            h2= l2|r2
            
            moves_mask= (h1<<16) | (h1>>16) | (h2<<8) | (h2>>8)
            moves_mask &= 0xFFFFFFFFFFFFFFFF
            
            moves_mask&=~self.board._occupancy[0].value
            mask &= mask - 1
            
            while moves_mask:
        
                lsb2 = moves_mask & -moves_mask
                to_square=lsb2.bit_length()-1
                white_knight_move.append((from_square ,to_square))
                
                moves_mask &= moves_mask - 1
                
        return white_knight_move

    def WhiteBishopMoves(self)->list[tuple[int,int]] :

        white_bishop=self.board._bitboards[Piece.B].value
        white_bishop_move =[]

        mask=white_bishop

        while mask:
            move_mask=0

            lsb=mask & -mask
            from_square=lsb.bit_length()-1

            for i in range(4,8): # NE SE NW SW only
                ray=self.get_ray(i,from_square)
                blockers= ray & self.board._occupancy[2].value

                if (blockers==0):
                    move_mask |=ray

                else:

                    if(i % 2==0):
                        blocker_lsb= blockers & -blockers
                        blocker_square=blocker_lsb.bit_length()-1

                    else:
                        blocker_square=blockers.bit_length()-1

                    attacks= ray ^ self.get_ray(i,blocker_square)
                    move_mask |= attacks & ~self.board._occupancy[0].value

            while move_mask:
                lsb2= move_mask & -move_mask
                to_square=lsb2.bit_length()-1
                white_bishop_move.append((from_square,to_square))
                move_mask &= move_mask - 1

            mask&=mask-1

        return white_bishop_move

    def WhiteRookMoves(self)->list[tuple[int,int]] :

        white_rook=self.board._bitboards[Piece.R].value
        white_rook_move =[]

        mask=white_rook

        while mask:
            move_mask=0

            lsb=mask & -mask
            from_square=lsb.bit_length()-1

            for i in range(4): # NORTH SOUTH EAST WEST only
                ray=self.get_ray(i,from_square)
                blockers= ray & self.board._occupancy[2].value

                if (blockers==0):
                    move_mask |=ray

                else:

                    if(i % 2==0):
                        blocker_lsb= blockers & -blockers
                        blocker_square=blocker_lsb.bit_length()-1

                    else:
                        blocker_square=blockers.bit_length()-1

                    attacks= ray ^ self.get_ray(i,blocker_square)
                    move_mask |= attacks & ~self.board._occupancy[0].value

            while move_mask:
                lsb2= move_mask & -move_mask
                to_square=lsb2.bit_length()-1
                white_rook_move.append((from_square,to_square))
                move_mask &= move_mask - 1

            mask&=mask-1

        return white_rook_move

    def WhiteQueenMoves(self)->list[tuple[int,int]] :
        
        white_queen=self.board._bitboards[Piece.Q].value
        white_queen_move =[]
        
        mask=white_queen
        
        while mask:
            move_mask=0
            
            lsb=mask & -mask
            from_square=lsb.bit_length()-1
            
            for i in range(8): # for direction in Direction 
                ray=self.get_ray(i,from_square)
                blockers= ray & self.board._occupancy[2].value # remove w and b occupied 
                
                if (blockers==0):
                    move_mask |=ray   
                
                else:
                    
                    if(i % 2==0):
                        blocker_lsb= blockers & -blockers
                        blocker_square=blocker_lsb.bit_length()-1
        
                    else:
                        blocker_square=blockers.bit_length()-1

                    attacks= ray ^ self.get_ray(i,blocker_square)
                    move_mask |= attacks & ~self.board._occupancy[0].value
            while move_mask:
                lsb2= move_mask & -move_mask
                to_square=lsb2.bit_length()-1
                white_queen_move.append((from_square,to_square))                    
                move_mask &= move_mask - 1
                
            mask&=mask-1
                        
        return white_queen_move

    def WhiteKingMoves(self)->list[tuple[int,int]] :
        white_king=self.board._bitboards[Piece.K].value
        white_king_move =[]

        mask=white_king

        lsb=mask & -mask
        from_square=lsb.bit_length()-1

        east = (lsb & 0x7f7f7f7f7f7f7f7f) << 1   
        west = (lsb & 0xfefefefefefefefe) >> 1
        
        r=lsb|east|west 
        move_mask = ((r<<8| r |r>>8) & ~lsb) & ~self.board._occupancy[2].value
        #king capture and check will be handled together with pesudo lrgal to legal moves
        while move_mask:
            lsb2= move_mask & -move_mask
            to_square=lsb2.bit_length()-1
            white_king_move.append((from_square,to_square))
            move_mask &= move_mask - 1
        
        return white_king_move

    def BlackPawnMoves(self)->list[tuple[int,int]] :

        black_pawn=self.board._bitboards[Piece.p].value
        black_pawn_move =[]

        # single push (south = >>8)
        single_push_target=black_pawn>>8
        single_push_target&=~self.board._occupancy[2].value

        # double push (black pawns start on rank 7 = 0x00FF000000000000)
        rank_7_pawns=black_pawn & 0x00FF000000000000
        first_step = rank_7_pawns >> 8
        first_step &= ~self.board._occupancy[2].value
        double_push_targets = first_step >> 8
        double_push_targets &= ~self.board._occupancy[2].value

        mask= single_push_target | double_push_targets

        while mask:

            lsb =mask & -mask
            to_square=lsb.bit_length() -1

            if (double_push_targets >> to_square) & 1:
                from_square = to_square + 16

            else:
                from_square = to_square + 8
            black_pawn_move.append((from_square, to_square))

            mask &= mask - 1

        # capture promotion en passant left
        return black_pawn_move

    def BlackKnightMoves(self)->list[tuple[int,int]] :

        black_knight=self.board._bitboards[Piece.n].value
        black_knight_move =[]

        mask=black_knight
        while mask:

            lsb =mask & -mask
            from_square=lsb.bit_length() -1

            l1=(lsb & 0x7f7f7f7f7f7f7f7f)<< 1
            l2=(lsb & 0x3f3f3f3f3f3f3f3f)<< 2
            r1=(lsb & 0xfefefefefefefefe)>> 1
            r2=(lsb & 0xfcfcfcfcfcfcfcfc)>> 2

            h1= l1|r1
            h2= l2|r2

            moves_mask= (h1<<16) | (h1>>16) | (h2<<8) | (h2>>8)
            moves_mask &= 0xFFFFFFFFFFFFFFFF

            moves_mask&=~self.board._occupancy[1].value
            mask &= mask - 1

            while moves_mask:

                lsb2 = moves_mask & -moves_mask
                to_square=lsb2.bit_length()-1
                black_knight_move.append((from_square ,to_square))

                moves_mask &= moves_mask - 1

        return black_knight_move

    def BlackBishopMoves(self)->list[tuple[int,int]] :

        black_bishop=self.board._bitboards[Piece.b].value
        black_bishop_move =[]

        mask=black_bishop

        while mask:
            move_mask=0

            lsb=mask & -mask
            from_square=lsb.bit_length()-1

            for i in range(4,8): # NE SE NW SW only
                ray=self.get_ray(i,from_square)
                blockers= ray & self.board._occupancy[2].value

                if (blockers==0):
                    move_mask |=ray

                else:

                    if(i % 2==0):
                        blocker_lsb= blockers & -blockers
                        blocker_square=blocker_lsb.bit_length()-1

                    else:
                        blocker_square=blockers.bit_length()-1

                    attacks= ray ^ self.get_ray(i,blocker_square)
                    move_mask |= attacks & ~self.board._occupancy[1].value

            while move_mask:
                lsb2= move_mask & -move_mask
                to_square=lsb2.bit_length()-1
                black_bishop_move.append((from_square,to_square))
                move_mask &= move_mask - 1

            mask&=mask-1

        return black_bishop_move

    def BlackRookMoves(self)->list[tuple[int,int]] :

        black_rook=self.board._bitboards[Piece.r].value
        black_rook_move =[]

        mask=black_rook

        while mask:
            move_mask=0

            lsb=mask & -mask
            from_square=lsb.bit_length()-1

            for i in range(4): # NORTH SOUTH EAST WEST only
                ray=self.get_ray(i,from_square)
                blockers= ray & self.board._occupancy[2].value

                if (blockers==0):
                    move_mask |=ray

                else:

                    if(i % 2==0):
                        blocker_lsb= blockers & -blockers
                        blocker_square=blocker_lsb.bit_length()-1

                    else:
                        blocker_square=blockers.bit_length()-1

                    attacks= ray ^ self.get_ray(i,blocker_square)
                    move_mask |= attacks & ~self.board._occupancy[1].value

            while move_mask:
                lsb2= move_mask & -move_mask
                to_square=lsb2.bit_length()-1
                black_rook_move.append((from_square,to_square))
                move_mask &= move_mask - 1

            mask&=mask-1

        return black_rook_move

    def BlackQueenMoves(self)->list[tuple[int,int]] :

        black_queen=self.board._bitboards[Piece.q].value
        black_queen_move =[]

        mask=black_queen

        while mask:
            move_mask=0

            lsb=mask & -mask
            from_square=lsb.bit_length()-1

            for i in range(8): # all 8 directions
                ray=self.get_ray(i,from_square)
                blockers= ray & self.board._occupancy[2].value

                if (blockers==0):
                    move_mask |=ray

                else:

                    if(i % 2==0):
                        blocker_lsb= blockers & -blockers
                        blocker_square=blocker_lsb.bit_length()-1

                    else:
                        blocker_square=blockers.bit_length()-1

                    attacks= ray ^ self.get_ray(i,blocker_square)
                    move_mask |= attacks & ~self.board._occupancy[1].value

            while move_mask:
                lsb2= move_mask & -move_mask
                to_square=lsb2.bit_length()-1
                black_queen_move.append((from_square,to_square))
                move_mask &= move_mask - 1

            mask&=mask-1

        return black_queen_move

    def BlackKingMoves(self)->list[tuple[int,int]] :
        black_king=self.board._bitboards[Piece.k].value
        black_king_move =[]

        mask=black_king

        lsb=mask & -mask
        from_square=lsb.bit_length()-1

        east = (lsb & 0x7f7f7f7f7f7f7f7f) << 1
        west = (lsb & 0xfefefefefefefefe) >> 1

        r=lsb|east|west
        move_mask = ((r<<8| r |r>>8) & ~lsb) & ~self.board._occupancy[2].value
        # king capture and check will be handled together with pseudo legal to legal moves
        while move_mask:
            lsb2= move_mask & -move_mask
            to_square=lsb2.bit_length()-1
            black_king_move.append((from_square,to_square))
            move_mask &= move_mask - 1

        return black_king_move
