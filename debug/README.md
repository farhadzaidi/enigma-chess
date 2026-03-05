# Debug Binary

`enigma-debug` is a separate binary for running tests and benchmarks against the engine.

## Build

```bash
cmake -B build
cmake --build build --target enigma-debug
```

## Layout

```text
debug/
  main.cpp        # Entry point
  bench.hpp       # Benchmark suites (movegen + engine)
  io.hpp          # File I/O and EPD parsing utilities
  tests/
    test.hpp      # Test runner and registration
    helpers.hpp   # Shared test helpers
    core/         # Board legality, hashing, state invariants, tables
    search/       # Move ordering, eval, limits, outcome-oriented behavior
    protocol/     # SAN/UCI parsing and command semantics
```

## Tests

```bash
./build/enigma-debug test
```
Run all tests.

```bash
./build/enigma-debug test core
./build/enigma-debug test search
./build/enigma-debug test protocol
```
Run by group.

```bash
./build/enigma-debug test core/zobrist
./build/enigma-debug test core/zobrist search/move_selector
```
Run by selector. Multiple selectors can be combined in one command.

## Bench

```bash
./build/enigma-debug bench
```

Modes (mutually exclusive):

- `--movegen`: run only movegen bench
- `--engine`: run only engine bench

Flags:

- `--fast`: reduced position set
- `--verbose`: detailed per-position output
- `--phased`: use phased move generation (movegen only)

```bash
./build/enigma-debug bench --fast
./build/enigma-debug bench --movegen --phased --verbose
./build/enigma-debug bench --engine --fast
```
