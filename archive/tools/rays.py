from enum import IntEnum

class Direction(IntEnum):

    NORTH = 0
    SOUTH = 1
    
    EAST  = 2
    WEST  = 3
    
    NE    = 4
    SE    = 5
    
    NW    = 6
    SW    = 7

class RayTable:

    # (delta_rank, delta_file) indexed by Direction value
    _VECTORS: tuple[tuple[int, int], ...] = (
    ( 1,  0),   # NORTH
    (-1,  0),   # SOUTH
    ( 0,  1),   # EAST
    ( 0, -1),   # WEST
    ( 1,  1),   # NE
    (-1,  1),   # SE
    ( 1, -1),   # NW
    (-1, -1),   # SW
    )
    # Populated at the end of this class block — one 64-int tuple per direction.
    _TABLE: tuple[tuple[int, ...], ...]

    @staticmethod
    def _build() -> tuple[tuple[int, ...], ...]:
        rows = []
        for d in range(8):
            dr, df = RayTable._VECTORS[d]
            row = []
            for sq in range(64):
                rank, file = divmod(sq, 8)
                bb = 0
                r, f = rank + dr, file + df
                while 0 <= r < 8 and 0 <= f < 8:
                    bb |= 1 << (r * 8 + f)
                    r += dr
                    f += df
                row.append(bb)
            rows.append(tuple(row))
        return tuple(rows)

    @classmethod
    def get(cls, direction: Direction | int, square: int) -> int:
        
        return cls._TABLE[int(direction)][square]

    def __class_getitem__(cls, key: tuple[int, int]) -> int:
        
        direction, square = key
        return cls._TABLE[int(direction)][square]

RayTable._TABLE = RayTable._build()
del RayTable._build
