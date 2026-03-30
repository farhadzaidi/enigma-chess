*Part 13 of 16 — [← Prev: Advanced Search](advanced-search.md) | [Next: NNUE Training →](nnue-training.md)*

# NNUE Evaluation

**NNUE** (Efficiently Updatable Neural Network) is a small neural network that replaces
traditional handcrafted evaluation. Instead of hundreds of hand-tuned terms (piece-square
tables, king safety formulas, pawn structure penalties), a neural network trained on
millions of self-play positions discovers its own features. It learns that centralized
knights are good, that doubled pawns are weak, that king safety matters more in
middlegames — without being told any of this.

For background on what evaluation is and why handcrafted approaches have limitations, see
[Evaluation](eval.md). For the training pipeline, see [NNUE Training](nnue-training.md).

The NNUE implementation lives in `src/nnue.hpp` and `src/nnue.cpp`. Weights are compiled
into the binary as static arrays in `src/data/nnue_weights.hpp`.

## The Speed Problem

A standard neural network is far too slow for chess evaluation. The search calls eval
millions of times per second. Even a small multi-layer perceptron evaluated naively would
be orders of magnitude too slow.

NNUE solves this with three techniques that work together:

1. A **sparse input layer** that acts like an embedding bag (not a dense matmul)
2. **Small hidden layers** that fit entirely in L1 cache
3. **Integer arithmetic** with SIMD vectorization (no floating point)

Let's look at each in detail.

## The Input Layer: Embedding Bag vs. Dense Linear

This is the most important architectural decision in NNUE, and it's what makes the whole
thing work. Understanding it is key to understanding why NNUE is fast.

### The Naive Approach: Dense Linear Layer

In a standard neural network, the first layer would be a dense matrix multiply:

```
output[256] = weight[40960 × 256] × input[40960] + bias[256]
```

That's 40,960 × 256 = **10.5 million** multiply-add operations. At millions of evals per
second, this is completely infeasible. Even with SIMD, it would take hundreds of
microseconds per eval.

### The NNUE Approach: Sparse Lookup (Embedding Bag)

Here's the insight: the input vector is **extremely sparse**. Of the 40,960 possible
features, at most ~30 are active in any position (one per non-king piece). The input is
a binary vector — each feature is either 0 or 1, nothing in between.

A dense matmul multiplies the weight matrix by a mostly-zero vector. All those
multiplications by zero are wasted. Instead, we can just **sum up the weight rows for
the active features**:

```
accumulator = bias
for each active feature f:
    accumulator += weight_row[f]    // add one row of 256 values
```

With ~30 active features, that's 30 × 256 = **7,680** additions instead of 10.5 million
multiply-adds. A **1000× reduction**.

If you're familiar with PyTorch, this is exactly what `nn.EmbeddingBag` does — it takes a
list of indices and sums the corresponding rows of an embedding table. The NNUE first
layer is functionally an embedding bag, not a dense linear layer. The weight matrix is the
embedding table, the active feature indices are the "bag," and the output is the sum of
the selected rows.

### Why This Enables Incremental Updates

The embedding bag structure is what makes incremental updates possible. When a piece moves
from E4 to D6:

- The old feature (piece on E4) is deactivated → **subtract** its weight row
- The new feature (piece on D6) is activated → **add** its weight row

That's 2 × 256 = 512 additions/subtractions instead of recomputing from scratch (7,680
additions). A quiet move costs ~0.5 KB of memory operations. This is the core innovation
of NNUE — the first layer is maintained incrementally across moves instead of recomputed.

Without the embedding bag structure, incremental updates wouldn't make sense. A dense
linear layer has no concept of "adding one input" — changing any input affects the entire
output. But because our input is binary and sparse, changing one feature simply adds or
removes one row.

## HalfKP: The Feature Encoding

A neural network needs a fixed-size input. Chess positions are variable. The **feature
encoding** maps any position to a fixed set of binary features (active or inactive).

**HalfKP** (Half-King-Piece) represents the position as a set of (king square, piece)
pairs. The feature index is calculated in `src/nnue.cpp`:

```
index = king_square × 640 + piece_side × 320 + piece_type × 64 + piece_square
```

That's: **64** king squares × **2** piece sides × **5** piece types × **64** piece
squares = **40,960** features per perspective.

Kings are excluded from the piece set because they're already the indexing key. Each king
square gets its own 640-feature slice, so the network learns **completely independent
patterns** for different king positions.

### Why King-Relative Features?

The king's position is arguably the most important context for evaluation. A knight on E5
means something completely different when the enemy king is on G8 (the knight is
threatening) vs. when it's on A1 (the knight is far away).

By making every feature dependent on the king square, the network can learn:
- **Position-dependent piece values**: a knight near the enemy king is worth more
- **King safety patterns**: pawns in front of a castled king are critical
- **Endgame patterns**: king centralization matters when material is low

The price is a large first-layer weight table (40,960 × 256 × 2 bytes = ~21 MB), but
the accuracy gain justifies it.

### Why 5 Piece Types (Not 6)?

Kings are excluded. Since the king's square is already the top-level index of the feature
space, including king-king features would be redundant. The network already has full
information about both kings from the perspective structure.

### Alternative Feature Sets

Other feature sets exist:
- **Piece-square features** — simpler (768 features), smaller, but less expressive.
  Doesn't condition on king position.
- **HalfKA** (king-all) — includes king-king interactions. Used by some Stockfish
  versions.
- **HalfKAv2** — Stockfish's current approach, with virtual piece types that encode
  more context.

HalfKP is a good balance for a first NNUE implementation: expressive enough to capture
most important patterns, simple enough to understand and implement.

### Two Perspectives

White and black see the board differently. Rather than one set of features, each side
gets its own **accumulator** computed from its own viewpoint:

- **White's perspective**: features computed normally
- **Black's perspective**: squares flipped vertically (`square ^ 56` —
  `src/square.hpp:8`) and piece sides swapped

This "normalization" means the network input for black is exactly what white would see
if colors were swapped. The network learns patterns once and applies them to both sides.

The two accumulators are concatenated before the hidden layers, with the side to move's
accumulator always first: `[our_accumulator, their_accumulator]` → 512 inputs.

## Network Architecture

```
L1 (Accumulator):   40960 → 256     per perspective (embedding bag)
                     Concatenate → 512

L2 (Hidden):         512  → 32      Clipped ReLU
L3 (Hidden):          32  → 32      Clipped ReLU
Output:               32  → 1       Linear (centipawns)
```

### Why This Shape?

**L1 width (256)**: this is the critical dimension. The accumulator captures all
positional information — piece placement relative to kings. Too narrow and the network
can't represent enough patterns. Too wide and the weight table explodes (it scales as
40,960 × width × 2 bytes). 256 is the standard that most NNUE implementations use,
including Stockfish's earlier architectures. At 256, the L1 table is ~21 MB — large but
manageable.

**Hidden layers (32 neurons)**: these layers combine information from both perspectives
and distill it into a score. They're deliberately tiny so they fit in L1 cache (~16 KB
for L2 weights, ~1 KB for L3). Cache residency is critical when running millions of
evals per second. Even doubling to 64 neurons would push L2 out of L1 cache on some
architectures.

**Two hidden layers**: one hidden layer can learn XOR-like combinations of features (e.g.,
"knight on E5 AND opponent king on G8 → bonus"). Two hidden layers can learn more complex
interactions. Three would add diminishing returns for the inference cost.

**Linear output**: the output is a single centipawn score. No activation function —
the score can be any value (positive or negative, proportional to advantage).

### Why Not a Bigger Network?

The network is evaluated **millions of times per second**. Every nanosecond of inference
time costs search depth. The search compensates for evaluation inaccuracy by searching
deeper — and deeper search generally outweighs more accurate but slower evaluation.

In practice, a small fast network + deep search outperforms a large accurate network +
shallow search. Stockfish made this tradeoff too — their NNUE architecture is similarly
tiny compared to typical ML models.

The accumulator type is defined in `src/nnue.hpp:10,43-44`:

```
int16 accumulator[256]       one perspective (256 values)
int16 accumulators[2][256]   both perspectives (white and black)
```

## Incremental Updates in Detail

### The Make/Unmake Integration

The accumulator is maintained as a push/pop stack, synchronized with the search's
make/unmake cycle (see [Moves & Make/Unmake](moves.md)). The NNUE class in
`src/nnue.hpp:13-51` provides:

```cpp
void push();              // save current state before making a move
void pop();               // restore after unmaking
void add_feature(...);    // add a piece's contribution
void remove_feature(...); // remove a piece's contribution
void refresh_features(...); // recompute everything from scratch
```

The integration in `Board::make_move()` looks like:

1. `nnue_.push()` — save current accumulators
2. `nnue_.remove_feature(...)` — piece leaves origin square
3. `nnue_.remove_feature(...)` — captured piece disappears (if capture)
4. `nnue_.add_feature(...)` — piece arrives at destination
5. For king moves: `nnue_.refresh_features(...)` — full recompute

On `Board::unmake_move()`: just `nnue_.pop()` — restore the saved state.

### Cost Per Move Type

| Move type | Feature updates | Cost (adds/subs of 256 int16s) |
|-----------|----------------|-------------------------------|
| Quiet move | remove + add | 2 × 256 = 512 operations |
| Capture | remove + remove + add | 3 × 256 = 768 operations |
| Castling | 2 removes + 2 adds | 4 × 256 = 1024 operations |
| King move | full refresh (~30 features) | ~30 × 256 = 7680 operations |

King moves are expensive because every feature index includes the king square. When the
king moves, ALL feature indices change — there's no incremental shortcut. This is the
main drawback of king-relative feature sets.

Fortunately, kings move relatively rarely in the search tree (especially in the
middlegame where search is deepest), so the amortized cost is low.

### The Accumulator Stack

The stack in `src/nnue.hpp:47`:

```cpp
vector<Accumulators> accumulator_stack;
```

Each entry is 1 KB (2 perspectives × 256 values × 2 bytes). The push/pop is a memcpy
of 1 KB. At search depths of 20-30 plies, the stack uses 20-30 KB — trivially small.

## Quantization: Integer Inference

The entire forward pass (L2 through output) uses **integer arithmetic**. No floating point
at inference time. This is both faster and more cache-friendly than float32.

### Why Integer Instead of Float?

Three reasons:

**1. SIMD throughput.** On x86, the `_mm256_maddubs_epi16` instruction multiplies 32
pairs of uint8 × int8 values and horizontally sums adjacent results into int16 — all in
one instruction. The equivalent float operation (`_mm256_fmadd_ps`) processes only 8
float32 values. Integer SIMD gives **4× more operations per instruction** for our small
operand sizes.

**2. Memory savings.** int8 weights are 4× smaller than float32. L2's weight matrix
(32 × 512) is 16 KB as int8 vs. 64 KB as float32. At 16 KB, the entire matrix fits in
L1 cache. At 64 KB, it might not. Cache misses are devastating at millions of evals per
second.

**3. Determinism.** Integer arithmetic is always exact (no rounding). Float arithmetic
can produce slightly different results depending on instruction ordering, which can cause
non-deterministic search behavior.

### The Q1/Q2 Scheme

Two quantization constants govern the fixed-point math:

**Q1 = 127** — the clipped ReLU ceiling. After the activation function, all values are
clamped to [0, 127] and stored as uint8. Why 127? It's the largest value that fits in a
signed 8-bit integer while leaving room for the sign bit in weight multiplication. When
you multiply a uint8 input (0-127) by an int8 weight (-128 to 127), the product fits
comfortably in int16.

**Q2 = 64** — the inter-layer scale factor. It's a power of 2 so that division is a
simple bit shift (>> 6). After each matmul, the accumulated int32 result is right-shifted
by 6 to bring it back to the Q1 range.

### Step-by-Step Forward Pass

Here's what happens for one hidden layer (e.g., L2: 512 → 32):

1. **Input preparation**: the concatenated accumulator (512 int16 values) is clamped to
   [0, 127] and packed into uint8. This is the clipped ReLU activation.

2. **Matrix multiply**: for each of the 32 output neurons:
   ```
   sum = bias[neuron]              // int32, pre-scaled to Q1 × Q2
   for i in 0..511:
       sum += input[i] * weight[neuron][i]   // uint8 × int8 → int16, accumulated in int32
   ```

3. **Rescale**: `sum >>= 6` (divide by Q2). Now the value is back in the Q1 range
   (~0-127 for typical values, but can exceed 127 before clamping).

4. **Clipped ReLU**: `output = clamp(sum, 0, 127)`. This is the activation function —
   standard ReLU (zero out negatives) with a ceiling at 127.

5. **Pack to uint8**: the clamped value is cast to uint8 for the next layer's input.

This process repeats for L3 (32 → 32). The output layer is slightly different — it
produces a single int32 score that's right-shifted to centipawns (the centipawn conversion
factor is baked into the output weights during export).

### Centipawn Conversion

The output layer weights are quantized with a special factor during weight export:

```
qeval = Q2 / (Q1 × sigmoid_scale)
```

This folds the centipawn conversion into the quantization itself. The engine's final
right-shift produces centipawns directly — no floating-point post-processing needed.
See [NNUE Training](nnue-training.md) for the export process.

## AVX2 Vectorization

When compiled with AVX2 (`-march=native` on modern x86), the hot paths in `src/nnue.cpp`
use SIMD intrinsics for significant speedup.

### Accumulator Updates

```cpp
_mm256_add_epi16(accumulator_chunk, weight_row_chunk)
_mm256_sub_epi16(accumulator_chunk, weight_row_chunk)
```

These process 16 int16 values per instruction. The 256-element accumulator update takes
16 iterations (256 / 16 = 16). Total: a few nanoseconds.

### Clipped ReLU (int16 → uint8)

Clamping and packing uses:
- `_mm256_max_epi16` — clamp negative values to 0
- `_mm256_min_epi16` — clamp values above 127
- `_mm256_packus_epi16` — pack int16 pairs into uint8
- A permute to fix AVX2's lane-crossing behavior (the pack instruction interleaves 128-bit
  lanes in a non-intuitive way)

### Hidden Layer Matmul

The `maddubs` + `madd` pipeline:

1. `_mm256_maddubs_epi16(input_u8, weight_s8)` — multiplies 32 uint8 × int8 pairs and
   sums adjacent results into 16 int16 values
2. `_mm256_madd_epi16(result, ones)` — sums int16 pairs into 8 int32 values
3. Horizontal reduction to a single int32 scalar

This gives 32 multiply-accumulate operations per two-instruction pair. For a 512-input
neuron, the entire dot product takes ~32 instruction pairs.

### Output Dot Product

`_mm256_madd_epi16` on int16 × int16 (both accumulator values and output weights are
int16), reduced to a scalar with horizontal adds.

### Scalar Fallback

All SIMD paths have scalar fallbacks, so the engine runs on ARM or older x86 without
AVX2 — just 4-8× slower for NNUE inference. The compile-time check in
`src/nnue.cpp:4-6`:

```cpp
#if defined(__AVX2__)
#include <immintrin.h>
#endif
```

(The bitboard code has a separate `__BMI2__` guard for the PEXT instruction used in magic
bitboard lookups — that's a different feature flag. AVX2 and BMI2 are typically present
together on modern x86, but they're independent capabilities.)

## Where NNUE Fits in the Search

The search calls NNUE evaluation at **leaf nodes** — positions where the engine stops
searching deeper and needs a static score. This happens in two places: at the depth
limit of the main search, and throughout quiescence search (see
[Advanced Search](advanced-search.md)). Pruning decisions in
[Pruning & Extensions](pruning.md) also use the eval score to decide whether to skip
branches.

Because the search explores millions of nodes per second, and most nodes eventually
need an eval, NNUE inference is one of the hottest paths in the entire engine.

## Calling the Evaluation

The eval is called through `Board::nnue_evaluate()` (`src/board.hpp:100`):

```cpp
PositionScore nnue_evaluate() { return nnue_.evaluate(to_move_); }
```

The `evaluate(side)` function in `src/nnue.hpp:40`:
1. Concatenates the two perspective accumulators (side-to-move first)
2. Applies clipped ReLU to produce the L2 input
3. Runs the L2, L3, and output layer forward pass
4. Returns the result as `PositionScore` (int16)

The return type `PositionScore` is `int16_t` (`src/types.hpp:17`), with a practical range
of roughly ±32000 centipawns. Scores near ±32000 represent checkmate (see
`CHECKMATE_SCORE` in `src/engine.hpp:21`).

## Weight Storage

Weights are compiled into the binary as static arrays in `src/data/nnue_weights.hpp`,
all `alignas(64)` for SIMD alignment:

| Array | Type | Shape | Size | Notes |
|-------|------|-------|------|-------|
| `NNUE_L1_WEIGHT` | int16 | 40960 × 256 | ~21 MB | The embedding table |
| `NNUE_L1_BIAS` | int16 | 256 | 512 B | Accumulator initial values |
| `NNUE_L2_WEIGHT` | int8 | 32 × 512 | 16 KB | Fits in L1 cache |
| `NNUE_L2_BIAS` | int32 | 32 | 128 B | Pre-scaled to Q1×Q2 |
| `NNUE_L3_WEIGHT` | int8 | 32 × 32 | 1 KB | |
| `NNUE_L3_BIAS` | int32 | 32 | 128 B | Pre-scaled to Q1×Q2 |
| `NNUE_OUTPUT_WEIGHT` | int16 | 32 | 64 B | Includes centipawn conversion |
| `NNUE_OUTPUT_BIAS` | int32 | 1 | 4 B | |

The 21 MB L1 table dominates. This is the price of a per-king-square feature set — 64
king squares × 640 features × 256 accumulator values × 2 bytes. The engine binary is
~25 MB, mostly NNUE weights.

L2 and L3 weights together are ~17 KB — small enough to stay resident in L1 cache
throughout the search. This is critical for performance: an L1 cache miss (going to L2
cache) adds ~5 nanoseconds, and an L2 miss (going to main memory) adds ~50-100
nanoseconds. At millions of evals per second, cache misses would be catastrophic.

See [NNUE Training](nnue-training.md) for how these weights are produced from the training
pipeline.

## Building Your Own

NNUE is an advanced topic. If you're writing your first engine, start with a
[handcrafted evaluation](eval.md) — material counting plus piece-square tables will get
you surprisingly far, and the infrastructure is much simpler (no training pipeline, no
data generation, no quantization).

When you're ready to add NNUE, the key decisions are:

- **Feature set**: HalfKP (what Enigma uses) is a good starting point. Simpler
  piece-square features (768 inputs) are easier to implement but weaker.
- **Accumulator width**: 256 is the standard. Wider is more expressive but uses more
  memory and is slower to refresh on king moves.
- **Hidden layer size**: keep it tiny (32 neurons). The search compensates for eval
  inaccuracy — a fast, shallow network plus deep search beats a slow, accurate network
  plus shallow search.

The training pipeline is covered in [NNUE Training](nnue-training.md) and the practical
workflow in [Tooling & Workflow](tooling.md).
