*Part 17 of 17 — [← Prev: Parameter Tuning](tuning.md)*

# Tooling & Workflow

## Building

Requires CMake 3.16+ and a C++20 compiler (GCC 10+, Clang 11+, or MSVC 2019+).

```bash
cmake -B build
cmake --build build
```

This produces two binaries:
- `build/enigma` — the UCI engine (what GUIs talk to)
- `build/enigma-dev` — tests and benchmarks (for development)

Release mode is the default. Compiler flags include `-O3 -march=native -flto` (GCC/Clang)
for maximum performance. `-march=native` enables AVX2 if your CPU supports it, which
significantly speeds up NNUE evaluation.

The engine is split into a static library (`enigma-lib`) and two thin frontends that link
against it.

## Running

Enigma speaks UCI. Point any UCI-compatible GUI (Arena, CuteChess, etc.) at
`build/enigma`, or run it directly in the terminal:

```bash
./build/enigma
uci
isready
position startpos moves e2e4 e7e5
go depth 20
```

### UCI Options

| Option | Default | Range | Description |
|--------|---------|-------|-------------|
| `Hash` | 64 | 1-16384 | Transposition table size in MB |
| `Threads` | 1 | 1-64 | Number of search threads |
| `OwnBook` | true | — | Use the compiled-in opening book |

## Tests

```bash
./build/enigma-dev test              # all tests
./build/enigma-dev test perft        # specific test
./build/enigma-dev test zobrist legal search   # multiple tests
```

Available test suites:

| Suite | What it tests |
|-------|--------------|
| `perft` | Move generation correctness (node counts at various depths) |
| `legal` | Legal move detection for edge cases |
| `zobrist` | Hash consistency across make/unmake |
| `make_unmake` | Board state restoration after unmake |
| `nnue` | NNUE accumulator consistency |
| `search` | Search finds known best moves |
| `transposition_table` | TT store/retrieve correctness |
| `null_move` | Null move make/unmake |
| `check` | Check detection |
| `draws` | Draw detection (repetition, 50-move) |
| `book` | Opening book lookups |
| `hashing` | Hash collision testing |

**Perft** is the most important test for development. If your perft numbers match the
known values, your move generator and make/unmake are almost certainly correct.

## Benchmarks

```bash
./build/enigma-dev bench             # all benchmarks
./build/enigma-dev bench movegen     # move generation only
./build/enigma-dev bench search      # search only
./build/enigma-dev bench --fast      # reduced position set (quicker)
```

Add `--verbose` for per-position breakdown. Benchmarks use a fixed set of positions and
report nodes/second, which is useful for comparing performance across code changes or
hardware.

## NNUE Training

All Python scripts live in `scripts/` and use `uv` for dependency management.

```bash
cd scripts
uv sync    # install dependencies
```

See [NNUE Training](nnue-training.md) for the full conceptual explanation. Quick reference:

### Generate Training Data

```bash
uv run -m nnue.datagen                # training data
uv run -m nnue.datagen --validation   # validation data
```

Self-play at fixed depth 8. Output goes to `scripts/nnue/data/{training,validation}/`.
Parallelization is automatic.

Datagen is the slowest part of the pipeline — expect hours to days depending on depth and
position count. You typically need millions of positions: 10-50 million is a reasonable
starting point for a first network. Depth 8 is a sweet spot between data quality and speed;
higher depths produce better labels but take exponentially longer per position.

### Train

```bash
uv run -m nnue.train                  # resume from latest checkpoint
uv run -m nnue.train --weights none   # train from scratch
uv run -m nnue.train --weights 3      # resume from checkpoint 3
```

Checkpoints saved to `scripts/nnue/data/weights/weights_N.pt`.

Training is much faster than datagen — usually minutes to a couple of hours depending on
dataset size. Typical runs are 50-200 epochs. Watch the validation loss: it should drop
quickly at first and then plateau. If it starts rising, you're overfitting and should stop.
A final loss around 0.02-0.05 is normal for a well-trained small network.

### Export Weights

```bash
uv run -m generators.nnue
```

Produces `src/data/nnue.bin`. **Rebuild the engine** after this step.

## Parameter Tuning

Uses SPSA (Simultaneous Perturbation Stochastic Approximation) to optimize all engine
parameters simultaneously by playing matches between perturbed configurations.

```bash
uv run -m tune                  # fresh tuning run
uv run -m tune --resume         # resume interrupted run
```

Each iteration perturbs all 25 search parameters in random directions and plays games
per time control at 8+0 and 8+0.08. The gradient estimate from each
iteration nudges parameters toward stronger values. Perturbation size (`c`) and learning
rate (`a`) are derived automatically from each parameter's range — no per-parameter tuning
needed.

The tuner auto-stops when the rolling average win rate over the last 30 iterations stays
within 0.5 ± 0.015, meaning the gradient is flat (converged). Results saved as pickles in
`scripts/tune/data/`.

To apply tuned parameters:

```bash
uv run -m generators.params
```

This writes `src/data/params.hpp`. Rebuild after.

## Running Matches

Requires `cutechess-cli`. Set the path:

```bash
export cutechess_cli_binary=/path/to/cutechess-cli
```

### Save a Version

```bash
uv run -m match.save_version my_feature   # saves as versions/vN_my_feature
```

### Run a Match

```bash
uv run -m match.run_match                 # current build vs latest saved version
uv run -m match.run_match v0              # current build vs v0
uv run -m match.run_match v1 v2           # v1 vs v2
uv run -m match.run_match --sprt 20       # SPRT test (20 Elo threshold)
```

Match config: 8+0.08 time control, 500 openings played from both sides (1000 games),
concurrency auto-detected.

The `--sprt` flag uses a Sequential Probability Ratio Test — a statistical test that stops
the match early once there's enough evidence to accept or reject a given Elo gain. This
saves time compared to running a fixed number of games. For context, typical Elo gains:
a bug fix or new pruning technique might gain 10-30 Elo, a new NNUE network 20-50, and
parameter tuning 5-15.

## Code Generators

The `generators` module converts external data into C++ headers that get compiled into
the binary.

```bash
uv run -m generators              # run all generators
uv run -m generators.book         # opening book → src/data/book.hpp
uv run -m generators.nnue         # NNUE weights → src/data/nnue.bin
uv run -m generators.params       # tuned params → src/data/params.hpp
```

Always rebuild after running any generator.

## Full Workflow

From nothing to a stronger engine:

```bash
# build (seconds)
cmake -B build && cmake --build build

# verify everything works (seconds — all tests should pass)
./build/enigma-dev test

# save current version as baseline
cd scripts
uv run -m match.save_version baseline

# generate training data (hours to days — the longest step by far)
uv run -m nnue.datagen &
uv run -m nnue.datagen --validation &
wait

# train the network (minutes to hours — watch validation loss converge)
uv run -m nnue.train

# export weights and rebuild (seconds)
uv run -m generators.nnue
cd .. && cmake --build build

# check for regression (30-60 min — should be roughly even or better)
cd scripts
uv run -m match.run_match

# optionally tune parameters (hours — auto-stops on convergence)
uv run -m tune

# export tuned params and rebuild (seconds)
uv run -m generators.params
cd .. && cmake --build build

# final match to verify improvement (stops early via SPRT once conclusive)
cd scripts
uv run -m match.run_match --sprt 20
```
