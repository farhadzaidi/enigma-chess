*Part 14 of 16 — [← Prev: NNUE Evaluation](nnue.md) | [Next: Time Management →](time-management.md)*

# NNUE Training

## The Self-Improvement Cycle

Training an NNUE network is a **feedback loop**: the engine generates training data by
playing itself, a neural network learns from that data, and the trained network goes back
into the engine. Each cycle produces slightly better evaluation, which produces slightly
better training data, which produces a slightly better network.

This is why NNUE engines can start from scratch (random weights) and eventually reach
superhuman strength through repeated self-play. The first generation of training data is
terrible — the engine barely plays legal chess with random weights — but each iteration
improves, and the improvements compound.

All training scripts live in `scripts/` and use `uv` for dependency management.

## Step 1: Data Generation

```bash
cd scripts
uv run -m nnue.datagen                # training data
uv run -m nnue.datagen --validation   # validation data
```

The engine plays games against itself at fixed depth 8, recording every position. For
each position, three things are saved:

1. **The board state** — which pieces are where, whose turn it is
2. **The search score** — what the engine currently thinks the position is worth (in
   centipawns, from the depth-8 search)
3. **The game outcome** — who won (or draw), determined after the game ends

### Why Self-Play?

The alternative is training on human games (from databases like CCRL or Lichess). Both
work:

- **Self-play** produces positions the engine actually encounters during search. The
  distribution of training positions matches the distribution of evaluation calls.
- **Human games** provide different perspectives — positions where human intuition
  matters, outcomes reflecting a wider range of play.

Enigma uses pure self-play. The engine plays at fixed depth for consistent-quality
scores (deeper search would give better scores but take exponentially longer per
position).

### Why Both Score and Outcome?

The **search score** is a rich per-position signal: "this exact position is worth +1.3
pawns." But it reflects the current (imperfect) evaluation function — it inherits whatever
biases the current network has.

The **game outcome** is objective ground truth: "white won this game." But it's noisy — a
position might be dead equal, yet white won because of a blunder 30 moves later.

Training uses both (see loss function below), getting the best of both worlds.

### Data Format

Each position is 36 bytes: 32 bytes for the board (nibble-encoded, 4 bits per square),
1 byte for side to move, 2 bytes for the search score, 1 byte for the outcome. A million
positions is ~36 MB.

### Parallelization

The script spawns one engine worker per CPU core (minus 2 reserved for the system). Each
worker plays independent games and writes to its own output file. Typical generation
takes a few hours and produces millions of positions.

Output goes to `scripts/nnue/data/{training,validation}/`.

## Step 2: Training

```bash
uv run -m nnue.train                   # resume from latest checkpoint
uv run -m nnue.train --weights none    # train from scratch (random init)
uv run -m nnue.train --weights 3       # resume from checkpoint 3
```

The PyTorch model mirrors the engine's architecture: 40,960 → 256 → 32 → 32 → 1.

### The Loss Function

The loss is a 50/50 blend of two objectives:

#### Eval Loss (50%)

```
eval_loss = MSE(sigmoid(prediction × σ), sigmoid(target × σ))
```

Both the network's output and the search score are passed through a sigmoid with a
scaling factor σ. The sigmoid compresses the score range:

- **Near-zero scores** (±50 cp): sigmoid is nearly linear, so the network sees the full
  gradient and learns precise differences between slightly better and slightly worse.
- **Large scores** (±500 cp): sigmoid saturates, compressing ±500 and ±1000 to nearly
  the same value. The network doesn't waste capacity distinguishing "winning" from
  "winning more" — both are just "winning."

This reflects chess reality: the difference between +0.3 and +0.5 matters (it might
change the best move), but the difference between +5.0 and +8.0 doesn't.

#### Outcome Loss (50%)

```
outcome_loss = MSE(sigmoid(prediction × σ), game_outcome)
```

The game outcome is 1.0 (white win), 0.5 (draw), or 0.0 (white loss). This anchors
the evaluation to actual game results.

Why this matters: if the engine's search has a systematic bias (overvalues bishop pairs,
undervalues passed pawns), the eval loss alone would perpetuate that bias. The outcome
loss provides an independent signal that corrects biases over time.

#### Why 50/50?

More eval weight → faster convergence (rich per-position signal), but inherits search
biases. More outcome weight → better calibration, but noisier gradients. 50/50 is the
standard starting point.

### Training Configuration

| Parameter | Value | Notes |
|-----------|-------|-------|
| Batch size | 16,384 | Large batches smooth out noise from individual positions |
| Learning rate | 1e-4 | Halved every 10 epochs |
| Weight decay | 1e-4 | Regularization to prevent overfitting |
| Gradient clipping | max norm 1.0 | Stabilizes early training |
| Max epochs | 100 | |
| Early stopping | 10 epochs | Without validation improvement |
| Device | CUDA / CPU | Auto-detected |

Checkpoints saved to `scripts/nnue/data/weights/weights_N.pt`.

### What the Network Learns

After training, the network has learned — from pure self-play — concepts that took human
chess knowledge centuries to formalize:

- Material values (queens worth more than pawns)
- Piece activity (centralized knights better than cornered ones)
- King safety (exposed kings are dangerous)
- Pawn structure (passed pawns valuable, doubled pawns weak)
- Piece coordination (bishop pairs, rooks on open files)

It also learns things hard to express in handcrafted rules: complex piece interactions,
positional compensation for material, initiative, and prophylaxis.

## Step 3: Weight Export

```bash
uv run -m generators.nnue
```

This reads the latest PyTorch checkpoint and quantizes float32 weights to the integer
types the engine uses:

| Layer | Scale factor | Output type | Notes |
|-------|-------------|-------------|-------|
| L1 weights | × Q1 (127) | int16 | The big one (~21 MB) |
| L1 bias | × Q1 | int16 | |
| L2/L3 weights | × Q2 (64) | int8 | Fits in L1 cache |
| L2/L3 bias | × Q1×Q2 (8128) | int32 | Pre-scaled to match matmul output |
| Output weights | × qeval | int16 | Bakes in centipawn conversion |
| Output bias | × Q1×qeval | int32 | |

The key trick: `qeval = Q2 / (Q1 × σ)` where σ is the sigmoid scaling factor from
training. This folds the centipawn conversion into the quantization, so the engine's
integer arithmetic produces centipawns directly.

The output is `src/data/nnue.bin` — about 21 MB. The binary is embedded into the
executable via `objcopy` at build time. Rebuild the engine after generating new weights.

### Quantization Overflow Check

The export script checks for overflow. The accumulator is int16, and the maximum possible
value is `max_bias + num_pieces × max_weight`. If this exceeds the int16 range (±32767),
the quantized network is invalid and training needs adjustment.

## The Full Pipeline

```bash
# build the engine
cmake -B build && cmake --build build

# save current version as baseline
cd scripts
uv run -m match.save_version baseline

# generate training data (takes hours)
uv run -m nnue.datagen &
uv run -m nnue.datagen --validation &
wait

# train
uv run -m nnue.train

# export and rebuild
uv run -m generators.nnue
cd .. && cmake --build build

# play a match against the baseline
cd scripts
uv run -m match.run_match
```

Typical iteration: generate a few million positions, train for 20-40 epochs, export,
play a match. If the new network wins, keep it as the baseline for the next round. If it
regresses, generate more data or adjust training.

Improvement per cycle diminishes as the network approaches its architectural capacity.
Early cycles might gain 50-100 Elo; later cycles might gain 5-10. At some point,
improvement requires a larger or differently structured network, better training data, or
improvements to the search itself.
