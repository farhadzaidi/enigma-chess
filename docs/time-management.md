*Part 15 of 17 — [← Prev: NNUE Training](nnue-training.md) | [Next: Parameter Tuning →](tuning.md)*

# Time Management

## Why Time Management Matters

Time management is the only part of a chess engine that operates without a clear "correct
answer." The search can find the best move given enough time. The evaluation can score a
position. The move generator can enumerate legal moves. But time management has to
**predict the future**: how many moves are left in the game? How much time does this
position need? Will the score stabilize in one more iteration or five?

Get it wrong and you either:
- **Flag on time** — catastrophic. You lose regardless of position.
- **Waste time on easy moves** — less dramatic but still costly. That time could have been
  spent on harder positions later.

## When Time Management Applies

Everything below applies to **clock-based** time controls (`go wtime/btime`), which is
the normal case in games. UCI also supports `go movetime X` (fixed time per move — just
use it all) and `go infinite` (think until the GUI sends `stop`). In those modes there's
nothing to manage.

## Building Up From Scratch

Before diving into what Enigma does, it helps to see why simpler approaches fall short.

**Fixed time per move.** Divide the remaining clock evenly:

```
time_per_move = remaining / estimated_moves_left
```

Pick 30 or 40 for `estimated_moves_left` and you'll rarely flag. But not all positions
deserve equal time — a forced recapture needs milliseconds, while a complex middlegame
might benefit from seconds of extra thought.

**Soft and hard limits.** The next step is splitting the budget into two deadlines: a
**soft limit** (target time — stop here if things look settled) and a **hard limit**
(absolute maximum — never exceed this). The soft limit is checked between depth iterations.
The hard limit is enforced mid-search — it prevents a single exploding iteration from
draining the clock.

Easy positions stop at the soft limit, complex ones push toward the hard limit. But both
are still computed once before the search starts. A smarter system would adjust based on
what the search actually finds.

## How Enigma Does It

Enigma uses the soft/hard framework, but the stop decision is made dynamically after each
completed depth based on what the search found.

The implementation lives in `src/time_manager.hpp` and `src/time_manager.cpp`. The
`TimeManager` class has three entry points:

1. **`allocate_time()`** — called before each search, computes the base soft and hard
   limits from the clock state.
2. **`init_search()`** — resets per-search tracking state.
3. **`should_stop_after_depth()`** — called after each completed depth, decides whether
   to stop or keep searching.

## Time Allocation

The first step is computing a time budget from the clock state.

### Safety Reserve

Before dividing up the clock, the engine holds back a safety reserve that scales with the
remaining time so it never flags. With increment the reserve can be smaller since time is
replenished each move (2% of remaining, or a multiple of the increment, whichever is
larger). Without increment the engine needs a bigger cushion (5% of remaining, with a
higher minimum floor).

### Estimating Moves Remaining

The engine needs to know how many moves to spread the clock across. If the GUI provides
`movestogo` (some time controls tell you how many moves until the next time top-up), that
number is used directly.

Otherwise the engine estimates from the game phase. A `phase_ratio` derived from the
material on the board (1.0 at the start, 0.0 in a bare endgame) scales between many moves
remaining and few. Without increment the estimate is floored higher to avoid running low.

### Base, Soft, and Hard Limits

The base time divides the spendable clock (remaining minus reserve) evenly across estimated
moves, then credits a portion of the increment as bonus time.

The **soft target** is slightly below the base — a scaling factor (different for increment
vs no-increment games) leaves headroom so the search can finish its current iteration
gracefully rather than cutting off mid-depth.

The **hard max** is the absolute ceiling — a multiple of the base time, but never more than
the full spendable clock. This lets the engine extend on complex positions without risking
the whole game.

### Emergency Mode

When the clock is critically low relative to the base allocation, both limits are
overridden to tiny fractions of the remaining time — fast shallow moves to avoid flagging.

### Ponder Bonus

When pondering (thinking on the opponent's turn), the engine can afford a larger soft
target since it already has a head start.

## Soft Time: Deciding When to Stop

After each completed depth in iterative deepening, `should_stop_after_depth()` decides
whether to keep going or stop.

The decision is based on two signals and one optimization:

### Best Move Stability

The engine tracks how many consecutive depths the best move has been the same. If the best
move keeps changing between iterations, the position is uncertain and more time is
warranted. If it's been stable, the engine is confident and can stop sooner.

### Score Drop Detection

If the score drops significantly between iterations, the position is more complicated than
it appeared. The engine refuses to stop early — it keeps searching even past the soft
target to avoid committing to a move when something has gone wrong.

### Winning Position Speedup

When the engine is clearly winning and the best move is stable, the soft target is reduced
so it moves faster. There's no point burning clock to distinguish +5.0 from +5.2 — both
are winning. The reduction scales linearly from a moderate advantage up to a decisive one.

### The Stop Decision

Putting it together: after each depth, once elapsed time exceeds the (possibly reduced)
soft target, the engine stops — but only if the best move is stable **and** the score
hasn't dropped. If either condition fails, it keeps searching up to the hard max.

## Hard Time Enforcement

The hard limit is the only mid-search safety net. Soft time is checked between depth
iterations (a natural stopping point), but a single iteration can take much longer than
expected. The hard limit needs to be enforced **during** search — potentially millions of
nodes into an iteration.

Reading the system clock on every node would be wasteful — at 10M nodes/sec, even a cheap
100ns syscall adds up to ~1% of search time. Instead, the clock is checked every **2048
nodes** via a bitmask (`should_stop_search()` in `src/engine.cpp`):

```
(nodes & 2047) == 0
```

This compiles to a single AND instruction. At 10M nodes/sec the clock is read ~5000 times
per second — the worst-case overshoot is the time to search 2048 nodes, well under a
millisecond.

Only the main thread checks time. Helper threads run until the main thread signals them
to stop.

## Tuning the Constants

Search parameters are tuned with SPSA (see [Parameter Tuning](tuning.md)), which needs
thousands of games for statistical significance. To keep each run practical, games are
played at very short time controls (8+0.08s). That works for search parameters — they
behave similarly regardless of TC.

Time management constants are a different story. The whole point of TM is to play well at
real human time controls — 1+0 (bullet), 3+2 (blitz), 15+10 (rapid) — and these have
very different pressure profiles from an 8-second TC. At 8+0.08 the clock is always
healthy, emergency mode rarely triggers, the moves-left estimate barely matters, and subtle
reserve issues never surface. Constants that look fine there can cause flagging or wasteful
spending at the TCs that actually matter.

For this reason, TM constants are hand-tuned by playing real games online and observing
clock behavior directly: whether the reserve holds up through the endgame, whether the
engine flags in won positions, whether it wastes time on moves that don't need it, and
whether spending spikes in already-decided positions. This is slower than automated tuning
but captures behavior that short-TC optimization cannot.
