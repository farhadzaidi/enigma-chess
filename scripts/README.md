# Scripts

Utility scripts for data generation, training, tuning, and version management. Managed with [uv](https://docs.astral.sh/uv/).

## Setup

```bash
uv sync
```

All commands below are run from the `scripts/` directory.

## Layout

```bash
scripts/
  lib/              # Shared utilities (paths, concurrency, versioning)
  generators/       # C++ header generators (book, NNUE weights, params)
  nnue/             # NNUE data generation and training
  match/            # Engine version management and cutechess matches
  tune/             # SPSA-based parameter tuner
```

## Data Generators

Generate C++ headers that are embedded into the engine binary at compile time. The generated files are checked in and don't need to be rerun unless the source data changes.

```bash
uv run -m generators          # run all generators
```

Individual generators:

- `generators/book.py` — `positions/games.san` → `src/data/book.hpp`
- `generators/nnue.py` — quantizes trained NNUE weights → `src/data/nnue.bin`
- `generators/params.py` — tuned engine params → `src/data/params.hpp`

## NNUE

#### Data Generation

Spawns parallel self-play workers to generate training data.

```bash
uv run -m nnue.datagen
uv run -m nnue.datagen --validation
```

#### Training

Trains a HalfKP neural network on the generated data.

```bash
uv run -m nnue.train
uv run -m nnue.train --weights none    # train from scratch
uv run -m nnue.train --weights 0       # resume from specific checkpoint
```

## Parameter Tuning

Uses SPSA to optimize all engine parameters simultaneously by playing matches between
perturbed configurations. Auto-stops on convergence.

```bash
uv run -m tune              # fresh tuning run
uv run -m tune --resume     # resume interrupted run
```

## Engine Matches

Requires `cutechess_cli_binary` environment variable pointing to the cutechess-cli binary.

#### `match/run_match.py`

```bash
uv run -m match.run_match              # current build vs latest version
uv run -m match.run_match v1           # current build vs v1
uv run -m match.run_match v1 v2        # v1 vs v2
uv run -m match.run_match --sprt 20    # SPRT test with 20 Elo expectation
```

#### `match/save_version.py`

```bash
uv run -m match.save_version my_version   # saves as v{n}_my_version
uv run -m match.save_version              # overwrites latest version
```