*Part 16 of 17 — [← Prev: Time Management](time-management.md) | [Next: Tooling & Workflow →](tooling.md)*

# Parameter Tuning

## Why Tune At All?

A chess engine has dozens of numeric constants that control its behavior. How wide should
the aspiration window be? How aggressively should we prune losing captures? How much time
should we save for the endgame?

Pick good values and the engine plays stronger. Pick bad ones and it wastes time on
hopeless positions, prunes winning tactics, or flags on the clock.

You could hand-tune these by intuition. Set the aspiration window to 50 centipawns, play
some games, try 25, play more games, try 75, compare results. This is how early engines
were tuned. It works, barely — but it has problems:

- **Interactions are invisible.** Widening the aspiration window might only help if you
  also increase the score drop threshold. Hand-tuning one parameter at a time misses these
  interactions entirely.
- **Signal is weak.** A 5 Elo improvement requires hundreds of games to measure reliably.
  You can't eyeball your way to 5 Elo.
- **There are too many knobs.** Enigma has 38 tunable parameters. Even testing 3 values
  per parameter means 3^38 combinations — more than the number of atoms in the universe.

Automated tuning solves all three. It searches the parameter space systematically, measures
results statistically, and finds combinations that no human would stumble on.

## Parameters Change As the Engine Changes

An important subtlety: the optimal parameter values **depend on the rest of the engine**.
When you add a new pruning technique, the best values for existing pruning thresholds
shift. When you retrain NNUE, the evaluation landscape changes, and
search parameters that were optimal before might not be anymore.

This means tuning isn't a one-time thing. Every significant engine change should be
followed by a tuning pass. The workflow is:

1. Make a code change (new feature, NNUE retrain, etc.)
2. Run a match to verify it doesn't regress
3. Tune parameters to find the new optimum
4. Run a match to measure the combined improvement

Step 3 often recovers Elo that the raw code change left on the table.

## The Intuition: Gradient From Games

Forget the math for a moment. Here's the core idea behind automated tuning:

Take your current parameters. Nudge all of them slightly in random directions — some up,
some down. Play a match with the nudged values. If the nudged version wins more, the
nudges were probably good. If it loses more, they were probably bad.

Now here's the clever part: you nudged each parameter in a known direction (+1 or -1).
If the overall result was positive, the parameters you nudged upward probably should go
up, and the ones you nudged downward probably should go down. If the result was negative,
reverse that logic.

Do this hundreds of times. The random noise cancels out. The consistent signal — which
parameters genuinely help when increased or decreased — accumulates. The parameters drift
toward their optimal values.

This is the essence of SPSA.

## SPSA: Simultaneous Perturbation Stochastic Approximation

SPSA is a gradient-free optimization algorithm. It estimates the gradient (which direction
to move each parameter) from just **two** function evaluations per iteration, regardless
of how many parameters you're tuning. For Enigma's 38 parameters, that means one match
per iteration instead of 76 (two per parameter, which is what finite-difference methods
would need).

### The Algorithm

Each iteration has three steps:

**1. Perturb.** For each parameter, flip a coin: +1 or -1. Multiply by a perturbation
size `c_k` and add/subtract from the current value. This creates two configurations:
`theta + perturbation` and `theta - perturbation`.

**2. Evaluate.** Play a match between the two perturbed configurations. The win rate
tells you which perturbation direction was better.

**3. Update.** For each parameter, estimate the gradient and take a step:

```
signal = win_rate - 0.5
gradient_i = signal / realized_perturbation_i
theta_i += a × a_k × gradient_i
```

The `signal` is how much one side outperformed the other. Dividing by the perturbation
gives the gradient estimate — how much the win rate changes per unit change in that
parameter. Then we step in that direction.

### The Gain Schedules

Two sequences control the step sizes and they decay over time:

```
c_k = c / (k + 1)^gamma         perturbation size (shrinks slowly)
a_k = a_init / (k + 1 + A)^alpha    learning rate (shrinks faster)
```

Early iterations take big steps to explore. Later iterations take small steps to converge.
The standard values (alpha = 0.602, gamma = 0.101) come from the SPSA convergence theory
and work well in practice.

### Deriving c and a From the Parameter Range

A common headache with SPSA is picking `c` and `a` for each parameter. Too small and
nothing moves. Too large and the optimizer overshoots.

Enigma derives both from the parameter's range automatically:

```
c = (max - min) × 0.075
a = 20 × c²
```

For integer parameters, `c` is floored at 1 (can't perturb by less than 1).

Why `a = 20 × c²`? The gradient update divides by `c`, so the effective step is
proportional to `a / c`. With `a ∝ c²`, the step is proportional to `c`, which is
proportional to range. Every parameter moves about 1-2% of its range per iteration with
a strong signal. Narrow-range integer parameters (range 3-5) move a bit faster
proportionally because `c` is floored at 1.

This means adding a new tunable parameter requires only a name, min, max, and whether
it's an integer. No per-parameter tuning constants to fiddle with.

### Why c Cancels Out (And Why That's Fine)

A surprising property: with `a ∝ c²`, the perturbation size `c` doesn't affect the update
magnitude at all. The `c` in the numerator (from `a`) cancels the `c` in the denominator
(from the gradient estimate). Every parameter gets the same absolute step per unit of
signal, regardless of how wide its perturbation was.

So what does `c` actually do? It controls the **quality** of the gradient estimate. Larger
`c` means the two configurations differ more, giving the match a better chance of
producing a meaningful signal. But it also makes the gradient estimate less precise (you're
measuring the slope of a secant line, not a tangent). The 7.5% of range is a good
compromise.

### Convergence

SPSA converges when perturbing parameters in any direction no longer helps — the win rate
between the +perturbation and -perturbation sides stays close to 50%. At that point the
gradient is effectively flat and you're at a local optimum.

In practice, you pick a window size (how many recent iterations to average) and a tolerance
(how close to 50% counts as "flat"). If the rolling average stays within the tolerance for
the entire window, stop. A wider window means more confidence but slower detection. A
tighter tolerance means you're more certain it's truly converged but you risk running
longer than necessary.

Enigma uses a window of 30 iterations and a tolerance of ±0.015 (48.5%-51.5%).

### Handling Integer Parameters

Most engine parameters are integers (depths, margins in centipawns). SPSA works in
continuous space internally — `theta` is a float even for integer parameters. The
perturbation step adds/subtracts fractional amounts, and the integer rounding only happens
when building the engine configuration for a match.

This continuous relaxation is essential. Without it, integer parameters would need the
accumulated update to exceed ±0.5 before `round()` produces a different value, and the
gradient signal would be lost to rounding noise. By keeping `theta` as a float, small
consistent gradients accumulate smoothly across iterations.

### Handling Bounds

Parameters have minimum and maximum values. When a perturbation pushes a value beyond
its bounds, it gets clamped. But the gradient update still needs to know what perturbation
**actually happened** — if one side got clamped, the realized perturbation is smaller
than `c_k`.

Enigma tracks the realized half-difference `(theta_plus - theta_minus) / 2` for each
parameter and uses that as the gradient denominator instead of the nominal `c_k`. If
both sides clamp to the same value (parameter is stuck at a bound), the gradient for
that parameter is skipped entirely.

## The Parameters

Enigma tunes 38 parameters simultaneously — 27 search parameters and 11 time management
parameters. They're defined in `scripts/tune/spsa.py` and the tuned values are compiled
into `src/data/params.hpp`.

Search parameters control pruning thresholds (futility margins, razoring depth, SEE
cutoffs), reduction amounts (null move, LMR), and other search heuristics. Time management
parameters control move estimation, time allocation factors, and emergency behavior. See
[Pruning](pruning.md) and [Time Management](time-management.md) for what each one does.

All parameters use the same SPSA infrastructure — the `c` and `a` values are derived from
their ranges, and the algorithm treats them identically. The only distinction is whether
a parameter is an integer or a float.

See [Tooling](tooling.md) for how to run the tuner and apply results.
