# Debug

`enigma-dev` is a separate binary for running tests and benchmarks against the engine.

## Build

```bash
cmake -B build
cmake --build build --target enigma-dev
```

## Layout

```text
dev/
  main.cpp        # Entry point
  parse.hpp       # File I/O and EPD parsing utilities
  bench/
    bench.hpp     # Bench runner
    movegen.hpp   # Movegen benchmark
    search.hpp    # Search benchmark
  tests/
    test.hpp      # Test runner and registration
    helpers.hpp   # Shared test helpers
    suite/        # Test case files
```

## Tests

```bash
./build/enigma-dev test
```
Run all tests.

```bash
./build/enigma-dev test zobrist
./build/enigma-dev test zobrist perft
```
Run by name. Multiple names can be combined in one command.

## Bench

```bash
./build/enigma-dev bench
```
Run both movegen and search benchmarks.

```bash
./build/enigma-dev bench movegen
./build/enigma-dev bench search
```
Run a specific benchmark.

Flags:

- `--fast`: reduced position set
- `--verbose`: detailed per-position output
- `--phased`: use phased move generation (movegen only)

```bash
./build/enigma-dev bench --fast
./build/enigma-dev bench movegen --phased --verbose
./build/enigma-dev bench search --fast
```
