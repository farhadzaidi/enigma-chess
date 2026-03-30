*Part 1 of 16 — [Next: Bit Manipulation →](bit-manipulation.md)*

# Architecture Overview

## What Is a Chess Engine?

A chess engine is a program that plays chess. You give it a position (the current state of
the board) and it tells you the best move. That's it. Everything else — the board
representation, the search algorithm, the evaluation function, the time management — exists
to answer that one question as accurately as possible within a time budget.

Chess engines don't have a graphical interface. They're command-line programs that
communicate with a GUI (like Arena, CuteChess, or Lichess) through a text protocol called
**UCI** (Universal Chess Interface). The GUI sends positions and time controls over
stdin; the engine thinks and replies with a move over stdout. This separation means you
can focus entirely on the chess logic without worrying about drawing boards or handling
mouse clicks.

Here's what a typical UCI conversation looks like:

```
GUI  → uci
Engine → id name Enigma
Engine → id author ...
Engine → uciok

GUI  → isready
Engine → readyok

GUI  → position startpos moves e2e4 e7e5
GUI  → go wtime 300000 btime 300000
Engine → info depth 1 score cp 30 nodes 42 time 1 pv d2d4
Engine → info depth 2 score cp 15 nodes 184 time 1 pv d2d4 d7d5
...
Engine → bestmove d2d4 ponder d7d5
```

The UCI implementation lives in `src/uci.cpp`. The entry point is in `src/main.cpp`,
which reads commands from stdin and dispatches them.

## The Three Pillars

Every chess engine, from a beginner project to Stockfish, is built on the same three
components. Understanding these three things gives you the mental model for the entire
program.

### 1. Board Representation

Before the engine can do anything, it needs a way to store a chess position in memory.
This means tracking:

- Where every piece is
- Whose turn it is
- Whether each side can still castle
- Whether en passant is available
- How many moves since the last pawn move or capture (for the 50-move draw rule)

The most important design choice is **how** you store piece positions. The two main
approaches are:

- **Mailbox**: an array of 64 squares, each storing what piece (if any) is on it. Simple
  and intuitive, but answering questions like "which of my pieces are attacked?" requires
  looping over squares one at a time.

- **Bitboards**: 64-bit integers where each bit represents a square. An entire set of
  squares — all white pawns, all squares a knight attacks — fits in a single machine
  word. Set operations (intersection, union, complement) become single CPU instructions.

Enigma uses bitboards. The board representation lives in `src/board.hpp` and
`src/board.cpp`. See [Bitboards](bitboards.md), [Board State](board.md), and
[Moves & Make/Unmake](moves.md) for the full details.

### 2. Search

Given unlimited time, you'd search every possible continuation of the game and find the
theoretically perfect move. In practice, the game tree is astronomically large — roughly
10^120 possible games, far more than atoms in the observable universe. So the engine
searches as deep as time allows and relies on smart heuristics to focus on the most
promising branches.

The core algorithm is **alpha-beta search** with **iterative deepening**: search to depth
1, then depth 2, then 3, and so on until time runs out. Each deeper iteration builds on
the previous one, reusing information about which moves are good. At each depth, a
cascade of **pruning** and **reduction** techniques trims the tree aggressively — skipping
branches that are almost certainly bad, and spending less time on moves that are probably
worse than what we've already found.

Good **move ordering** (searching the best move first) is what makes alpha-beta work. If
you always search the best move first, alpha-beta can prune the vast majority of branches
without examining them. If you search in random order, it barely helps at all.

A **transposition table** (a big hash table) caches search results so that positions
reached by different move orders aren't searched twice. This is the single biggest
performance win after alpha-beta itself.

The search implementation is in `src/engine.hpp` and `src/engine.cpp`. See
[Search](search.md), [Move Ordering](move-ordering.md), [Pruning](pruning.md),
[Transposition Table](transposition-table.md), and
[Advanced Search](advanced-search.md).

### 3. Evaluation

At leaf nodes of the search tree (and whenever pruning decides a subtree isn't worth
exploring), the engine needs a score: "how good is this position for the side to move?"

Simple evaluation could just count material (each queen is 9, rook 5, bishop/knight 3,
pawn 1). But that misses everything about position — king safety, pawn structure, piece
activity, control of the center. Traditional engines used **handcrafted evaluation (HCE)**:
hundreds of manually tuned terms for every positional factor a human could think of.

Enigma uses **NNUE** (Efficiently Updatable Neural Network) — a small neural network
(~10M parameters) trained on millions of self-play positions. The network takes the
position as input and outputs a centipawn score. It discovers its own features — it learns
that centralized knights are good, that doubled pawns are weak, that king safety matters
more in middlegames — without being told any of this explicitly.

The key trick is **incremental updates**: the expensive first layer of the network is
maintained as pieces move, not recomputed from scratch. This makes neural evaluation fast
enough to call millions of times per second.

The NNUE implementation lives in `src/nnue.hpp` and `src/nnue.cpp`. See
[Evaluation](eval.md), [NNUE Evaluation](nnue.md), and
[NNUE Training](nnue-training.md).

## How They Fit Together

When you tell the engine to think about a position (the `go` command in UCI), here's what
happens:

1. **Book check** — look up the position in the opening book; if found, return
   immediately.
2. **Time allocation** — convert the clock into soft and hard time limits.
3. **Iterative deepening** — search to depth 1, then 2, then 3, and so on until time
   runs out.
4. **Alpha-beta with pruning** — at each node, probe the transposition table, try
   pruning heuristics, then search remaining moves in order from most to least promising.
5. **NNUE evaluation** — at leaf nodes, the neural network scores the position.
6. **Result** — report the best move.

Each of these steps is covered in its own doc — follow the reading order below.

## Design Choices

Some decisions that shape Enigma's architecture:

**Legal-only move generation** instead of pseudo-legal + filter. Computes pins and checks
up front so every generated move is guaranteed legal. More complex move generator, but no
wasted make/unmake on illegal moves during search. (See `src/move_generator.cpp`)

**Pure NNUE evaluation.** No handcrafted eval, no piece-square tables, no explicit king
safety terms. All positional knowledge comes from the trained network. This simplifies
the codebase but means the engine is only as good as its training data and network
architecture.

**Compiled-in data.** Magic numbers, opening book, NNUE weights, and tuned parameters
are all generated by Python scripts into C++ headers. The engine has zero runtime
dependencies — no data files to load, no configuration to parse. The tradeoff is a large
binary (~25 MB, mostly NNUE weights) and a rebuild whenever you retrain.

**Lazy SMP threading.** Each thread runs an independent search, sharing only the
transposition table. No work splitting, no lock-free queues, no complex synchronization.
This is simpler than the alternatives and scales well up to moderate thread counts.

**Single source file for search.** The search, move ordering, and pruning all live in
`engine.cpp`. Not great for navigation, but good for the compiler — it can see the
entire hot path in one translation unit and optimize aggressively.

## File Layout

```
src/
├── types.hpp              — fundamental types (Bitboard, Square, Piece, Side, etc.)
├── bitboard.hpp           — bitboard shifts, masks, bit manipulation
├── square.hpp             — square arithmetic (rank, file, flip)
├── move.hpp               — 16-bit move encoding, MoveList
├── board.hpp/cpp          — board state, make/unmake, Zobrist hashing
├── move_generator.hpp/cpp — legal move generation, magic bitboards
├── engine.hpp/cpp         — search, move ordering, pruning
├── nnue.hpp/cpp           — NNUE evaluation, incremental updates, AVX2
├── transposition_table.hpp/cpp
├── opening_book.hpp/cpp
├── zobrist.hpp/cpp        — hash key tables
├── uci.hpp/cpp            — UCI protocol, time management
├── notation.hpp/cpp       — move parsing/formatting (e.g., "e2e4" ↔ Move)
├── print.hpp/cpp          — debug output utilities
├── main.cpp               — entry point
└── data/                  — auto-generated headers (don't edit by hand)
    ├── magics.hpp         — magic numbers for sliding pieces
    ├── book.hpp           — opening book data
    ├── nnue_weights.hpp   — quantized NNUE network (~21 MB)
    ├── search_params.hpp  — tuned search constants
    └── tm_params.hpp      — tuned time management constants
```

Python tooling in `scripts/`: NNUE training, parameter tuning (Optuna), match automation
(CuteChess). Tests and benchmarks in `dev/`, compiling to `enigma-dev`.

## Building Your Own Engine

If you're reading these docs because you want to write your own chess engine, here's a
suggested implementation order:

1. **Board representation and move generation.** Get this working first, and verify it
   with perft (see [Move Generation](movegen.md)). Nothing else works without correct
   move generation. Start with pseudo-legal generation if you prefer — it's simpler and
   you can switch to legal-only later.

2. **Basic search.** Implement negamax with alpha-beta and iterative deepening. Use a
   simple material-counting eval. The engine will play legal chess at this point, just
   badly.

3. **Handcrafted evaluation.** Add piece-square tables. This single addition typically
   gains hundreds of Elo. Then add terms one at a time (see [Evaluation](eval.md)),
   testing each with matches to confirm it actually helps.

4. **Transposition table and move ordering.** These are the biggest search speed wins.
   A TT alone can double your effective search depth.

5. **Pruning and reductions.** Null move pruning, late move reductions, and futility
   pruning. Each one lets the engine search deeper in the same time.

6. **NNUE (optional).** This is a big step — it requires a training pipeline, self-play
   data generation, and quantized inference. But it replaces all your handcrafted eval
   terms with a single neural network that's typically much stronger. Don't attempt this
   until everything else is solid.

At each step, save a version and run matches against the previous version. If a change
doesn't measurably improve play, revert it — even ideas that "should" help sometimes
don't. See [Tooling & Workflow](tooling.md) for how to run matches and measure
improvement.
