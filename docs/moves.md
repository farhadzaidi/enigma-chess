*Part 5 of 17 — [← Prev: Board State](board.md) | [Next: Move Generation →](movegen.md)*

# Moves & Make/Unmake

Moves are the interface between the search and the board. Every search node makes a
move, recurses deeper, and unmakes it. The search explores millions of nodes per second,
so moves must be cheap to encode, make, and unmake.

This doc covers how moves are represented, how the board is updated when a move is
played, and how that update is reversed. For the board state that moves operate on, see
[Board State](board.md).

## Move Encoding

Each move is packed into **16 bits** (`src/move.hpp:15-49`):

```
bits  0-5:  origin square (0-63)
bits  6-11: destination square (0-63)
bit  12:    type — 0 = quiet, 1 = capture
bits 13-15: flag — normal, en passant, castle, or promotion piece
```

Why 16 bits? Because moves get stored everywhere — the transposition table, killer move
tables, move history, move lists — and they're compared frequently. Smaller means less
memory, better cache behavior, and faster comparison.

6 bits per square (2^6 = 64, exactly enough for 64 squares), 1 bit for capture/quiet,
and 3 bits for special flags:

```cpp
// src/move.hpp:26-29
Square from()     // bits 0-5:  move & 63
Square to()       // bits 6-11: (move >> 6) & 63
MoveType type()   // bit 12:    (move >> 12) & 1
MoveFlag flag()   // bits 13-15: (move >> 13) & 7
```

### Move Flags

The flag field encodes special moves (`src/types.hpp:55-63`):

```
MF_NORMAL       = 0   regular move
MF_EN_PASSANT   = 1   en passant capture
MF_CASTLE       = 2   castling
MF_PROMO_BISHOP = 3   promotion to bishop
MF_PROMO_KNIGHT = 4   promotion to knight
MF_PROMO_ROOK   = 5   promotion to rook
MF_PROMO_QUEEN  = 6   promotion to queen
```

### What's NOT in the Encoding

The **moving piece type**, the **captured piece type**, and whether the move **gives check**
are absent. These can be derived from the board state when needed (piece map lookup, attack
computation). Storing them would require 24-32 bits, doubling the move size for information
that's often unused.

### The Null Move

`NULL_MOVE` is the zero value — from A1 to A1, no flags (`src/move.hpp:52`). It's
illegal (no piece can move to where it already is), so there's no ambiguity. Used as a
sentinel ("no move") throughout the codebase.

### MoveList

Moves are collected in a stack-allocated list (`src/move.hpp:57-100`):

```cpp
Move moves[256];  // MAX_MOVES = 256
int size = 0;
```

The actual maximum legal moves in any chess position is 218 (a constructed position), so
256 has comfortable margin. Stack allocation means no heap allocation in the hot path.

## Make and Unmake

### Why Make/Unmake?

The search explores the game tree by playing a move, recursing, and then taking the move
back. An alternative design would **copy** the board for each recursive call, but that's
too expensive — the board has hundreds of bytes of state, and copying it millions of times
per second adds up.

**Make/unmake** modifies the board in place and restores it, avoiding any allocation or
copying. This means every field of the board must be reversible: some are trivially
reversible (putting a piece back where it was), but others are destroyed by the move and
need to be explicitly saved.

### The Undo State

Before each move, an `UndoState` is pushed onto a stack (`src/board.hpp:105-118`):

```cpp
struct UndoState {
    Square en_passant_target;
    CastlingRights castling_rights;
    uint8_t halfmoves;
    Piece captured_piece;
};
```

Why these four fields?

- **En passant target** — overwritten by the new position's en passant square (or
  cleared if no double push happened). Can't be reconstructed from the move alone.
- **Castling rights** — a rook capture might revoke the opponent's rights. The move
  encoding doesn't tell you what rights existed before.
- **Halfmove clock** — reset on pawn moves and captures, incremented otherwise. You
  need the old value to restore it.
- **Captured piece** — the move says "capture" but not "capture of what." Needed to put
  the victim back.

The history stacks (`src/board.hpp:141-144`):

```cpp
int ply;
Move move_history[2048];               // MAX_GAME_PLY = 2048
UndoState state_history[2048];
ZobristHash position_hashes[2049];     // for repetition detection
```

### What make_move Does

The sequence matters for correctness (hash updates must happen in the right order, NNUE
must see the right board state). Here's the full sequence:

1. Save undo state
2. Update clocks (halfmove resets on pawn move or capture; fullmove increments after
   black's move)
3. Handle en passant target (hash out old, compute and hash in new if double pawn push)
4. Push the NNUE accumulator (save before modifying features)
5. Remove the piece from origin square (updates bitboards, piece map, hash, NNUE)
6. Remove captured piece if applicable (en passant captures from a different square
   than the destination)
7. Promote if applicable (swap the piece type)
8. Place on destination (updates bitboards, piece map, hash, NNUE)
9. Castle: move the rook (derived from the king's to-square)
10. Update castling rights (both sides — moving a rook or capturing one clears bits)
11. King move: refresh NNUE from scratch (all feature indices change with king position)
12. Toggle side to move (hash XOR)
13. Record position hash for repetition detection

### What unmake_move Does

Pop the undo state and NNUE accumulator. Undo everything in roughly reverse order. The
key insight: **unmake doesn't recalculate anything**. It doesn't recompute the hash,
doesn't re-evaluate features, doesn't scan the board. It's a pure state restore. This is
what makes make/unmake viable at millions of operations per second.

### Null Moves

Null move pruning (see [Pruning](pruning.md)) needs a "pass" move: toggle side to move
without moving a piece. `make_null_move()` toggles the side, clears the en passant target,
and updates the hash. `unmake_null_move()` reverses it. No NNUE update needed since no
features changed.

## Position Hashing

Each position has a 64-bit **Zobrist hash** — a fingerprint used by the transposition
table and for repetition detection. The hash is updated incrementally during make/unmake
with just 3-6 XOR operations per move.

For the full details of how Zobrist hashing works, see the
[Transposition Table](transposition-table.md) doc, which covers the hashing scheme,
hash components, collision risk, and how the TT uses these hashes.

## Repetition Detection

A position has repeated if its Zobrist hash appeared earlier in the game. The engine
walks backward through `position_hashes[]`, stepping by 2 (same side to move), going
back at most `halfmoves` positions (any pawn move or capture creates an irreversible
boundary). This is implemented in `has_repeated()` (`src/board.hpp:73`).

Enigma treats a single repetition during search as a draw (score 0). This is more
conservative than the official threefold repetition rule, but it prevents the engine
from walking into repetition loops.
