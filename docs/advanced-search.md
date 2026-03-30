# Advanced Search

This doc builds on the fundamentals from [Search](search.md). If you haven't read that
yet, start there — you'll need minimax, alpha-beta, and iterative deepening as
background.

Here we cover the techniques that turn a basic alpha-beta searcher into a competitive
engine: aspiration windows, principal variation search, quiescence search, and
multi-threaded search.

## Aspiration Windows

At each iteration of iterative deepening, instead of searching with the full score range
[-32000, +32000], the engine uses a **narrow window** centered on the previous iteration's
score:

```
window = previous_score ± aspiration_delta
```

The aspiration delta is a tunable constant (from `src/data/search_params.hpp`),
typically on the order of 50 centipawns.

### Why This Helps

A narrow window means more aggressive cutoffs. With a full window, alpha-beta needs to
find the **exact** score. With a narrow window around the expected score, it only needs to
confirm the score is roughly what we expected. Nodes that would need deep exploration to
determine whether they're at +250 or +300 can now cut off as soon as they exceed the
window.

### When It Fails

If the result falls outside the window (**fail-high** or **fail-low**), the engine doubles
the corresponding delta and re-searches. The two deltas grow independently — a fail-high
doesn't widen the lower bound.

Most iterations resolve within the initial window (scores are stable between depths). The
overhead of the occasional re-search is vastly outweighed by the savings from the narrow
window on all other iterations.

Depth 1 uses a full window because there's no prior score to center on.

## Principal Variation Search (PVS)

Plain alpha-beta searches every move with the same full-width window. But if move ordering
is good (and it usually is — the TT move is best ~90% of the time), the first move is
almost always the best. We're spending full effort on 30+ moves when only the first one
matters. Can we search the rest more cheaply?

**PVS** does exactly that. Search the first move with a **full window** to get an accurate
score. All subsequent moves get a **null window** (zero width: [alpha, alpha+1]) that only
asks "is this move better than the best so far?"

A null-window search is cheap: it can cut off as soon as it finds any move better than
the threshold. Most of the time, subsequent moves confirm they're worse, and you move on.

When a null-window search reports "yes, this move is better" (a **fail-high**), re-search
with the full window to get the real score. This happens rarely if move ordering is good.

### The Re-Search Cascade

At non-root nodes, the cascade has three tiers:

1. **Reduced null-window**: apply LMR reduction (see [Pruning](pruning.md)), search with
   null window. This is the cheapest possible check.
2. **Full-depth null-window**: if the reduced search beat alpha and we reduced, re-search
   without reduction. The reduced search might have been too shallow.
3. **Full window**: if the full-depth null-window still beats alpha, re-search with the
   real window to get an exact score.

Most moves exit at tier 1 (quickly confirmed as worse). A few reach tier 2 (the reduction
was too aggressive). Rarely does a move reach tier 3 (genuinely better than the first
move). This is exactly the pattern you want: cheap tests for the common case, expensive
work only when needed.

## The Negamax Function: Putting It All Together

The `negamax()` function is the main search routine. Here's a high-level walk through
what happens at each node:

1. **Draw detection.** Check for repetition and 50-move rule. Returns 0 (draw score).
   Checked first because it's cheap and common in endgames.

2. **Depth 0 → quiescence search.** When depth reaches 0, hand off to quiescence search
   (see below) rather than using the static evaluation directly.

3. **TT probe.** Look up the position's hash in the transposition table (see
   [Transposition Table](transposition-table.md)). A hit with sufficient depth and the
   right bound type produces an instant cutoff — return the stored score without any
   search. Even on a miss, the stored best move becomes the first move to try.

4. **IID (Internal Iterative Deepening).** On PV nodes with no TT move and sufficient
   depth, run a shallow search (at half the current depth) to populate the TT with a
   move hint. This ensures PV nodes almost always have a good first move, which is
   critical for PVS efficiency.

5. **Pre-move pruning.** Null move pruning, reverse futility pruning, razoring. These
   can return a score for the entire node without looking at any moves — see
   [Pruning](pruning.md).

6. **Move loop.** Iterate through moves from the staged move selector (see
   [Move Ordering](move-ordering.md)). For each move:
   - Apply per-move pruning (futility, LMP, SEE pruning)
   - Compute extensions (check extension, singular extension)
   - Make the move, search recursively via PVS, unmake
   - Update alpha, track best move

7. **Checkmate/stalemate detection.** If no legal moves were found: checkmate (score
   = -CHECKMATE_SCORE + ply, so shorter mates are preferred) or stalemate (score = 0).

8. **TT store.** Write the result into the transposition table with the appropriate bound
   type: EXACT if within the original window, FAIL_HIGH if it beat beta, FAIL_LOW if
   nothing beat alpha.

9. **History updates.** On a beta cutoff by a quiet move, update killer moves, history
   tables, and the countermove table (see [Move Ordering](move-ordering.md)).

## Quiescence Search

At depth 0, the static evaluation might be completely wrong. If the last move was a queen
capture and we haven't recaptured, the eval thinks we're down a queen. This is the
**horizon effect** — the search stops at an arbitrary depth and misses obvious tactical
sequences.

**Quiescence search** extends the search with captures and promotions only, until the
position "quiets down." There's no fixed depth limit — capture sequences are finite and
usually short (2-6 plies).

The quiescence search is `quiescence_search()` in `src/engine.hpp:209-214`.

### Stand Pat

Before looking at captures, the engine evaluates the position statically (using NNUE).
This **stand pat** score serves as a lower bound: "I can always choose not to capture."

If the stand pat score is already above beta, we can cut off immediately — doing nothing
is already good enough. If it's above alpha, raise alpha.

### In Check

When in check, there's no stand pat — you **must** respond. All legal evasions are
searched, not just captures. This prevents the search from overlooking a checkmate at the
horizon.

### Capture Ordering and SEE Pruning

Captures are ordered by MVV-LVA (Most Valuable Victim, Least Valuable Attacker).
Captures with SEE (Static Exchange Evaluation — see [Move Ordering](move-ordering.md))
below a tunable cutoff are skipped. Deeply negative exchanges (rook for pawn) almost
never lead to anything good.

## Threading: Lazy SMP

Multiple threads search the same position independently, sharing only the transposition
table. No work splitting, no message passing, no shared move lists.

### How It Works

Each thread runs its own iterative deepening with its own board copy, history tables,
and killer tables. The only shared state is the TT and two atomic stop flags
(`external_stop_` and `main_finished_` in `src/engine.hpp:141-142`).

This means:
- No synchronization overhead — no locks, no atomic operations in the hot path
- No complex work-distribution logic
- Each thread's search is slightly different (different history, different TT timing,
  staggered start depths), so they explore different parts of the tree

The TT acts as an implicit communication channel: when one thread discovers a strong move
and stores it, other threads find it on their next TT probe.

### Thread Staggering

In Lazy SMP, helper threads start at staggered depths: even-indexed helpers at depth 2,
odd at depth 1. This creates **search diversity**: different threads work on different
depths, finding different positions, populating different TT entries. If all threads
searched identically, they'd do entirely redundant work.

### Scaling

Lazy SMP scales well up to about 8 threads, with diminishing returns beyond that
(eventually threads do too much redundant work). Thread count is configurable from 1 to
64 via UCI `Threads`.

### Termination

The main thread watches the clock and node count via `should_stop_search()`
(`src/engine.hpp:163`). When it finishes, it sets `main_finished_`. Helpers check this
flag periodically and exit when it's set. The main thread then joins all helper threads
and reports the best move.

## Search Constants

A few important constants defined in `src/engine.hpp:21-28`:

```cpp
constexpr PositionScore CHECKMATE_SCORE = 32'000;  // score for checkmate
constexpr PositionScore STALEMATE_SCORE = 0;         // score for stalemate (draw)
constexpr MoveScore MAX_MOVE_SCORE = 32'000;         // bounds for move ordering scores
```

Checkmate scores encode distance: `-CHECKMATE_SCORE + ply` means "I'm getting mated in
`ply` moves." Shorter mates get more extreme scores, so the engine prefers mating quickly
and avoids getting mated as long as possible.
