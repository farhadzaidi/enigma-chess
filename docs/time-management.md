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

Time management is implemented in `src/uci.cpp` (allocation) and `src/engine.cpp`
(enforcement during search).

## When Time Management Applies

Everything below applies to **clock-based** time controls (`go wtime/btime`), which is
the normal case in games. UCI also supports `go movetime X` (fixed time per move — just
use it all) and `go infinite` (think until the GUI sends `stop`). In those modes there's
nothing to manage.

## Two Deadlines

The engine computes two deadlines before each search:

**Soft limit** — the target thinking time. After each iteration of iterative deepening,
the engine checks whether the soft deadline has passed. If it has and the search has
settled on a move, stop. If the situation is uncertain (score dropping, best move
changing), keep going.

**Hard limit** — the absolute maximum. The search stops immediately when this is reached,
even mid-iteration. This is the safety net — no matter how complex the position, you can't
spend more than this.

The soft limit is where the intelligence is. The hard limit is where the safety is.

## Estimating Moves Remaining

The first step: how many moves are left in the game? This determines how to divide the
remaining clock.

If the GUI sends `movestogo` (some time controls specify moves until the next time
increment), use that directly. Otherwise, estimate from the game phase:

```
moves_left = base_moves + phase_ratio × phase_scale
```

`phase_ratio` is the material-based game phase (1.0 at full material, 0.0 with bare
kings — see the game phase section in [Board State](board.md)). `base_moves` sets the
floor for endgames. `phase_scale` controls how much the opening/middlegame inflates the
estimate.

In the opening with all pieces, the estimate is deliberately high — conserving time for
the long game. As pieces trade off, it drops toward `base_moves`.

All constants are auto-tuned (stored in `src/data/tm_params.hpp`). Optuna plays thousands
of games at various time controls and finds the values that maximize playing strength
while minimizing time forfeits.

## Allocating Time

The time allocation formula in `src/uci.cpp` (lines 41-66):

```
base = remaining / moves_left + increment × increment_fraction
```

The increment fraction is slightly below 1.0 — the engine holds back a small safety
buffer rather than spending the entire increment each move.

### Soft Limit

```
soft = base × soft_factor
```

The soft factor differs depending on whether the time control has increment. With
increment, it's smaller (more conservative) because increment-based time controls tend
to have less total remaining time.

### Hard Limit

```
hard = min(base × hard_factor, remaining / hard_cap_divisor)
```

The hard factor gives room for complex positions. The cap (typically half the remaining
clock) is an absolute safeguard — never spend too much on one move.

### Emergency Mode

When remaining time is critically low (below a tunable trigger):

```
soft = remaining / emergency_soft_divisor
hard = remaining / emergency_hard_divisor
```

Panic mode. The engine abandons deep search and just tries to play reasonable moves fast
enough to survive.

## Continuing Past the Soft Limit

The soft limit isn't a hard stop — it's a suggestion. The search continues past it when
either condition holds:

### Score Drop

If the score fell by more than `score_drop_threshold` since the previous iteration,
something dramatic happened — the engine just discovered it's losing a piece, or found a
deep tactical sequence. Stopping now would commit to a move chosen *before* this
discovery.

### Best-Move Instability

If the best move keeps changing between iterations, the engine hasn't converged. Committing
to whichever move happens to be best at the moment of the soft deadline is risky.

The stability counter tracks consecutive iterations with the same best move. If it's zero
(the move just changed), keep searching. Once it's positive (stable for at least one
iteration) and the score hasn't dropped, stop.

These extensions can push thinking time up to the hard limit — which is why the hard limit
exists.

## Clock Checking

Reading the system clock is a syscall. Cheap by syscall standards (~50-200ns) but not free
when called millions of times per second. At 10M nodes/sec, a 100ns clock read on every
node would be a significant overhead.

Instead, the clock is checked every **2048 nodes**, detected via a bitmask
(`should_stop_search()` in `src/engine.hpp:163`):

```
(nodes & 2047) == 0
```

This bitmask check compiles to a single AND instruction — essentially free. At 10M
nodes/sec, the clock is read ~5000 times per second.

Only the main thread checks time. Helper threads run until `main_finished_` is set by
the main thread.

## The Tuning Dimension

Every constant in time management — base moves, phase scale, soft/hard factors, emergency
thresholds, score drop threshold — is a tunable parameter. Enigma tunes them by playing
thousands of games with different values, using Optuna to find the best combination.

This is important because time management interacts with search quality in non-obvious
ways. Spending more time per move means deeper search, but also less time for future
moves. The optimal balance depends on game length, increment, search efficiency, and even
opponent strength. Automated tuning navigates this space better than manual adjustment.

See [Tooling](tooling.md) for how to run the tuning pipeline.
