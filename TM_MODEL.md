# Time Management Model

Replace the handwritten time management rules with a small neural network that learns optimal time allocation from self-play.

## Motivation

The current time management uses hardcoded constants and heuristics (moves_left estimation, emergency scaling, etc.). These are crude and don't adapt to position characteristics. A learned model can discover its own time allocation strategy — spending more in complex positions, conserving in simple ones — without us defining the rules.

## Architecture

Single hidden layer neural network: **8 inputs -> 8 hidden neurons (ReLU) -> 2 outputs (sigmoid)**.

82 total parameters (64 weights + 8 biases in hidden layer, 16 weights + 2 biases in output layer).

### Inputs

All inputs are normalized to roughly 0-1 range:

| # | Input | Normalization | Purpose |
|---|-------|---------------|---------|
| 1 | remaining clock | remaining / initial_clock | How much time is left |
| 2 | increment | increment / initial_clock | How helpful the increment is |
| 3 | our pawns | count / 8 | Material: pawn structure |
| 4 | their pawns | count / 8 | Material: opponent pawns |
| 5 | our pieces | count / 7 | Material: non-pawn non-king |
| 6 | their pieces | count / 7 | Material: opponent pieces |
| 7 | last eval | clamp(eval, -1000, 1000) / 1000 | Are we winning or losing |
| 8 | eval change | clamp(delta, -1000, 1000) / 1000 | Tactical complexity — did the eval swing? |

These replace the `game_phase` heuristic. The model learns what "game phase" means from raw piece counts rather than us defining it. Eval and eval change are 0 on the first move (no previous search).

### Output

Two sigmoid outputs produce time fractions in (0, 1), clamped and multiplied by remaining clock:

```
soft_fraction = sigmoid(output[0])
hard_fraction = sigmoid(output[1])
soft_limit = remaining * clamp(soft_fraction, 0.01, 0.5)
hard_limit = remaining * clamp(hard_fraction, soft_fraction, 0.9)
```

The model learns both limits jointly — it can produce a tight hard limit (low clock, no increment) or a loose one (plenty of time, complex position) depending on the situation.

### Inference (C++)

Two matrix multiplications, ~80 multiply-adds total. Runs once per search at the start. Negligible cost compared to search.

```cpp
// Layer 1: ReLU(W1 * inputs + b1)
double hidden[8];
for (int i = 0; i < 8; i++) {
    hidden[i] = l1_bias[i];
    for (int j = 0; j < 8; j++)
        hidden[i] += l1_weights[j][i] * inputs[j];
    hidden[i] = std::max(0.0, hidden[i]);
}

// Layer 2: sigmoid(W2 * hidden + b2) -> soft and hard time fractions
double out[2];
for (int k = 0; k < 2; k++) {
    out[k] = l2_bias[k];
    for (int i = 0; i < 8; i++)
        out[k] += l2_weights[i][k] * hidden[i];
    out[k] = 1.0 / (1.0 + std::exp(-out[k]));
}
```

## Training

### Why not gradient descent?

The loss function (win rate) is not differentiable through the game. The chain is:

```
model weights -> time allocation -> engine plays game -> outcome
```

We can't backpropagate through "engine plays a chess game," so we use a black-box optimizer instead.

### CMA-ES (Covariance Matrix Adaptation Evolution Strategy)

CMA-ES is well-suited for optimizing 50-100 continuous parameters with a noisy objective:

1. Maintain a multivariate gaussian distribution over the 82 weights
2. Each generation: sample ~30 candidate weight vectors from the distribution
3. Each candidate plays matches against a baseline (the default/current best weights)
4. Top candidates pull the distribution's mean toward them
5. The covariance matrix adapts — learning which weights are correlated and should move together
6. Repeat until convergence

CMA-ES is preferred over:
- **SPSA**: treats weights independently, requires manual step size tuning
- **TPE (Optuna default)**: models parameters independently, misses weight correlations
- **Gradient-based**: not possible with a non-differentiable objective

Implementation: use Optuna with `CmaEsSampler`, reusing the same match infrastructure as the engine parameter tuner.

### Training time controls

To generalize across time controls, training matches use a variety of TCs that preserve the ratios of common online formats but are scaled down for fast training:

```
0.5+0       (1+0 bullet)
0.5+0.25    (2+1 bullet)
0.75+0      (3+0 blitz)
0.75+0.5    (3+2 blitz)
1+0         (5+0 blitz)
1+0.6       (5+3 blitz)
```

Since the model sees `increment / initial_clock` as input, a 0.5+0.25 game looks the same as a 2+1 game to the model.

### Convergence

Training runs until the best score hasn't improved in 30 trials (patience-based stopping). The objective is win rate against a fixed baseline.

## Integration with other models

The engine has three learned components, trained in order:

```
1. NNUE          (position evaluation, trained with gradient descent on self-play data)
2. TM model      (time allocation, trained with CMA-ES on match outcomes)
3. Search params  (pruning/reduction constants, tuned with Optuna TPE on match outcomes)
```

Each depends on the layers below it. Retrain bottom-up: NNUE first (independent), then TM (needs good eval), then search params (needs both). The full cycle only reruns when NNUE is retrained; otherwise just redo steps 2-3.

