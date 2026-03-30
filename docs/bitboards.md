*Part 3 of 16 — [← Prev: Bit Manipulation](bit-manipulation.md) | [Next: Board State →](board.md)*

# Bitboards

## What Is a Bitboard?

A bitboard is a 64-bit integer where each bit represents one square on the chess board.
That's it. A `uint64_t` has exactly 64 bits, and a chess board has exactly 64 squares —
a perfect fit.

If a bit is set (1), that square is "in the set." If it's clear (0), it's not. A bitboard
can represent any set of squares: all the squares occupied by white pawns, all the squares
a knight can reach from E4, the entire third rank, or any other grouping you can think of.

Here's what the starting position's white pawns look like as a bitboard:

```
  A B C D E F G H
8 . . . . . . . .    bit 63 ← H8
7 . . . . . . . .
6 . . . . . . . .
5 . . . . . . . .
4 . . . . . . . .
3 . . . . . . . .
2 1 1 1 1 1 1 1 1    ← bits 8-15
1 . . . . . . . .    bit 0 ← A1

= 0x000000000000FF00  (in hex)
= 65280               (in decimal)
```

Every "1" means "there's a white pawn here." Every "." is a 0.

In code, this is simply (`src/types.hpp:14`):

```cpp
using Bitboard = uint64_t;
```

## How Squares Map to Bits

The mapping is simple: bit 0 is A1, bit 1 is B1, ..., bit 7 is H1, bit 8 is A2, and
so on up to bit 63 which is H8. This is **row-major from white's perspective** — you go
left to right across each rank, then up to the next rank.

The formula is:

```
bit index = rank × 8 + file
```

where rank 0 = first rank (white's back rank) and file 0 = A-file.

In code, a square is just a small integer (`src/types.hpp:18`):

```cpp
using Square = uint8_t;
```

All 64 squares are enumerated in `src/types.hpp:65-74`:

```
A1=0,  B1=1,  C1=2,  D1=3,  E1=4,  F1=5,  G1=6,  H1=7,
A2=8,  B2=9,  ...
...
A8=56, B8=57, C8=58, D8=59, E8=60, F8=61, G8=62, H8=63
```

To convert between squares, ranks, and files (`src/square.hpp:9-11`):

```cpp
get_square(rank, file)  →  rank * 8 + file
get_rank(square)        →  square / 8
get_file(square)        →  square % 8
```

To get a single-bit bitboard for a given square (`src/bitboard.hpp:47`):

```cpp
get_mask(square)  →  1ULL << square
```

So `get_mask(E4)` produces a `uint64_t` with only bit 28 set — a bitboard containing
just the square E4.

## Why Bitboards?

The alternative is a **mailbox** representation: an array of 64 squares, each storing
what piece (if any) is on it. It's intuitive and easy to implement:

```cpp
// Mailbox approach
Piece board[64];
// board[E4] = KNIGHT
```

But answering questions like "which of my pieces are attacked?" requires looping over
squares one at a time. With bitboards, the same question becomes one or two CPU
instructions.

For example: "which white pawns are on the fourth rank?"

```cpp
// Bitboard approach: a single AND instruction
Bitboard result = white_pawns & RANK_4_MASK;

// Mailbox approach: a loop checking 8 squares
for (int file = 0; file < 8; file++) {
    if (board[get_square(RANK_4, file)] == WHITE_PAWN) { ... }
}
```

Bitboards are faster because CPUs are designed to manipulate 64-bit integers in a single
clock cycle. AND, OR, NOT, XOR, shift — these all take one cycle. What would be a loop in
mailbox representation becomes a single instruction with bitboards.

Modern CPUs also have hardware instructions for the operations bitboards need most:

- `popcount` — count how many bits are set (e.g., "how many white pawns are there?")
- `ctz` / `clz` — find the first/last set bit (e.g., "where is the first white pawn?")
- `pext` — extract bits at specific positions (used for magic bitboard lookups)

These turn what would be multi-step operations into single-cycle instructions.

The tradeoff: bitboards make set operations fast but individual square lookups need a bit
extraction. That's why most engines keep **both** representations: bitboards for fast set
operations, plus an array-based piece map for O(1) "what's on this square?" lookups.
Enigma does this too — see [Board State](board.md).

## How Enigma Stores Bitboards

Enigma stores the following bitboards (defined in `src/board.hpp:122-127`):

```
Bitboard pieces[2][6];   // 12 bitboards: one per (side, piece type)
Bitboard sides[2];       // 2 bitboards:  all pieces for each side
Bitboard occupied;       // 1 bitboard:   all pieces on the board
```

That's 15 bitboards total:

- **12 piece bitboards** — `pieces[side][piece]`. One per (side, piece type) pair.
  `pieces[WHITE][KNIGHT]` is a bitboard of all squares with white knights.
- **2 side bitboards** — `sides[WHITE]` and `sides[BLACK]`. The union (OR) of a side's
  six piece bitboards. Tells you where all of one side's pieces are.
- **1 occupancy bitboard** — `occupied`. The union of both sides. Tells you which
  squares have any piece on them.

Sides and pieces are small integers used as array indices (`src/types.hpp:115-128`):

```
WHITE = 0, BLACK = 1
PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5
```

## Core Operations

All bitboard operations live in `src/bitboard.hpp` and are `constexpr`, so the compiler
resolves what it can at compile time.

### Finding Pieces (Iterating Over Set Bits)

The most common operation is iterating over all set bits in a bitboard — for example,
looping over all white knights to generate their moves.

The `pop_lsb` function extracts and clears the lowest set bit (`src/bitboard.hpp:57-61`):

```cpp
Square pop_lsb(Bitboard& b) {
    Square sq = countr_zero(b);   // hardware CTZ: index of lowest set bit
    b &= b - 1;                    // carry-ripple trick: clears that bit
    return sq;
}
```

**How the carry-ripple trick works:** `b - 1` flips all bits below and including the
lowest set bit. So `b & (b - 1)` zeroes exactly that bit. This compiles to a single
`blsr` instruction on x86.

Example: iterating over all white knights to print their squares:

```cpp
Bitboard knights = pieces_[WHITE][KNIGHT];
while (knights) {
    Square sq = pop_lsb(knights);
    // sq is now the square of the next white knight
    // knights has had that bit cleared, so the loop continues to the next one
}
```

There's also `pop_msb` for the highest bit (`src/bitboard.hpp:64-68`), and a
direction-aware `pop_next<D>` (`src/bitboard.hpp:71-77`) that picks LSB or MSB depending
on traversal direction — when generating moves northward, starting from the lowest bit
(A1 side) is natural; when going south, start from the top.

### Shifting (Moving Bits in a Direction)

Moving all bits one step in a direction is a bit shift. North means higher ranks, which
means higher bit indices, so shifting north is `b << 8`. South is `b >> 8`.

But there's a catch: the board has edges, and bit shifts don't know about them. A bit on
the H-file (bit 7) shifted left by 1 becomes bit 8 — which is the A-file of the *next
rank*. A piece on H1 would "wrap around" to A2. Horizontal shifts need **masking** to
prevent this:

```
NORTH:     b << 8
SOUTH:     b >> 8
EAST:      (b << 1) & NOT_A_FILE    // mask off the A-file
WEST:      (b >> 1) & NOT_H_FILE    // mask off the H-file
NORTHEAST: (b << 9) & NOT_A_FILE    // north + east
```

The masks (`src/bitboard.hpp:43-44`) filter out wraparound bits:

```
NOT_A_FILE = ~(A-file mask)    all bits except the A-file
NOT_H_FILE = ~(H-file mask)    all bits except the H-file
```

The shift function is templated on direction, so the compiler picks the right variant at
compile time with zero runtime cost (`src/bitboard.hpp:80-94`):

```cpp
Bitboard shift<Direction>(Bitboard b) {
    NORTH: return b << 8;
    SOUTH: return b >> 8;
    EAST:  return (b << 1) & NOT_A_FILE;
    WEST:  return (b >> 1) & NOT_H_FILE;
    // ... and so on for diagonals
}
```

### Rank and File Masks

Precomputed masks for each rank and file (`src/bitboard.hpp:16-41`):

```
rank_mask(rank) →  0xFF << (rank * 8)          8 bits on the given rank
file_mask(file) →  0x0101010101010101 << file   1 bit per rank on the given file
```

`rank_mask(RANK_4)` returns a bitboard with all 8 bits on the fourth rank set.
`file_mask(E_FILE)` returns a bitboard with one bit on each rank of the E-file.

## Non-Sliding Piece Attacks

Knight, king, and pawn attacks depend only on the origin square — there are no blockers
involved (a knight jumps over pieces, a king moves one square, pawns attack diagonally
regardless of what's in the way). All three are precomputed into **lookup tables** at
startup in `src/move_generator.cpp`.

### Knight Attacks

A knight can reach up to 8 squares from any position (the L-shaped jumps). For each of
the 64 squares, a bitboard is precomputed with all the squares that knight can attack.

The computation shifts the square's bitmask in the 8 L-shaped patterns (e.g., north-north
then east, north-north then west, etc.) and OR's all results together. The `NOT_A_FILE` /
`NOT_H_FILE` masks prevent wraparound.

Looking up "what does a knight on D4 attack?" is a single array read — O(1), no
computation needed at search time.

### King Attacks

Same idea as knight attacks, but for the 8 adjacent squares (including diagonals).
Precomputed for all 64 squares.

### Pawn Attacks

Pawns are direction-dependent (white attacks diagonally *forward*, black attacks
diagonally *backward*), so the table is `[2 sides][64 squares]`. The lookup function
is `get_pawn_attacks(side, square)` (`src/move_generator.hpp:11`).

**Important:** pawn attacks and pawn moves are different things. The attack tables only
store the diagonal capture squares. Pawn pushes (moving forward) are handled separately
in move generation.

These tables are tiny (64 entries each, one `uint64_t` per entry) and fit comfortably in
L1 cache.

## Sliding Piece Attacks (Magic Bitboards)

Sliding pieces (bishops, rooks, queens) are the hard case. A rook on D4 might be able to
reach D8, or it might be blocked by a piece on D6 — the answer depends on what's in the
way. You can't just precompute a single table indexed by square alone.

This is the biggest challenge in bitboard-based move generation, and the solution — magic
bitboards — is one of the most elegant tricks in chess programming.

### The Problem

A rook on D4 can move along the D-file and the 4th rank, but it stops when it hits
another piece (friend or foe). The set of squares it can actually reach depends on the
**occupancy** of the board — specifically, which squares along its movement lines have
pieces on them.

For a single square, the number of possible occupancy configurations is 2^N where N is
the number of relevant squares on the rook's lines. That's potentially thousands of
configurations, each producing a different attack bitboard.

### Blocker Masks

For each square, we define the **blocker mask**: the set of squares where a blocker would
affect the piece's movement. For a rook on D4, that's the rest of the D-file and the rest
of the 4th rank — but **not the edge squares**. A rook always stops at the board edge, so
whether there's a piece on D8 doesn't change the attack set (the rook reaches D8 either
way). Excluding edges reduces the number of relevant bits, which shrinks the lookup table.

A rook on D4 might have 10 relevant blocker squares. That means 2^10 = 1024 possible
blocker configurations, each with a potentially different attack bitboard. Corner rooks
have as few as 2^12 entries; center bishops might have 2^9.

### The Magic Trick

Here's the problem: you have a 64-bit blocker configuration and need to turn it into a
table index. The blocker bits aren't contiguous — they're scattered across the 64-bit
word at the positions of the relevant squares.

The classic solution multiplies by a **magic number** and shifts right:

```
index = (blockers * magic) >> (64 - num_relevant_bits)
```

The magic number is chosen so that this mapping is **collision-free**: different blocker
configurations always produce different indices. Finding these magic numbers is done by
brute-force search during development — you try random numbers until you find one that
works. The magic numbers for all 128 squares (64 bishop, 64 rook) are precomputed and
stored in `src/data/magics.hpp`.

In practice, getting the attack bitboard for a rook looks like:

```cpp
Bitboard attacks = ROOK_ATTACK_TABLE[square][(occupied & blocker_mask) * magic >> shift];
```

This is a handful of instructions — one AND, one multiply, one shift, one table lookup —
regardless of how many blockers exist.

### PEXT: The Modern Alternative

On CPUs with BMI2 (most Intel chips from ~2013 onward), the `PEXT` instruction extracts
the bits at positions specified by a mask into contiguous low-order bits. This does exactly
what we need — extract the relevant blocker bits into a dense index — without any
multiplication. It's simpler and sometimes faster, but not all CPUs support it (notably,
AMD CPUs before Zen 3 had a slow PEXT), so Enigma falls back to magic multiplication when
PEXT isn't available.

The BMI2 support is detected at compile time (`src/bitboard.hpp:5-7`):

```cpp
#ifdef __BMI2__
#include <immintrin.h>
#endif
```

### Table Construction

At startup, `src/move_generator.cpp` builds the attack tables by enumerating all possible
blocker subsets for each square. The subset enumeration uses the **Carry-Ripple trick**:
starting from the full blocker mask, `subset = (subset - 1) & mask` generates every
subset in decreasing order. For each subset, walk each ray direction until you hit a
blocker (or the board edge) to compute the actual attack bitboard, then store it at the
magic-hashed index.

The resulting tables are roughly 5k entries for bishops and 100k for rooks. **Queens don't
get their own table** — a queen's attacks are the union of bishop and rook attacks from
the same square, so you get them for the cost of two lookups.

The template function `get_piece_attacks<P>(square, occupied)` in
`src/move_generator.hpp:14-15` handles the dispatch:

```cpp
template <Piece P>
Bitboard get_piece_attacks(Square from, Bitboard occupied);
```

For knights and kings, this ignores the `occupied` parameter and returns the precomputed
table. For bishops and rooks, it does the magic lookup. For queens, it combines bishop +
rook results.

## Rays and Lines

For pin detection and check evasion, the engine precomputes two more tables (built at
startup in `src/move_generator.cpp`):

### Ray Maps

8 directions × 64 squares. `NORTH_RAY_MAP[D4]` gives all squares north of D4 in a
straight line, extending to the board edge. These are used to cast rays from the king and
find what's along each line.

For example, to check if a rook on D7 is pinning a piece to a king on D1, you'd look at
`NORTH_RAY_MAP[D1]` and see if any friendly piece is between D1 and D7, with the rook
behind it.

### Line Map

A 64×64 table. `LINES[A][B]` gives the segment between two aligned squares A and B
(including B, excluding A). If A and B are not on the same rank, file, or diagonal, it's
zero.

Lines are critical for two things:

1. **Pin masks**: a pinned piece can only move along the line between its king and the
   pinner. `LINES[king][pinner]` gives exactly that constraint.
2. **Check blocking**: when a slider gives check, non-king pieces can block by moving to
   any square on the line between king and checker.

The 64×64 line table is 32 KB (4096 × 8 bytes) — not tiny, but it's accessed frequently
enough to stay in cache during move generation.

## Putting It All Together

With bitboards, answering complex chess questions becomes a sequence of fast bit
operations. Here's a taste of how different parts of the engine use them:

**"Is the white king in check?"** — OR together attack bitboards of all black pieces,
AND with the white king's square mask. If nonzero, the king is attacked.

**"Where can this knight move?"** — look up the precomputed attack table, AND out
friendly pieces (can't capture your own), AND with `must_cover` if in check.

**"Which pawns can push forward?"** — shift all white pawn bits north by 8, AND with
the complement of `occupied` (only empty squares).

**"Is this capture winning material?"** — SEE (Static Exchange Evaluation) uses
`attackers_to()` which combines magic lookups for sliders and table lookups for knights
and pawns.

Every one of these operations takes a few nanoseconds. Over millions of nodes per second,
those nanoseconds add up — which is exactly why bitboards are the representation of
choice for competitive chess engines.
