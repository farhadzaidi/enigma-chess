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

### Train

```bash
uv run -m nnue.train                  # resume from latest checkpoint
uv run -m nnue.train --weights none   # train from scratch
uv run -m nnue.train --weights 3      # resume from checkpoint 3
```

Checkpoints saved to `scripts/nnue/data/weights/weights_N.pt`.

### Export Weights

```bash
uv run -m generators.nnue_weights
```

Produces `src/data/nnue_weights.hpp`. **Rebuild the engine** after this step.

## Parameter Tuning

Uses Optuna with TPE (Tree-structured Parzen Estimator) to optimize search or time
management parameters by playing matches.

```bash
uv run -m tune.tune search               # tune search params
uv run -m tune.tune tm                    # tune time management params
uv run -m tune.tune search --games 800   # custom games per trial
uv run -m tune.tune search --only AspirationWindow ScoreDropThreshold  # subset
```

Each trial plays the candidate parameters against the current baseline. 500 games per
trial by default. Results saved as pickles in `scripts/tune/data/`.

To apply tuned parameters:

```bash
uv run -m generators.params search    # or: tm, all
```

This writes `src/data/search_params.hpp` and/or `src/data/tm_params.hpp`. Rebuild after.

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

## Code Generators

The `generators` module converts external data into C++ headers that get compiled into
the binary.

```bash
uv run -m generators              # run all generators
uv run -m generators.book         # opening book → src/data/book.hpp
uv run -m generators.nnue_weights # NNUE weights → src/data/nnue_weights.hpp
uv run -m generators.params all   # tuned params → src/data/*_params.hpp
```

Always rebuild after running any generator.

## Full Workflow

From nothing to a stronger engine:

```bash
# build
cmake -B build && cmake --build build

# verify everything works
./build/enigma-dev test

# save current version as baseline
cd scripts
uv run -m match.save_version baseline

# generate training data (takes hours)
uv run -m nnue.datagen &
uv run -m nnue.datagen --validation &
wait

# train the network
uv run -m nnue.train

# export weights and rebuild
uv run -m generators.nnue_weights
cd .. && cmake --build build

# check for regression
cd scripts
uv run -m match.run_match

# optionally tune search parameters
uv run -m tune.tune search

# export tuned params and rebuild
uv run -m generators.params all
cd .. && cmake --build build

# final match to verify improvement
cd scripts
uv run -m match.run_match --sprt 20
```
