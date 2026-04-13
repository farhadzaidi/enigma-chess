# Enigma Chess

Enigma is a UCI chess engine written in C++. It plays well beyond human strength by combining a classical alpha-beta search with a neural network evaluation function trained on hundreds of millions of positions. 

[Check it out on Lichess!](https://lichess.org/@/enigma-chess-bot)

## Development Requirements

- CMake 3.16+
- A C++20 compiler

## Build

```bash
cmake -B build
cmake --build build
```

Binaries:

```bash
./build/enigma      # Engine (UCI)
./build/enigma-dev  # Tests and benchmarks
```

## Usage

Running the engine starts UCI mode:

```bash
./build/enigma
```

In addition to standard UCI commands, the engine supports two non-standard commands:

#### `perft <depth> [fen]`

Runs a perft node-count search. If FEN is omitted, the current position is used.

```
perft 5
perft 4 rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1
```

#### `search <depth> [fen]`

Runs a fixed-depth search. If FEN is omitted, the current position is used.

```
search 8
search 8 r1bqkbnr/pppp1ppp/2n5/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 2 3
```

## Dev

Tests and benchmarks are built as a separate binary. See [`dev/README.md`](dev/README.md) for details.

## Scripts

Utility scripts for data generation, version management, and engine matches. See [`scripts/README.md`](scripts/README.md) for details.

## License

This project is licensed under the [MIT License](LICENSE).
