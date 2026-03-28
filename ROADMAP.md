# Search Roadmap

Improvements to the search, organized by priority. All new constants are exposed as UCI options and tuned automatically with the parameter tuner.

---

## Phase 1: New Techniques (High Impact)

### Reverse Futility Pruning (Static Null Move Pruning)

At non-PV, non-check nodes at low depth, if the static eval is already far above beta, prune the entire node without searching moves. The idea: if we're already winning by a large margin, no move from the opponent can change that.

```cpp
if (!is_pv_node && !in_check && depth <= rfp_max_depth) {
    PositionScore static_eval = board.nnue_evaluate();
    if (static_eval - rfp_margin_per_depth * depth >= beta)
        return static_eval;
}
```

Tunable constants:
- `rfp_max_depth` (int, 1-8) — max depth to apply
- `rfp_margin_per_depth` (int, 30-200) — margin per remaining depth

### Late Move Pruning (LMP)

At low depths in non-PV nodes, skip quiet moves after a certain count. Moves ordered late by history are statistically unlikely to beat alpha — don't waste time on them.

```cpp
if (!is_pv_node && !in_check && depth <= lmp_max_depth
    && num_moves > lmp_base + lmp_multiplier * depth)
    continue;
```

Tunable constants:
- `lmp_max_depth` (int, 1-8) — max depth to apply
- `lmp_base` (int, 1-10) — minimum moves before pruning starts
- `lmp_multiplier` (int, 1-10) — additional moves allowed per depth

### SEE Pruning in Main Search

Currently SEE pruning only happens in quiescence. In the main search, bad captures are just ordered last but still searched. At low depths in non-PV nodes, prune moves with bad SEE entirely.

```cpp
if (!is_pv_node && depth <= see_prune_max_depth
    && see(board, move) < -see_prune_margin * depth)
    continue;
```

Tunable constants:
- `see_prune_max_depth` (int, 1-8) — max depth to apply
- `see_prune_margin` (int, 10-200) — threshold per depth

### Razoring

At shallow non-PV nodes, if the static eval is far below alpha, the position is likely hopeless. Drop directly into quiescence search — if even qsearch can't raise the score above alpha, return immediately.

```cpp
if (!is_pv_node && !in_check && depth <= razor_max_depth) {
    PositionScore static_eval = board.nnue_evaluate();
    if (static_eval + razor_margin < alpha) {
        PositionScore qscore = quiescence(board, alpha, beta, ...);
        if (qscore <= alpha)
            return qscore;
    }
}
```

Tunable constants:
- `razor_max_depth` (int, 1-4) — max depth to apply
- `razor_margin` (int, 50-500) — how far below alpha before razoring

### Singular Extensions

When the TT move is significantly better than all alternatives, extend it by one ply. This catches situations where there's "one obvious move" — extending ensures we search it deeply enough to confirm it's truly best.

Search all other moves with a reduced depth and a window of `tt_score - singular_margin`. If every alternative fails low, the TT move is singular and gets extended.

```cpp
if (depth >= singular_min_depth && tt_move_exists && tt_bound == FAIL_HIGH) {
    int reduced_depth = depth / 2 - 1;
    int singular_beta = tt_score - singular_margin * depth;
    // search all other moves at reduced_depth with window [singular_beta-1, singular_beta]
    // if all fail low -> extend TT move by 1
}
```

Tunable constants:
- `singular_min_depth` (int, 4-12) — minimum depth to attempt
- `singular_margin` (int, 1-8) — margin per depth for the reduced window
- `singular_depth_offset` (int, 1-4) — how much to reduce the verification search

### SEE Piece Values

The piece values used for Static Exchange Evaluation are hardcoded at `{100, 300, 325, 500, 900}`. These gate every SEE-based decision in the engine — bad capture ordering, qsearch SEE pruning, and (future) main search SEE pruning. Tuning them alongside `see_cutoff` lets the engine discover the optimal material thresholds for pruning decisions, which may differ from the "true" piece values.

Tunable constants:
- `see_pawn_value` (int, 50-150)
- `see_knight_value` (int, 200-400)
- `see_bishop_value` (int, 200-450)
- `see_rook_value` (int, 350-650)
- `see_queen_value` (int, 700-1200)

### History-Based LMR Adjustment

Moves with high history scores have been good in similar positions — reduce them less. Moves with low history scores are likely bad — reduce them more. This makes LMR adaptive to the position rather than purely mechanical.

```cpp
reduction -= history_score / lmr_history_divisor;
```

Tunable constants:
- `lmr_history_divisor` (int, 1000-16000) — scales the history adjustment

### Improving Flag

Track whether the static eval improved compared to 2 plies ago (our previous turn). If the position is "improving" (our eval went up), be less aggressive with pruning. If "not improving" (eval went down or stayed flat), prune more aggressively.

Used to modulate:
- Reverse futility pruning margin (tighter when improving)
- LMP move count threshold (higher when improving)
- Futility margin (tighter when improving)
- Null move reduction (more when improving)

Tunable constants:
- `rfp_improving_margin` (int, 10-100) — margin reduction when improving
- `lmp_improving_bonus` (int, 1-5) — extra moves allowed when improving
- `futility_improving_margin` (int, 10-100) — margin reduction when improving

---

## Phase 2: Move Ordering Improvements

### Countermove Heuristic

Store the move that caused a beta cutoff as a "countermove" to the opponent's previous move. In move ordering, try the countermove after killers but before quiet history sorting. Indexed by `[previous_piece][previous_to_square]`.

No tunable constants — it's a move ordering slot, not a scored heuristic.

### Continuation History

A history table indexed by the previous move's `[piece][to_square]` combined with the current move's `[piece][to_square]`. Captures follow-up patterns: "after Nf3, Bc4 is usually good." Blended into quiet move scoring alongside the existing history tables.

Tunable constants:
- `continuation_history_weight` (float, 0.1-2.0) — weight when blending into move score

### Capture History

A separate history table for captures, indexed by `[piece][to_square][captured_piece]`. Used alongside MVV/LVA for finer capture ordering — distinguishes "this particular capture has been good in practice" from "this capture wins material."

Tunable constants:
- `capture_history_weight` (float, 0.1-2.0) — weight relative to MVV/LVA score in tactical sorting

### History Table Weighting

Currently the two history tables (side-piece-to and from-to) are summed with equal weight. Allow tuning the ratio.

Tunable constants:
- `from_to_history_weight` (float, 0.1-2.0) — weight of from-to table relative to side-piece-to (implicitly 1.0)

### Quiescence Move Ordering

Currently quiescence search generates tacticals but doesn't sort them via `MoveSelector`. Extract the tactical generation + SEE-based sorting into a shared helper and use it in qsearch for better capture ordering. Also add TT probing in quiescence for a free move ordering hint.

No tunable constants — structural improvement.

---

## Phase 3: Expanding Existing Constants

### LMR Formula

Current: `log(depth+1) * log(move+1) / C`. Could add a base multiplier and an offset for more control over the reduction curve.

```cpp
LMR_TABLE[d][m] = lmr_base * log(d+1) * log(m+1) / lmr_divisor + lmr_offset;
```

Tunable constants:
- `lmr_base` (float, 0.5-3.0) — multiplier on the log product
- `lmr_offset` (float, -1.0-1.0) — flat adjustment to all reductions

### LMR PV Node Reduction

Current: `reduction -= 1` for PV nodes. The `1` is hardcoded.

Tunable constants:
- `lmr_pv_reduction` (int, 0-3) — how much to decrease reduction on PV nodes

### LMR Killer Reduction

Current: killers get `reduction = 0`. Could allow a small reduction for killers rather than none.

Tunable constants:
- `lmr_killer_reduction` (int, 0-3) — reduction to use for killer moves (0 = current behavior)

### LMR In-Check Reduction

Current: when in check, LMR is fully disabled (`reduction = 0`). Could allow partial reduction instead of completely zeroing it.

Tunable constants:
- `lmr_in_check_reduction` (int, 0-3) — reduction to use when in check (0 = current behavior, fully disabled)

### Null Move Depth Scaling

Current: `reduction = base + (depth >= threshold)`. The `+1` bonus is hardcoded. Could scale continuously with depth.

```cpp
int reduction = nmp_base + depth / nmp_depth_divisor;
```

Tunable constants:
- `nmp_depth_divisor` (int, 2-8) — how quickly reduction grows with depth

### Futility Margin Quadratic Term

Current: `margin = per_depth * depth + base`. Linear. A quadratic term lets the margin grow faster at higher depths.

```cpp
PositionScore margin = futility_quadratic * depth * depth
                     + futility_per_depth * depth
                     + futility_base;
```

Tunable constants:
- `futility_quadratic` (int, 0-50) — quadratic scaling factor

### Aspiration Window Widening Factor

Current: deltas are doubled (`*= 2`) on fail. The multiplier is hardcoded.

Tunable constants:
- `aspiration_widening_factor` (float, 1.5-4.0) — multiplier on fail

### Aspiration Minimum Depth

Current: aspiration windows activate at depth 2. At low depths the score is volatile, causing costly re-searches. Many engines start aspiration at depth 4-5.

Tunable constants:
- `aspiration_min_depth` (int, 2-6) — minimum depth before using aspiration windows

### History Bonus/Malus

Current: bonus is `depth * depth`, malus is `-(depth * depth / 2)`. Both formulas are fixed.

```cpp
MoveScore bonus = std::min(history_bonus_scale * depth * depth, history_bonus_max);
MoveScore malus = -(bonus / history_malus_divisor);
```

Tunable constants:
- `history_bonus_scale` (int, 1-4) — multiplier on depth^2
- `history_bonus_max` (int, 500-5000) — cap on bonus magnitude
- `history_malus_divisor` (int, 1-8) — how much weaker the malus is vs bonus

### History Gravity

Current: gravity denominator is `MAX_MOVE_SCORE` (32000). Controls how fast history scores saturate.

Tunable constants:
- `history_gravity_max` (int, 8000-64000) — saturation ceiling

### IID Depth Reduction

Current: `depth / 2`. The divisor is hardcoded.

Tunable constants:
- `iid_depth_divisor` (int, 2-4) — how aggressively to reduce IID depth

---

## Phase 4: Fine-Tuning

### Separate QSearch SEE Cutoff

Current: same `see_cutoff` for qsearch and (future) main search SEE pruning. These should be independent since the context is different.

Tunable constants:
- `qsearch_see_cutoff` (int, -500-0) — separate from main search SEE threshold

### Delta Pruning in QSearch

Skip captures where even capturing the piece can't raise alpha. Avoids searching hopeless captures.

```cpp
if (static_eval + piece_value[captured] + delta_margin < alpha)
    continue;
```

Tunable constants:
- `delta_margin` (int, 50-300) — buffer above piece value

### Check Extension Amount

Current: always extend by 1 ply. Could be gated by depth or made tunable.

Tunable constants:
- `check_extension_max_depth` (int, 4-32) — only extend checks below this depth

### Futility Pruning Move Count

Current: futility prunes all quiet moves after the first. Could allow more moves before pruning kicks in.

Tunable constants:
- `futility_min_moves` (int, 1-5) — moves to search before futility pruning applies

### Best Move Stability Threshold

Current: any non-zero stability allows early soft-time stop. A higher threshold requires more consecutive iterations with the same best move.

Tunable constants:
- `stability_threshold` (int, 0-5) — minimum stability count before allowing early stop

### NMP Verification Search

At high depth, null move pruning can be fooled by zugzwang. A verification re-search at reduced depth catches this.

```cpp
if (depth >= nmp_verify_depth && null_score >= beta) {
    int verify_score = negamax(board, beta - 1, beta, depth - nmp_verify_reduction, ...);
    if (verify_score < beta) // zugzwang detected, don't prune
        ...
}
```

Tunable constants:
- `nmp_verify_depth` (int, 8-16) — minimum depth to verify
- `nmp_verify_reduction` (int, 2-6) — depth reduction for verification search

### TT Cutoff in PV Nodes

Current: TT cutoffs are allowed on all node types including PV. Most strong engines disable TT cutoffs on PV nodes to preserve the principal variation and avoid truncating the search tree at important nodes.

Tunable constants:
- `allow_tt_cutoff_in_pv` (bool) — whether to allow TT cutoffs on PV nodes

### Lazy SMP Depth Stagger

Current: helper threads start at alternating depths (`1 + i % 2`). The stagger pattern affects search diversity in multi-threaded play. More sophisticated staggering can improve thread utilization.

Tunable constants:
- `smp_stagger_base` (int, 1-3) — base starting depth for helper threads
- `smp_stagger_modulus` (int, 2-5) — modulus for depth alternation

---

## Tuning Strategy

~73 total parameters (11 existing search + 11 existing TM + ~51 new). The tuner uses Optuna with multivariate grouped TPE (`TPESampler(multivariate=True, group=True)`) which models parameter interactions jointly rather than independently.

### Batch Tuning

73 params in one run is feasible but slow. Batch tuning groups params by logical function so interactions within each group are captured, and each batch is 15-25 params — TPE's sweet spot.

After all batches, a final full tune of all params together with high patience catches cross-batch interactions.

| Batch | Group | ~Params | Contents |
|-------|-------|---------|----------|
| 1 | **Pruning** | 20 | RFP (max depth, margin, improving margin), LMP (max depth, base, multiplier, improving bonus), futility (base, per depth, quadratic, max depth, min moves, improving margin), razoring (max depth, margin), improving flag. Includes existing `futility_margin_base`, `futility_margin_per_depth`, `futility_max_depth`. |
| 2 | **Reductions** | 16 | LMR formula (base, divisor, offset), LMR PV reduction, LMR killer reduction, LMR in-check reduction, LMR history divisor, NMP (base reduction, deeper threshold, min depth, depth divisor, verify depth, verify reduction). Includes existing `lmr_tuning_constant`, `null_move_base_reduction`, `null_move_deeper_threshold`, `null_move_min_depth`. |
| 3 | **SEE + captures** | 12 | SEE piece values (pawn, knight, bishop, rook, queen), SEE pruning in main search (max depth, margin), qsearch SEE cutoff, delta pruning margin, capture history weight. Includes existing `see_cutoff`. |
| 4 | **Move ordering + history** | 10 | History bonus (scale, max), history malus divisor, history gravity max, from-to history weight, continuation history weight, history-based LMR divisor (shared with batch 2). Includes existing score-related params. |
| 5 | **Extensions + aspiration + misc** | 15 | Singular extensions (min depth, margin, depth offset), check extension max depth, aspiration (window, widening factor, min depth), IID depth divisor, stability threshold, TT cutoff in PV, SMP stagger (base, modulus). Includes existing `aspiration_window`, `score_drop_threshold`, `minimum_iid_depth`. |

Some params appear in multiple batches where they have cross-group interactions:
- `lmr_history_divisor` — lives in batch 2 (reductions) but also relevant to batch 4 (history) since history scale affects it
- `see_cutoff` — lives in batch 3 but interacts with batch 1 pruning decisions
- Improving flag params — in batch 1 but modulate NMP reduction in batch 2

When a param spans groups, include it in both batches. The second batch refines it given the other group's tuned values.

### Adaptive Parameter Freezing

During a tuning run, some params converge early while others are still being explored. Detect converged ("dead") params and freeze them to shrink the active search space.

**Detection method:**

Every 25 trials (starting at trial 50), examine the top 20% of completed trials. For each param, compute the spread of values across those top trials:

- **Int params**: converged if `max - min <= 1` across top 20%
- **Float params**: converged if coefficient of variation `(std / |mean|) < 5%` across top 20%

**Freezing:**

When a param is detected as converged:
- Lock its value to the median of the top 20% trials
- Remove it from `suggest_params` — hardcode the frozen value in future trial dicts
- The active search space shrinks, making TPE more efficient on the remaining params

**Safety:**

- Minimum 50 trials before any freezing (top 20% = 10 trials minimum for a meaningful distribution)
- Recheck every 25 trials — don't freeze too many at once
- Conservative 5% CV threshold — a param needs strong consensus before freezing
- Log frozen params and their locked values for review

### Convergence

No fixed trial limit. The tuner runs until patience is exhausted (no improvement in N trials). As params freeze, the effective search space shrinks and convergence accelerates — the remaining active params are the ones that still matter.