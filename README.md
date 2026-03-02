# Enigma Chess

Enigma is a UCI-compatible chess engine written in C++20.

## Requirements

- CMake 3.16+
- A C++20 compiler (GCC, Clang, or MSVC)

## Build

```bash
cmake -S . -B build
cmake --build build -j
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

#### `test [test_name...]`

Runs the built-in test suite.

- With no names, all tests run.
- With one or more names, only those tests run.

Available test names:

- `in_check`
- `san_parsing`
- `zobrist`
- `opening_book`
- `game_end`
- `transposition_table`

Examples:

```bash
./build/enigma test
./build/enigma test zobrist transposition_table
```

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

## Future Enhancements

Planned roadmap items:

### Search

- Check extensions

### Evaluation

- King safety terms (pawn shield, open files, nearby attackers)
- Rooks (on 2nd/7th rank, behind passed pawn, connected rooks)
- Piece mobility (open files/diagnols for rooks, bishops, queens)
- Knight outpost


## License

This project is licensed under the [MIT License](LICENSE).
