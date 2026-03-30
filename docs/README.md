# Enigma Chess Engine

Enigma is a UCI chess engine written. These docs walk through the ideas behind
chess programming — how engines represent the board, search for the best move, and
evaluate positions — using Enigma's source code as a concrete example.

If you've ever wondered "how does a chess engine actually work?", you're in the right
place. Maybe you're thinking about writing your own engine. Maybe you're curious about
the algorithms behind Stockfish or Leela. Maybe you just want to understand how a
computer plays chess.

These docs assume you know how to play chess and have some programming experience, but **no prior
knowledge of chess programming**. We start from the very basics — how integers work in
binary, what a "bitboard" is, how to represent a chess position in memory — and build up
to the advanced techniques that make modern engines strong.

The chess programming wiki (chessprogramming.org) is a good reference for "what exists,"
but it's thin on the "why" and "how to actually build this." These docs try to fill that
gap — enough depth to actually implement the ideas, with references to the actual code so
you can see how theory becomes practice.

## Reading Order

You don't need to read these linearly, but they roughly build on each other. If you're
completely new, follow the order below.

### Getting Started

1. **[Architecture Overview](overview.md)** — the big picture: what are the components of
   a chess engine and how do they interact? Start here.

### Foundations

2. **[Bit Manipulation](bit-manipulation.md)** — how integers are stored in memory
   (signed/unsigned, LSB/MSB) and the bit manipulation tricks you'll need for bitboards.
   Skip this if you already know binary arithmetic.

3. **[Bitboards](bitboards.md)** — the 64-bit integer trick that makes chess engines fast.
   Starts with the basics, then builds up to magic bitboards for sliding pieces.

### Board Representation

4. **[Board State](board.md)** — the engine's model of a position: simpler representations
   (mailbox), why engines graduate to bitboards, Enigma's three parallel views, castling
   rights, en passant, FEN parsing, and game phase.

5. **[Moves & Make/Unmake](moves.md)** — how moves are encoded in 16 bits, the make/unmake
   pattern that lets the search explore millions of positions per second, the undo state,
   and repetition detection.

### Move Generation

6. **[Move Generation](movegen.md)** — given a position, what are all the legal moves?
   Covers pin detection, check evasion, the tricky edge cases in pawn and castling logic,
   and why generating only legal moves is worth the extra complexity.

### Evaluation Basics

7. **[Evaluation](eval.md)** — what is evaluation and why does it matter? Covers material
   counting, piece-square tables, king safety, pawn structure, tapered eval, and the
   limitations that motivated the shift to neural networks.

### Search

8. **[Search Fundamentals](search.md)** — starts with the basic idea (look ahead and pick
   the best move), then builds through minimax, alpha-beta pruning, and iterative
   deepening.

9. **[Transposition Table](transposition-table.md)** — caching search results to avoid
   redundant work. Zobrist hashing, TT entries, replacement policy, and thread safety.

10. **[Move Ordering](move-ordering.md)** — why searching the best move first matters so
    much. Staged generation, killer moves, history tables, countermove tables, and SEE.

11. **[Pruning & Extensions](pruning.md)** — the heuristics that let the engine skip
    unpromising branches and spend extra time on critical ones. Null move pruning, futility
    pruning, late move reductions, singular extensions, and more.

12. **[Advanced Search](advanced-search.md)** — PVS, aspiration windows, quiescence search,
    the full negamax walkthrough, and Lazy SMP threading.

### Neural Network Evaluation

13. **[NNUE](nnue.md)** — how Enigma actually evaluates positions. The embedding bag
    architecture, HalfKP features, incremental updates, quantization, and SIMD
    vectorization.

14. **[NNUE Training](nnue-training.md)** — the self-play pipeline that produces the neural
    network: data generation, the training loop, the loss function, and weight export.

### Infrastructure

15. **[Time Management](time-management.md)** — deciding when to stop thinking. Harder
    than it sounds when you don't know how many moves are left in the game.

16. **[Tooling & Workflow](tooling.md)** — building, testing, benchmarking, parameter
    tuning, and running matches.
