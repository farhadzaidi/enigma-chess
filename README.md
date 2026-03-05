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

Binaries:

```bash
./build/enigma        # Engine (UCI)
./build/enigma-debug  # Tests and benchmarks
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

## Debug Binary

Tests and benchmarks are built as a separate binary. See [`debug/README.md`](debug/README.md) for details.

## License

This project is licensed under the [MIT License](LICENSE).
