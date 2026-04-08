*Part 6 of 17 — [← Prev: Moves & Make/Unmake](moves.md) | [Next: Evaluation →](eval.md)*

# Move Generation

## What Is Move Generation?

Move generation answers a simple question: given a position, what are all the legal moves?

This is one of the first things you implement in a chess engine. The search needs to
enumerate moves to explore; the move ordering needs to score them; perft testing needs to
count them. Everything depends on the move generator being both correct and fast.

"Correct" is the harder part. Chess has a surprising number of edge cases: en passant,
castling, pins, double checks, pawn promotions, and the notorious en passant x-ray
problem. Missing even one means the engine can play illegal moves or miss legal ones.

The move generator lives in `src/move_generator.hpp` and `src/move_generator.cpp`.

## Legal vs. Pseudo-Legal Generation

The first major design decision is whether to generate only legal moves, or to generate
all "plausible" moves and filter afterward.

### Pseudo-Legal Generation

The simpler approach. Generate all moves that respect basic piece movement rules, without
checking whether they leave the king in check. A "pseudo-legal" move like moving a pinned
bishop might expose the king to a rook — that's caught later.

The legality filter happens after: make the move, check if the king is attacked, unmake
if it is. This is straightforward to implement.

### Legal-Only Generation

The harder approach. Compute pins and checks **before** generating moves, so every move
produced is guaranteed legal. The generator is more complex (it needs to understand pin
lines and check evasion), but you never waste time making and unmaking illegal moves.

### Why Enigma Uses Legal-Only

The motivation comes from how the search works: the move selector yields moves **one at
a time**, and a beta cutoff often happens after just 1-3 moves. With legal-only
generation, those first few moves are ready immediately. With pseudo-legal, you'd still
need to make/unmake each candidate to test legality, even if you immediately cut off.

The correctness argument is simpler too. A pseudo-legal generator produces moves that
*might* be illegal, and the legality check must be bulletproof. With legal-only, the
invariant is clear: if the generator returns it, it's legal.

The downside is code complexity. The generator needs to handle pinned pieces, double
checks, en passant x-ray attacks, and more. It's about 2-3x as much code as a
pseudo-legal generator.

## The CheckInfo Structure

Before generating any moves, the engine builds a `CheckInfo` structure
(`src/move_generator.hpp:37-43`):

```cpp
struct CheckInfo {
    Bitboard pinned;        // all pinned pieces
    Bitboard pins[64];      // per-square: where can a pinned piece move?
    Bitboard checkers;      // enemy pieces giving check
    Bitboard must_cover;    // squares non-king moves must target
    Bitboard unsafe;        // squares the king cannot move to
};
```

Let's break down each field:

- **pinned**: a bitboard of all our pieces that are pinned to our king. A pinned piece
  can move, but only along the pin line.
- **pins[sq]**: for each pinned piece on square `sq`, the set of squares it can legally
  move to (its pin line, including the pinner's square for captures).
- **checkers**: a bitboard of enemy pieces currently giving check. If empty, we're not in
  check. If one bit set, single check. If two bits, double check.
- **must_cover**: when in check, non-king moves must either capture the checker or block
  the checking ray. This bitboard defines where they can go. When not in check, this is
  set to all ones (no restriction).
- **unsafe**: all squares attacked by any enemy piece. The king can't move here.

This precomputation costs a few hundred nanoseconds but saves much more by constraining
move generation upfront.

The check info is computed by `compute_check_info<S>()` (`src/move_generator.hpp:51-52`),
which is called once in the `MoveGenerator` constructor (`src/move_generator.hpp:23`).

## Pin Detection

A piece is **pinned** when moving it would expose the king to an attack from a sliding
piece behind it. Pins only exist along lines (ranks, files, diagonals), and only from
sliding pieces (bishop, rook, queen).

### How It Works

The algorithm casts a ray from the king in each of the 8 directions
(`compute_sliding_checks_and_pins<S, D>()` in `src/move_generator.hpp:55-57`):

1. Walk along the ray until hitting the first piece.
2. If it's **friendly**: walk further. If the next piece is an **enemy slider** that
   attacks along this direction (rook/queen on ranks/files, bishop/queen on diagonals),
   the friendly piece is pinned.
3. If it's an **enemy slider** of the matching type with no friendly piece in between:
   it's giving check directly.

The pin mask for a pinned piece is `LINES[king][pinner]` — the set of squares on the
line between the king and the pinner, plus the pinner's square itself. A pinned piece
can move anywhere along this line (including capturing the pinner) but nowhere else.

### Example

```
8  ·  ·  ·  ·  ·  ·  ·  ·
7  ·  ·  ·  ·  ·  ·  ·  ·
6  ·  ·  ·  ♜  ·  ·  ·  ·    Black rook on D6
5  ·  ·  ·  ·  ·  ·  ·  ·
4  ·  ·  ·  ♘  ·  ·  ·  ·    White knight on D4 — PINNED!
3  ·  ·  ·  ·  ·  ·  ·  ·
2  ·  ·  ·  ·  ·  ·  ·  ·
1  ·  ·  ·  ♔  ·  ·  ·  ·    White king on D1
   a  b  c  d  e  f  g  h
```

The knight on D4 is pinned to the king on D1 by the rook on D6. It can only move along
the D-file (D2, D3, D5, D6). Since knights can only move in L-shapes, none of those are
legal — the knight can't move at all! The pin mask is `LINES[D1][D6]`.

## Check Evasion

When in check, the rules change. Non-king pieces can only resolve the check by capturing
the checker or blocking the checking ray (if it's a slider). King moves must leave the
king safe.

### Double Check

If two pieces give check simultaneously (this requires a discovered check — one piece
moves, revealing another checker), the **only** legal response is a king move. You can't
block or capture two attackers at once.

The generator detects `popcount(checkers) == 2` and skips directly to king move generation.
This is both correct and a nice optimization: double checks are rare but produce very
constrained positions.

### Single Check

With one checker, `must_cover` is set based on the checker type:

- **Sliding checker (bishop, rook, queen)**: `must_cover = LINES[king][checker]` — capture
  the checker OR move to any square between it and the king (blocking).
- **Knight or pawn**: `must_cover = checker's square` — only capture works. You can't
  block a knight jump or a pawn attack.

Every non-king move's destination must intersect with `must_cover`.

## Unsafe Squares for the King

The `unsafe` bitboard marks all squares attacked by any enemy piece. The king can't move
to these squares.

There's an important subtlety: when computing enemy sliding piece attacks, the **king is
temporarily removed** from the occupied set. This is necessary because otherwise the king
would block the slider's ray, making it look like the square behind the king is safe.

```
8  ·  ·  ·  ·  ·  ·  ·  ·
7  ·  ·  ·  ·  ·  ·  ·  ·
6  ♜  ·  ·  ·  ♔  ·  ·  ·    Rook on A6, king on E6
5  ·  ·  ·  ·  ·  ·  ·  ·
4  ·  ·  ·  ·  ·  ·  ·  ·
3  ·  ·  ·  ·  ·  ·  ·  ·
2  ·  ·  ·  ·  ·  ·  ·  ·
1  ·  ·  ·  ·  ·  ·  ·  ·
   a  b  c  d  e  f  g  h
```

Without removing the king, E6 blocks the rook's ray — F6 appears safe. But if the king
moves to F6, the rook now attacks F6. The engine removes the king during unsafe-square
computation to prevent this error.

## Piece Move Generation

The move generator has three modes (`src/types.hpp:27-31`):

```
MGM_ALL            all legal moves
MGM_QUIET_ONLY     non-captures, non-promotions
MGM_TACTICAL_ONLY  captures and promotions
```

These let the search ask for specific move types: quiescence search only needs tactical
moves, staged move ordering generates quiets separately from captures.

### Knights, Bishops, Rooks, Queens

For these pieces, the pattern is uniform (`generate_piece_moves<S, P, M>()` in
`src/move_generator.hpp:69-70`):

1. Look up the attack bitboard (magic bitboard for sliders, precomputed table for knights)
2. Remove friendly pieces (can't capture your own)
3. Intersect with `must_cover` if in check
4. If pinned, intersect with the pin mask
5. For each remaining target square, emit a quiet or capture move

### King Moves

Similar to other pieces, but:
- Intersect with `~unsafe` instead of `must_cover` (the king can go anywhere safe)
- After removing the king from occupied, verify no x-ray attack on the destination

## Pawn Move Generation

Pawns are the most complex piece to generate moves for. They have:

- **Direction-dependent movement** (white goes north, black goes south)
- **Two different capture directions** (diagonal only)
- **Double push** from the starting rank
- **En passant capture** (the most edge-case-laden move in chess)
- **Promotion** (4 possible pieces × captures and pushes)
- All of the above interacting with pins and check evasion

The generator handles this with direction-templated code: `generate_pawn_moves<WHITE>`
and `generate_pawn_moves<BLACK>` (`src/move_generator.hpp:77-78`) use compile-time
direction constants so the compiler generates optimal code for each side without runtime
branching.

### Single Pushes

Shift all non-promotion-rank pawns one square forward, intersect with empty squares.
A pinned pawn can only push if pinned along its file (a diagonally pinned pawn can't
push forward without exposing the king).

### Double Pushes

Take the single-push results and shift one more rank. Intersect with the 4th/5th rank
and empty squares. The single push already verified the intermediate square is empty.

### Captures

Shift pawns diagonally, intersect with enemy pieces. Pinned pawns can only capture along
their pin line.

### Promotions

Pawns on the 7th/2nd rank (about to reach the back rank) generate four moves per target
square — queen, rook, bishop, knight. Both capture-promotions and push-promotions are
handled. Quiet promotions (no capture) are classified as tactical moves because they
fundamentally alter the material balance.

### The En Passant X-Ray Problem

En passant has the most notorious edge case in chess programming. Consider this position:

```
8  ·  ·  ·  ·  ·  ·  ·  ·
7  ·  ·  ·  ·  ·  ·  ·  ·
6  ·  ·  ·  ·  ·  ·  ·  ·
5  ♖  ·  ·  ♟  ♙  ·  ·  ♔    White rook on A5, black pawn on D5,
4  ·  ·  ·  ·  ·  ·  ·  ·    white pawn on E5, white king on H5
3  ·  ·  ·  ·  ·  ·  ·  ·
2  ·  ·  ·  ·  ·  ·  ·  ·
1  ·  ·  ·  ·  ·  ·  ·  ·
   a  b  c  d  e  f  g  h
```

(After black played d7-d5, en passant is available on D6.)

Neither pawn is pinned in the normal sense — there's a piece between each of them and the
rook. But en passant removes **both pawns** from the 5th rank simultaneously (the
capturing pawn moves to D6, and the captured pawn on D5 disappears), suddenly exposing
the king on H5 to the rook on A5.

The generator handles this with a specific check: for en passant captures, temporarily
remove both pawns from the occupied bitboard and test whether the king is now attacked
by a rook or queen along that rank. This only happens for en passant moves (at most 2
per position), so it doesn't affect normal pawn generation performance.

## Castling

Castling is only generated when the king is not in check
(`generate_castling_moves<S>()` in `src/move_generator.hpp:81-82`). Requirements:

1. **Rights**: the relevant castling rights bit is set (king and rook haven't moved)
2. **Clear path**: no pieces between king and rook
3. **Safe path**: the squares the king crosses are not attacked

For queenside castling, the B-file square (B1/B8) must be empty but **doesn't need to be
safe** — the king never crosses it. Only the king's path matters (D1/D8 and C1/C8 for
queenside, F1/F8 and G1/G8 for kingside). This is a common gotcha: many engines
incorrectly require B1 to be unattacked.

Castling is encoded with the `MF_CASTLE` flag (`src/types.hpp:58`). The from/to squares
are the king's movement; the rook's destination is derived during `make_move()`.

## Testing Move Generation: Perft

How do you know your move generator is correct? **Perft** (performance test) counts the
number of leaf nodes at a given depth from a position. These counts are well-known for
standard positions and can be verified against databases.

For example, from the starting position:
- Depth 1: 20 nodes (20 legal moves)
- Depth 2: 400 nodes
- Depth 3: 8,902 nodes
- Depth 5: 4,865,609 nodes

If your perft numbers match, your move generator is almost certainly correct. If they
don't, there's a bug. Enigma's perft tests are in the test suite:

```bash
./build/enigma-dev test perft
```

Perft is invaluable during development. Any time you change the move generator, run perft
to verify you haven't broken anything.

Once moves are generated, the search doesn't try them in arbitrary order — see [Move Ordering](move-ordering.md) for how the engine prioritizes which moves to search first.
