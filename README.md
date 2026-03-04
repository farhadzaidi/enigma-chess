# Enigma Chess

Enigma is a UCI-compatible chess engine written in C++.  
[Play against it on Lichess!](https://lichess.org/@/enigma-chess-bot)

#### Lichess Ratings (as of 03/03/2026)
- Bullet: **2339** (95.6th percentile)
- Blitz: **2362** (98.8th percentile)

## Requirements

- CMake 3.16+
- A C++20 compiler

## Build

```bash
cmake -B build
cmake --build build
```

Binary path:

```bash
./build/enigma
```

## Usage

Running with no arguments starts UCI mode:

```bash
./build/enigma
```

You can also run utility modes directly:

```bash
./build/enigma <command> [args...]
```

#### `bench [flags...]`

Runs benchmark suites.

- By default, runs both move generation and engine search benches.
- Add flags to narrow scope or change output.

Flags:

- `--verbose`: print detailed per-position output
- `--fast`: run a reduced benchmark set
- `--phased`: use phased move generation in movegen bench
- `--movegen`: run only movegen bench
- `--engine`: run only engine bench

Examples:

```bash
./build/enigma bench
./build/enigma bench --fast
./build/enigma bench --movegen --phased
./build/enigma bench --engine --verbose
```

---

#### `perft <depth> [fen...]`

Runs a perft node-count search.

- `depth` is required and must be a positive integer.
- If FEN is omitted, start position is used.
- If FEN is provided, pass it as one quoted string.

Examples:

```bash
./build/enigma perft 5
./build/enigma perft 4 "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
```

---

#### `search <depth> [fen...]`

Runs a fixed-depth search and prints `Best move: <uci>`.

- `depth` is required and must be a positive integer.
- If FEN is omitted, start position is used.
- If FEN is provided, pass it as one quoted string.

Examples:

```bash
./build/enigma search 8
./build/enigma search 8 "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 2 3"
```

---

#### `test [selector...]`

Runs the built-in test suite.

- With no selectors, all tests run.
- Supported selectors include groups (`core`, `search`, `protocol`, `behavior`),
  and full selectors (`group/name`).

Examples:

```bash
./build/enigma test
./build/enigma test core
./build/enigma test core/zobrist search/move_selector
```

See [`tests/README.md`](tests/README.md) for test layout and selector details.

---

#### `debug`

Starts an interactive board debugger.

- Enter moves in UCI format (for example `e2e4`).
- Use `undo` to take back one move.
- Use `quit` to exit.

Example:

```bash
./build/enigma debug
```

## License

This project is licensed under the [MIT License](LICENSE).
