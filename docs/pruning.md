*Part 11 of 16 — [← Prev: Move Ordering](move-ordering.md) | [Next: Advanced Search →](advanced-search.md)*

# Pruning and Extensions

## What Is Pruning?

In [Search](search.md), we saw that alpha-beta pruning skips branches when it can prove
they won't affect the result. That's **sound** pruning — it never misses the best move.

But even with perfect alpha-beta, the tree is too big to search deeply. Modern engines go
further with **speculative pruning**: skipping branches that are *probably* bad, even
though they *might* contain something useful.

The tradeoff is clear: speculative pruning can occasionally miss things, but the time
saved lets you search deeper overall. An engine that prunes aggressively and reaches depth
15 will beat one that prunes conservatively but only reaches depth 12 — even though the
deeper engine occasionally misses something at a pruned node.

### The Pruning Spectrum

Every technique sits on a spectrum:

| Sound (safe) | | | Speculative (risky) |
|---|---|---|---|
| Alpha-beta cutoffs | Null move pruning | Futility pruning | Late move pruning |
| Never misses best move | Rarely misses | Sometimes misses | Frequently skips moves |

All speculative pruning is disabled in **PV nodes** (accurate scores matter for the
principal variation) and when **in check** (checks are forcing — skipping them is
dangerous).

**Relative importance.** Not all pruning techniques contribute equally. Null move pruning
and late move reductions together account for roughly 80% of the total tree reduction.
Everything else — futility pruning, LMP, SEE pruning, razoring — is useful but
comparatively minor. If you're implementing these for the first time, get NMP and LMR
right first.

The thresholds for every pruning technique are auto-tuned. Each has been validated by
playing thousands of games — if changing a threshold costs Elo, it stays where it is.
The tuned values live in `src/data/params.hpp`.

## Pre-Move Pruning

These techniques decide whether to skip the **entire subtree** at a node, before looking
at any moves. They rely on the static evaluation (from NNUE) to estimate whether the node
is "obviously" winning or losing.

### Null Move Pruning

**What it is.** In most chess positions, having the right to move is an advantage — you
can improve your position, attack, defend. Null move pruning exploits this: if we
*give up our move* (pass the turn to the opponent) and the position is still good enough
to beat beta, we're so far ahead that detailed search is unnecessary.

**How it works.**
1. Make a "null move" (pass — toggle side to move without moving a piece)
2. Search at reduced depth with a zero window around beta
3. If the result is still >= beta, return beta (prune the node)

**The reduction.** A fixed number of plies, with an even deeper reduction at higher
depths. Deeper reductions are justified because the "pass and still win" signal is
stronger at greater depth.

**When it breaks: zugzwang.** The assumption "having the move is good" fails in
**zugzwang** positions — where every move makes your position worse. These are rare in
middlegames (pieces have flexible moves) but common in king-and-pawn endgames. The
mitigation: null move pruning is only applied when the moving side has non-pawn material.
If you only have pawns left, zugzwang risk is too high.

The null move check in `negamax()` references the `can_apply_null_move()` helper, which
verifies we're not in check, not on a PV node, and have non-pawn material.

### Reverse Futility Pruning

**What it is.** If the static eval minus a generous safety margin still beats beta, the
position is clearly winning. No need to search.

```
if (eval - margin_per_depth × depth >= beta) return beta;
```

**The margin.** Grows linearly with depth. At depth 1, roughly a minor piece. At the
maximum applied depth (7), roughly a queen. Deeper nodes have more uncertainty, so the
margin must be larger.

**Why the name.** It's the "reverse" of futility pruning (below): futility checks "is
the eval so far below alpha that quiet moves can't help?" Reverse futility checks "is the
eval so far above beta that nothing can hurt?"

### Razoring

**What it is.** At depth 1, if the eval is far below alpha (a couple hundred centipawns),
quiet moves are unlikely to bridge the gap. Drop into quiescence search and let captures sort things
out.

**Why only depth 1.** At deeper depths, quiet moves can trigger chain reactions — a
knight retreat enabling a bishop fork. At depth 1, there's no room for multi-move
sequences.

## Per-Move Pruning

These techniques decide whether to skip **individual moves** during the move loop. They
only activate after at least one move has been fully searched (to ensure we don't prune
everything at a node).

### Futility Pruning

**What it is.** At shallow depth, if the eval plus a margin is below alpha, a quiet
non-checking move isn't going to raise alpha. The position is too far behind for a quiet
move to matter.

```
if (eval + margin_per_depth × depth + margin_base < alpha) skip this quiet move;
```

**What's skipped.** Only quiet moves that don't give check. Captures might win material.
Checks are forcing and can lead to mate or winning tactics.

**The chess intuition.** If you're down a pawn and a half (at depth 4), a quiet knight
move that doesn't attack anything isn't going to save you. You need a capture or a check.

### Late Move Pruning (LMP)

**What it is.** In a well-ordered move list, the first few moves contain everything
important. By the time you've searched 12+ moves at a shallow node, the remaining moves
are all low-history quiet moves — almost certainly inferior.

```
threshold = lmp_base + depth²
if (move_index > threshold) skip this quiet move;
```

The quadratic growth means deeper nodes get a higher threshold (more uncertainty about
which move is best).

**The risk.** LMP depends on move ordering being good. If history tables are poorly
calibrated, important moves might be late in the order and get pruned. In practice, this
is rare enough that the savings dominate.

Moves that give **check** bypass LMP regardless of their position in the order.

### SEE Pruning

**What it is.** At shallow depth, captures that lose material according to SEE (see
[Move Ordering](move-ordering.md)) are almost never good. A queen capturing a defended
pawn at depth 4 — there's not enough room for a justifying follow-up.

## Late Move Reductions (LMR)

LMR is the **workhorse** of modern search. Rather than pruning later moves entirely,
search them at **reduced depth**. If the reduced search finds something interesting (beats
alpha), re-search at full depth. If not, the move is dismissed.

### The Intuition

Move ordering puts the most promising moves first: the TT move, good captures, killers,
high-history quiets. Later moves are progressively less likely to be the best. So it makes
sense to spend less effort on them — search them shallowly, and only invest full effort
if the shallow search says "hey, this might actually be good."

### The Formula

Reductions come from a precomputed table (built by `build_lmr_table()` in
`src/engine.hpp:17`):

```
reduction = log(depth + 1) × log(move_index + 1) / k
```

where `k` is a tunable divisor controlling overall aggressiveness. `move_index` is how
far into the move list we are (0 = first move, which gets no reduction).

This gives a smooth logarithmic curve: early moves get little reduction, while late moves
at high depth get several plies of reduction.

### Why Logarithmic?

A linear formula would over-reduce late moves and under-reduce early ones. The logarithmic
shape is concave: the first few moves beyond the TT move get a gentle fractional-ply
reduction, while the 50th move at high depth gets substantial reduction — but not as
extreme as linear would give.

### Adjustments

- **PV nodes**: reduce 1 less ply. PV accuracy matters.
- **In check**: no reduction. Checks need full-depth treatment.
- **TT move, killers, countermove, captures**: no reduction. These are the moves we trust
  most. Reducing them would undermine the ordering system.
- **Clamped**: reduction can't go below depth 1 (always search at least 1 ply).

### Re-Search

If a reduced search beats alpha, the move is re-searched at full depth to confirm. This
re-search integrates with PVS into a multi-tier cascade — see the
[Re-Search Cascade](advanced-search.md#the-re-search-cascade) section in Advanced Search
for the full picture.

## Extensions

Extensions go the opposite direction: certain moves are searched **deeper** than the
nominal depth because they're critically important.

### Check Extension

When a move gives check, extend the search by 1 ply. This is the most universal extension
in chess engines, applied unconditionally.

**Why it matters.** Checks are forcing — the opponent must respond. Missing a check
extension means the search might stop right before discovering a checkmate or a winning
sequence. Checks can cascade (check → escape → check → escape) and the extension ensures
these are followed to completion.

### Singular Extensions

**What it is.** If one move is clearly better than all alternatives at a node, it deserves
deeper search. You want to be confident in a move that the entire evaluation hinges on.

**How it works.** At sufficient depth, if the TT has a good-quality entry:

1. Re-search the position with the TT move **excluded**, at reduced depth and with a
   narrower beta window.
2. If no other move comes close to the TT move's score (the reduced search fails low),
   the TT move is **singular** — extend it by 1 ply.
3. If the reduced search **still beats beta** even without the TT move: **multi-cut**.
   Multiple moves are very good, so this node is almost certainly a fail-high. Return
   beta immediately.

Multi-cut turns a "failed singularity test" into a pruning opportunity: if you removed
the best move and the position *still* beats beta, it's overwhelmingly likely to beat
beta with the best move included.

**Why only at high depth.** The singularity test requires an extra search per node —
expensive. At shallow depths, the cost outweighs the benefit.
