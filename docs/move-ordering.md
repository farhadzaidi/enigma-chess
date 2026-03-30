*Part 10 of 16 — [← Prev: Transposition Table](transposition-table.md) | [Next: Pruning & Extensions →](pruning.md)*

# Move Ordering

## Why Move Ordering Matters

As explained in [Search](search.md), alpha-beta pruning only works well when good moves
are searched first. If the best move at every node happened to be searched first,
alpha-beta would achieve its theoretical best case: examining only O(√N) nodes instead of
N. In a position with 35 legal moves at depth 12, that's the difference between examining
a few million nodes and a few trillion.

Of course, if you already knew the best move, you wouldn't need to search. The art is in
**guessing** well. Every improvement in move ordering lets the engine search deeper in the
same time. A 10% improvement in first-move hit rate can translate to an entire extra ply
of search depth, which can mean 20-50 Elo of playing strength.

This is why engines invest heavily in move ordering heuristics — killer moves, history
tables, countermove tables, SEE. The search and pruning get all the attention, but move
ordering is arguably where the most Elo is hiding.

## Staged Generation

A naive approach would generate all legal moves, score them, sort them, and iterate.
Enigma does something smarter: **staged generation**, where moves are produced in batches
from most promising to least.

Each stage is only computed when the previous stage is exhausted. If a beta cutoff
happens on the TT move (stage 2), you never generate captures, killers, or quiet moves.
In a well-ordered tree, most nodes cut off early, so later stages are rarely reached.

The staged move selector is the `MoveSelector` class, defined in `src/engine.cpp`. The
stages are defined in `src/types.hpp:40-48`:

```
MSP_PREV_BEST    stage 1: previous iteration's best (root only)
MSP_TT           stage 2: transposition table move
MSP_TACTICAL     stage 3: winning/equal captures & promotions
MSP_KILLER       stage 4: killer moves
MSP_COUNTERMOVE  stage 5: countermove
MSP_QUIET        stage 6: remaining quiet moves
MSP_BAD_CAPTURE  stage 7: losing captures
```

Let's walk through each stage.

### Stage 1: Previous Best (Root Only)

At the root, the best move from the previous iteration is tried first. This is almost
always the best move at the next depth too — iterative deepening is remarkably stable.
The best move at depth 11 is the best move at depth 12 roughly 90% of the time.

### Stage 2: TT Move

The transposition table (see [Transposition Table](transposition-table.md)) stores the
best move found the last time this position was searched. Even when the TT entry's depth
is too shallow for a score cutoff, the **move itself** is an excellent first guess.

TT moves produce a first-move hit rate of 85-95% in typical positions. This alone
accounts for most of alpha-beta's pruning power.

### Stage 3: Winning/Equal Captures

Generate all tactical moves (captures + promotions), evaluate each with SEE (static
exchange evaluation — see below), and return the ones with SEE >= 0 (captures that don't
lose material).

These are scored by **MVV-LVA** (Most Valuable Victim, Least Valuable Attacker): a pawn
capturing a queen is more promising than a queen capturing a queen. You want to capture
the most valuable enemy piece with the least valuable friendly piece, because that
maximizes material gain while minimizing risk.

Promotion bonuses are added, with queen promotion weighted highest.

### Stage 4: Killer Moves

**Killers** are quiet moves that caused a beta cutoff at the same ply in a recently
searched sibling node. The idea: if the opponent tries different moves and your same
response keeps causing cutoffs, that response is probably good here too.

Two slots per ply, FIFO replacement. When a new killer arrives, the old first killer
shifts to the second slot. Killer moves are stored per-thread and per-ply in the search
context (`src/engine.hpp:115-116`):

```
Move killer_1[256]   first killer per ply (max 256 plies)
Move killer_2[256]   second killer per ply
```

Why two slots and not more? Empirically, the first killer hits most of the time. The
second is a fallback. Adding a third provides diminishing returns.

### Stage 5: Countermove

The **countermove** is the quiet move that historically refuted the opponent's last move.
Stored in a table indexed by `[piece_type][destination]` (`src/engine.hpp:118`):

```
Move countermoves[6][64]   indexed by [piece_type][to_square]
```

"When they moved their knight to F3, what did I usually play in response?"

This captures a different pattern than killers. Killers are specific to the ply (same
depth in the tree). Countermoves are specific to the opponent's preceding move regardless
of depth.

### Stage 6: Remaining Quiets

All quiet moves not yet returned, scored by a combined **history score** (see below) and
sorted. This is the bulk of moves in most positions — 20-30 quiet moves, of which maybe
5-10 have already been returned by earlier stages.

### Stage 7: Bad Captures

Captures with SEE < 0 — exchanges that lose material. Tried last because they're usually
bad. But "almost never" isn't "never" — sacrificial combinations exist, and the search
needs to be able to find them.

### Deduplication

Moves returned by earlier stages might appear again in later stages (the TT move is also
a capture, a killer is also the countermove, etc.). The selector tracks returned moves
and skips duplicates.

## History Heuristic

The history heuristic is the most important move ordering mechanism for **quiet moves**.
The idea is simple: if a move causes beta cutoffs across many different positions, it's
probably a good move in general.

A knight centralizing to E5, a bishop developing to C4 — these moves tend to be good
regardless of the specific position. History tracks this pattern.

### Three History Tables

Enigma maintains three tables that track cutoff success from different perspectives
(`src/engine.hpp:110-113`):

**Side-piece-to** — how often does this side moving this piece type to this square cause
a cutoff? 768 entries. Captures universal patterns like "white knights going to E5 are
usually good."

```
int32 side_piece_to_history[2][6][64]    indexed by [side][piece][to_square]
```

**From-to** — indexed by the square pair rather than piece type. 4096 entries. Captures
positional patterns tied to specific squares: "moving from G8 to F6 is usually good"
(developing a knight).

```
int32 from_to_history[64][64]            indexed by [from_square][to_square]
```

**Continuation** — conditions on the opponent's last move. "After they played knight to
F3, how often does our bishop to C5 cause a cutoff?" Captures move-pair dependencies.

```
int32 continuation_history[6][64][6][64]
    indexed by [prev_piece][prev_to][our_piece][our_to]
```

The combined score for a quiet move is the sum of all three lookups.

### How History Updates Work

On a beta cutoff by a quiet move (`handle_beta_cutoff()` in `src/engine.hpp:233`):

- **Bonus**: the cutoff move gets `depth²` added to all three tables
- **Malus**: all quiet moves tried before the cutoff get `-(depth² / 2)` — penalizing
  moves that failed to produce a cutoff

The quadratic scaling means deeper cutoffs carry much more weight. A cutoff at depth 10
adds 100 to the bonus; at depth 2, only 4. Deeper cutoffs are stronger evidence.

### Bounded Blending

History values stay within bounds using a blending formula:

```
score += bonus - score × |bonus| / MAX_MOVE_SCORE
```

Two nice properties:

1. **Soft cap**: as a score approaches its bound, the effective update shrinks. Prevents
   runaway accumulation.
2. **Implicit decay**: old values are diluted by new updates. History naturally favors
   recent information without explicit aging.

## Static Exchange Evaluation (SEE)

SEE answers the question: "if I capture on this square, and they recapture, and I
recapture... who comes out ahead?" It's a material-only analysis of a capture sequence
on a single square.

### How It Works

Think of it like a sequence of trades on one square:

1. I capture their queen with my knight (+9 for the queen)
2. They recapture my knight with a pawn (+3 for them, net = +9-3 = +6 for me)
3. I recapture their pawn with my bishop (+1 for me, net = +6+1-3 = +4 for me, but I
   risk losing my bishop)
4. ...and so on until one side runs out of attackers or decides to stop

At each step, the capturing side can choose to **stop** (keeping the current balance) or
**continue** (risking the attacker but potentially winning more). The sequence is
minimax'd backward to find the actual result.

### X-Ray Attacks

When a piece captures and is removed from the board, a slider behind it might now see
the target square. A pawn on E4 blocking a bishop on C2 — if the pawn captures on D5,
the bishop now attacks D5. SEE recomputes attackers after each removal.

### Where SEE Is Used

- **Move ordering**: good captures (SEE >= 0) tried early, bad captures (SEE < 0) tried
  last
- **SEE pruning in search**: at shallow depth, bad captures are skipped entirely
- **Quiescence search**: captures below a tunable SEE threshold are skipped
