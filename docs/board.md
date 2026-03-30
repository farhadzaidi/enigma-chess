# Board State

The board is the engine's model of reality. Every other system — move generation, search,
evaluation — depends on it being fast, correct, and cheap to update.

The full board implementation lives in `src/board.hpp` and `src/board.cpp`. For how moves
are encoded and how the board is updated during search (make/unmake), see
[Moves & Make/Unmake](moves.md).

## What the Engine Needs to Know

To fully describe a chess position, you need more than just where the pieces are. The
engine tracks all of the following:

| State | Why it matters | Where in code |
|-------|---------------|---------------|
| Piece positions | Where is everything? | `pieces_`, `sides_`, `occupied_` (`src/board.hpp:122-127`) |
| Side to move | Whose turn is it? | `to_move_` (`src/board.hpp:133`) |
| Castling rights | Can each side still castle? | `castling_rights_` (`src/board.hpp:134`) |
| En passant target | Did a pawn just double-push? | `en_passant_target_` (`src/board.hpp:135`) |
| Halfmove clock | Moves since last pawn move or capture (50-move rule) | `halfmoves_` (`src/board.hpp:136`) |
| Fullmove counter | Total full moves played | `fullmoves_` (`src/board.hpp:137`) |
| Position hash | 64-bit fingerprint for the transposition table | `position_hash_` (`src/board.hpp:125`) |
| Game phase | Material-based phase for time management | `game_phase_` (`src/board.hpp:129`) |

All of this is encoded in a standard format called **FEN** (Forsyth-Edwards Notation).
The starting position in FEN is:

```
rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
```

Each section: piece placement (ranks 8 to 1), side to move (`w`), castling rights
(`KQkq`), en passant target (`-` = none), halfmove clock (`0`), fullmove number (`1`).

The `load_from_fen()` method (`src/board.hpp:50`) parses this string and initializes
the entire board state.

## Board Representation

Before diving into how Enigma represents the board, it's worth understanding the simpler
approaches that most engines start with. Enigma uses bitboards, but they're not the only
option — and they're not where you'd begin if you were writing your first engine.

### Mailbox (8x8 Array)

The most intuitive representation is just an array of 64 squares:

```cpp
Piece board[64];
// board[E4] = WHITE_KNIGHT
// board[D7] = BLACK_PAWN
// board[A3] = NO_PIECE
```

Each slot stores what piece (if any) is on that square. Point queries are instant:
"what's on E4?" is a single array read. Making a move is straightforward — copy the
piece from one slot to another, clear the old one. It's easy to understand, easy to
debug, and perfectly functional.

The downside shows up in set queries. "Which squares have white knights?" means scanning
all 64 entries. "Are any of my pieces attacked?" means looping over every piece and
computing its attackers individually. These questions come up thousands of times per
second during search, and the cost adds up.

Some engines use a **10x8** or **12x10** padded mailbox to simplify bounds checking —
off-board squares are marked as sentinels, so move generation doesn't need explicit edge
checks. See the [Chess Programming Wiki](https://www.chessprogramming.org/Mailbox) for
details.

### Bitboards

The approach most competitive engines use. A single `uint64_t` represents a set of
squares — one bit per square, 64 bits for 64 squares. Set queries that require loops
with a mailbox become single CPU instructions with bitboards: AND for intersection, OR
for union, popcount for "how many?".

The tradeoff is that point queries ("what piece is on E4?") are awkward — you'd need to
check up to 12 bitboards. That's why most engines keep both representations.

See [Bitboards](bitboards.md) for the full explanation of how they work.

## Piece Representation

No single representation is good at everything. Mailbox is fast for point queries,
bitboards are fast for set operations. So Enigma keeps **three parallel views** of where
pieces are, each optimized for different kinds of queries. They must always stay in sync.

### Bitboards (the primary representation)

Twelve 64-bit integers, one per (side, piece type) pair (`src/board.hpp:122`):

```cpp
// 2 sides × 6 piece types = 12 bitboards
Bitboard pieces[2][6];
```

You can answer "where are all the white knights?" by reading a single 64-bit word:
`pieces[WHITE][KNIGHT]`. You can answer "what does white control?" by OR'ing all six
piece bitboards into `sides[WHITE]`. Set intersection ("which white pieces are on the
fourth rank?") is a single AND.

See [Bitboards](bitboards.md) for how these work.

### Piece Map (for point queries)

A byte array indexed by square (`src/board.hpp:124`):

```cpp
Piece piece_map[64];  // what piece is on each square
```

`piece_map[E4]` returns `KNIGHT` (or `NO_PIECE` if the square is empty). This is O(1)
and used constantly: scoring captures (what was captured?), updating the hash (which piece
type moved?), and SEE calculation.

The tradeoff: bitboards are great for set operations but awkward for point queries. To
find what piece is on E4 using only bitboards, you'd need to check up to 12 bitboards.
The piece map makes it O(1).

### King Squares (cached separately)

```cpp
Square king_squares[2];  // src/board.hpp:126
```

Nearly everything needs the king position. Move generation computes pins and checks
relative to the king. The NNUE uses the king square as part of every feature index.
Caching it avoids constantly extracting it from the king bitboard.

### Keeping Everything in Sync

Every `place_piece` and `remove_piece` call (`src/board.hpp:152-153`) updates all three
representations, plus the Zobrist hash, the game phase counter, and optionally the NNUE
accumulator. This bookkeeping is the cost of redundant representation — but computing
derived data on the fly would be far more expensive given how often each view is accessed.

## Representing Sides and Pieces

Sides and pieces are small integers used as array indices everywhere (`src/types.hpp`):

```cpp
// src/types.hpp:115-119
WHITE    = 0
BLACK    = 1
NO_SIDE  = 2   // sentinel

// src/types.hpp:121-129
PAWN     = 0
KNIGHT   = 1
BISHOP   = 2
ROOK     = 3
QUEEN    = 4
KING     = 5
NO_PIECE = 6   // sentinel
```

A useful trick: `opposite_side(side)` is just `side ^ 1` (`src/square.hpp:7`), which
flips WHITE ↔ BLACK with a single XOR.

## Castling Rights

Castling rights are stored as a 4-bit bitmask (`src/types.hpp:131-137`):

```
WHITE_SHORT  = 0b0001   (bit 0 — white kingside)
WHITE_LONG   = 0b0010   (bit 1 — white queenside)
BLACK_SHORT  = 0b0100   (bit 2 — black kingside)
BLACK_LONG   = 0b1000   (bit 3 — black queenside)
```

You can check if white can castle kingside with `castling_rights & WHITE_SHORT`. You can
revoke a right with `castling_rights &= ~WHITE_SHORT`. Both are single-instruction
operations.

Rights are revoked when:
- A king moves (loses both rights for that side)
- A rook moves from its starting square (loses the right for that corner)
- A rook is captured on its starting square (the opponent's rook is gone)

## Game Phase

A simple sum of non-pawn, non-king material (`src/board.hpp:15-23`):

```
knight = 1, bishop = 1, rook = 2, queen = 4
pawns = 0, kings = 0
```

With all starting pieces:

```
(2 knights + 2 bishops + 2 rooks + 1 queen) × 2 sides
= (2×1 + 2×1 + 2×2 + 1×4) × 2
= 12 × 2 = 24
```

The phase starts at 24 and decreases toward 0 as pieces are captured. Time management
uses it to estimate how many moves remain (more material = more moves expected). It's
updated incrementally: `place_piece` adds, `remove_piece` subtracts.

Pawns are excluded because pawn trades don't change the character of a position the way
piece trades do. A position with all minor pieces but no pawns is still a complex
middlegame; a position with all pawns but no pieces is a quiet endgame.
