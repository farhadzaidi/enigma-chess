*Part 8 of 17 — [← Prev: Evaluation](eval.md) | [Next: Transposition Table →](transposition-table.md)*

# Search

## What Is Search?

At its core, a chess engine does one thing: given a position, find the best move. But how?

You can't just look at the current position and pick a move — chess is too complex for
that. A move that looks good right now might walk into a trap two moves later. To play
well, you need to **look ahead**: consider your move, then the opponent's response, then
your reply, and so on. The further ahead you look, the better your move choice.

This "looking ahead" is called **search**. The engine builds a **game tree** — a branching
structure of all possible move sequences — and evaluates the positions at the leaves to
figure out which initial move leads to the best outcome.

The search is the heart of the engine. Everything else (board representation, move
generation, evaluation) exists to support it. The search implementation lives in
`src/engine.hpp` and `src/engine.cpp`.

## The Game Tree

Imagine it's white's turn. White has, say, 30 legal moves. For each of white's moves,
black has about 30 responses. For each of those, white has another 30 moves. The tree
branches explosively:

```
Depth 1:  30 positions (white's moves)
Depth 2:  30 × 30 = 900 positions
Depth 3:  30 × 30 × 30 = 27,000 positions
Depth 6:  30^6 ≈ 729,000,000 positions
Depth 12: 30^12 ≈ 500 trillion positions
```

Clearly, you can't examine every node at depth 12. The game tree is simply too big. The
entire field of chess engine search is about **examining as little of this tree as possible
while still finding the best move.**

## Minimax: The Basic Idea

Chess is a **zero-sum game**: what's good for one side is bad for the other. If you could
search the entire tree, you'd use **minimax**: at your turns, pick the move that
**maximizes** your score; at the opponent's turns, assume they pick the move that
**minimizes** your score (maximizes theirs).

Here's a tiny example with 2 moves per side and depth 2:

```
              Root (White to move)
             /                    \
        Move A                  Move B
       (Black)                 (Black)
      /       \              /        \
   A-1       A-2          B-1        B-2
  score=+3  score=+1     score=+5   score=-2
```

Black minimizes, so:
- After Move A: black picks A-2 (score +1, better for black than +3)
- After Move B: black picks B-2 (score -2, much better for black)

White maximizes, so white picks Move A (guaranteed +1) over Move B (where black can
force -2). The minimax value of the root is +1.

The **evaluation function** provides the scores at the leaf nodes (the bottom of the
tree). More on that in [Evaluation](eval.md).

### Negamax

In practice, nobody implements minimax with separate "maximize" and "minimize" functions.
Instead, engines use **negamax**, a reformulation where both sides maximize — but the
score is negated on recursion:

```
score = -search(opponent's position)
```

This works because chess is zero-sum: my +3 is your -3. Negating the score on each
recursion means every call maximizes from its own perspective. The code is simpler (one
function instead of two) and the compiler has one recursive function to optimize instead
of two alternating ones.

Enigma's main search function is `negamax()` in `src/engine.hpp:188-196`:

```cpp
PositionScore negamax(
    Board& board,
    Context& ctx,
    SearchDepth depth,
    PositionScore alpha,
    PositionScore beta,
    bool allow_null_move = true,
    Move excluded_move = NULL_MOVE
);
```

## Alpha-Beta Pruning

Minimax examines every node in the tree. But many of those nodes are a waste of time.
**Alpha-beta pruning** is the insight that lets us skip large portions of the tree while
still finding the exact same best move.

### The Key Insight

Consider searching Move A first, and finding it scores +1. Now you start searching
Move B. While examining B's subtree, you discover that one of black's responses leads to
a score of -2. You don't need to look at black's other responses — you already know that
Move B can lead to -2 (or worse), so Move A (+1) is better regardless.

More formally, the engine maintains two bounds:

- **Alpha**: the best score the searching side can guarantee so far. "I can do at least
  this well."
- **Beta**: the threshold above which the opponent would have avoided this position.
  "If I find something this good, the opponent would never have let me get here."

If a move produces a score >= beta, the opponent would never have allowed us to reach this
node. We can stop searching (**a beta cutoff**) without examining the remaining moves.

### Worked Example

Let's trace alpha-beta through the same tree from the minimax example:

```
              Root (White to move)
         alpha=-inf, beta=+inf
             /                \
        Move A              Move B
       (Black)             (Black)
      /       \           /        \
   A-1       A-2       B-1        B-2
  score=+3  score=+1  score=+5   score=-2
```

**Searching Move A:**

1. Enter Move A's subtree. Black is minimizing, so we flip: black's alpha=-inf, beta=+inf.
2. Evaluate A-1: score +3. Black's best so far = +3 (from black's view this is -3; from
   white's view, black can hold white to +3 at most here).
3. Evaluate A-2: score +1. Better for black (+1 < +3). Black's best = +1.
4. Move A's subtree complete. Black picks +1. Back at root: alpha = +1.

**Searching Move B:**

5. Enter Move B's subtree. Black's alpha=-inf, beta=-1 (white's alpha of +1 becomes
   black's beta of -1 — white already has +1, so black needs to score below -1 from
   black's perspective to make Move B interesting).
6. Evaluate B-1: score +5. From black's view that's -5. Black's best so far = -5.
   -5 < -1 (beta), so no cutoff yet.
7. Evaluate B-2: score -2. From black's view that's +2 (negate for negamax).
   +2 >= -1 (beta) — **beta cutoff!** Remember, black's beta of -1 is white's
   alpha of +1 negated. Black scoring +2 here means white would get -2, which is
   worse than the +1 white already has from Move A. White would never choose
   Move B.

**Result:** We skipped nothing in this tiny tree (B-2 was the last move anyway), but in
a real tree, Move B might have 30 more children — all skipped. The cutoff after B-2
means we never examine B-3 through B-30.

The key: because we searched Move A first and got +1, we entered Move B knowing "this
needs to beat +1." The moment black found a way to score -2 (which is +2 for black —
worse than +1 for white), we stopped.

### Why Move Ordering Matters

The effectiveness of alpha-beta depends entirely on **move ordering**. If the best move
is searched first at every node, alpha-beta examines only O(√N) nodes instead of N — a
massive reduction. With 35 legal moves at depth 12:

- Without alpha-beta: 35^12 ≈ 10^18 nodes
- With perfect move ordering: 35^6 ≈ 10^9 nodes (square root)
- In practice (good but imperfect ordering): a few million nodes

If you search moves in random order, alpha-beta barely helps. If you search the best move
first at every node, it prunes the vast majority of the tree. This is why engines invest
so heavily in move ordering — see [Move Ordering](move-ordering.md).

## Iterative Deepening

### The Problem: How Deep Should You Search?

Alpha-beta needs a depth limit — you can't search the entire game tree. But what depth
should you pick? You don't know in advance how much time you have (it depends on the
clock, the time control, and how complex the position is). If you guess too high, you
run out of time mid-search with no result. If you guess too low, you waste time that
could have gone into a deeper, better search.

What you really want is to search **indefinitely** — getting progressively better results
— and stop whenever an external signal says "time's up." That's exactly what iterative
deepening does.

### How It Works

Rather than searching directly to a fixed depth, the engine searches depth 1, then
depth 2, then depth 3, and so on until time runs out. This is called **iterative
deepening**.

```
Iteration 1:  search to depth 1  →  best move = e2e4, score = +30
Iteration 2:  search to depth 2  →  best move = d2d4, score = +15
Iteration 3:  search to depth 3  →  best move = d2d4, score = +20
...
Iteration 12: search to depth 12 →  best move = d2d4, score = +25
(time runs out — return d2d4)
```

The iterative deepening loop lives in `iterative_deepening()` (`src/engine.hpp:227`).

### Why This Isn't Wasteful

It seems wasteful to repeat the search at every depth. But the game tree grows
**exponentially**. Searching to depth 12 examines roughly as many nodes as all of depths
1-11 combined. The overhead of all previous iterations is roughly 2× (the sum of a
geometric series). For that 2× cost, you get three huge benefits:

**1. Move ordering from previous iterations.** The best move at depth N seeds the first
move at depth N+1. This is the single most important source of move ordering. The best
move at depth 11 is the best move at depth 12 roughly 90% of the time. Without iterative
deepening, the first move at each node would be a random guess.

**2. Transposition table priming.** Each iteration fills the transposition table (see
[Transposition Table](transposition-table.md)). The next iteration gets TT hits for most
positions it visits, receiving both move hints and sometimes direct score cutoffs.

**3. Time management flexibility.** You can stop between iterations when time runs out.
If the clock expires during iteration 13, you still have a valid result from iteration 12.
Without iterative deepening, you'd need to guess the right depth up front — guess too high
and you time out with nothing.

### Thread Staggering (Lazy SMP)

Helper threads start at staggered depths to create search diversity — see
[Advanced Search](advanced-search.md) for the full Lazy SMP discussion.

## Building Your Own

If you're implementing search from scratch, start with plain negamax (no alpha-beta).
Get it working and verify it finds the right move in simple tactical puzzles. Then add
alpha-beta — the speedup is dramatic and immediately visible in the search depth you
can reach in the same time. Add iterative deepening next, and you'll have a search that
can play under time controls.

Don't try to add move ordering, pruning, or a transposition table all at once. Add them
one at a time, running matches after each change to verify it helps. The docs below
cover these in the order you'd typically implement them.

## What's Next

With minimax, alpha-beta, and iterative deepening, you have a working search. But a
competitive engine needs more: caching results in a transposition table, ordering moves
to maximize cutoffs, and pruning techniques that skip unpromising branches. These are
covered in the next few docs:

- [Transposition Table](transposition-table.md) — caching search results
- [Move Ordering](move-ordering.md) — searching the best move first
- [Pruning & Extensions](pruning.md) — skipping bad branches, extending good ones
- [Advanced Search](advanced-search.md) — PVS, aspiration windows, quiescence search,
  and Lazy SMP threading
