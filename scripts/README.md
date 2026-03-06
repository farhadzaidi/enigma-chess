# Scripts

Utility scripts for building, testing, and managing engine versions. Managed with [uv](https://docs.astral.sh/uv/).

## Setup

```bash
uv sync
```

## Scripts

#### `run_match.py`

Runs a match between engine versions using cutechess-cli. Requires `cutechess_cli_binary` env var.

```bash
uv run run_match.py              # current build vs latest version
uv run run_match.py v1           # current build vs v1
uv run run_match.py v1 v2        # v1 vs v2
uv run run_match.py --sprt 20    # SPRT test with 20 Elo expectation
```

#### `save_version.py`

Saves the current build binary to the versions folder.

```bash
uv run save_version.py my_version   # saves as v{n}_my_version
uv run save_version.py              # overwrites latest version
```

## Data Generators

These scripts generate data that is embedded directly into the engine binary at compile time. The generated files are checked in and don't need to be rerun unless the source data changes.

- `generators/book.py` — generates `src/data/book.hpp` from `positions/games.san`

```bash
uv run generators/book.py
```
