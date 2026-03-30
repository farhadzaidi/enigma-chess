# Evaluation

## What Is Evaluation?

The search explores the game tree, but at some point it needs to stop and ask: **"how good
is this position?"** The answer to that question is the job of the **evaluation function**
(or "eval" for short).

The eval takes a position and returns a number — a **score** — representing how good the
position is for the side to move. Positive means good for the side to move, negative means
bad, zero means roughly equal. Scores are in **centipawns** (hundredths of a pawn), so
+100 means "we're about a pawn ahead."

The eval is called at leaf nodes of the search tree (when we've searched as deep as we're
going to) and at various pruning boundaries. It needs to be **fast** — the search calls it
millions of times per second — and **accurate** enough that the search can distinguish
good positions from bad ones.

## Simple Evaluation: Material Counting

The simplest possible eval just counts material:

```
score = (our_material - their_material) × 100
```

Using the traditional piece values:
- Pawn = 100 centipawns
- Knight = 300
- Bishop = 320
- Rook = 500
- Queen = 900

This alone gets you surprisingly far. Material is the single most important factor in
chess — being up a queen is winning regardless of position. But material counting misses
everything about **position**: a centralized knight is better than one stuck in the
corner; a passed pawn on the 7th rank is more valuable than one on the 2nd; a king
exposed to attack is dangerous.

## Handcrafted Evaluation (HCE)

Traditional engines improve on material counting by adding dozens (or hundreds) of
**hand-tuned evaluation terms**:

### Piece-Square Tables (PSTs)

A 64-entry table per piece type that assigns a bonus or penalty for each square. Knights
get bonuses for central squares and penalties for edges. Kings get bonuses for castled
positions in the middlegame and central squares in the endgame. Pawns get bonuses for
advancement.

A PST for knights might look conceptually like:

```
   A    B    C    D    E    F    G    H
8 -50  -40  -30  -30  -30  -30  -40  -50
7 -40  -20    0    5    5    0  -20  -40
6 -30    0   10   15   15   10    0  -30
5 -30    5   15   20   20   15    5  -30
4 -30    0   15   20   20   15    0  -30
3 -30    5   10   15   15   10    5  -30
2 -40  -20    0    0    0    0  -20  -40
1 -50  -40  -30  -30  -30  -30  -40  -50
```

Central squares get the biggest bonuses — knights are most effective when they control the
center.

### Other HCE Terms

Beyond PSTs, a full handcrafted eval typically includes:

- **King safety** — penalty for exposed kings, bonus for pawn shelter
- **Pawn structure** — penalties for doubled, isolated, or backward pawns; bonuses for
  passed pawns
- **Mobility** — bonus for pieces that can reach many squares
- **Bishop pair** — bonus for having both bishops (they complement each other)
- **Rook on open file** — bonus for rooks on files with no pawns
- **Connected rooks** — bonus for rooks defending each other
- **Outposts** — bonus for knights/bishops on squares that can't be attacked by pawns

Each term has a weight (how important it is) that must be tuned. Early engines tuned these
by hand; modern ones use automated tuning (gradient descent or evolutionary algorithms).

### Tapered Evaluation

Many terms have different importance in different phases of the game. King safety matters
a lot in the middlegame but barely at all in a king-and-pawn endgame. Piece-square values
for kings are completely different between phases.

**Tapered evaluation** smoothly interpolates between a middlegame score and an endgame
score based on the amount of material on the board (the "game phase"):

```
score = (middlegame_score × phase + endgame_score × (max_phase - phase)) / max_phase
```

### Limitations of HCE

Handcrafted evaluation works well but has a ceiling. Humans can only encode so much
understanding into explicit rules. Complex piece interactions, prophylaxis, positional
compensation for material — these are hard to express as simple formulas. Every new eval
term interacts with every other one, making tuning increasingly difficult.

This is what motivated the shift to neural network evaluation.

> **Note:** Enigma's handcrafted evaluation code has been entirely removed in favor of
> pure NNUE. If you're building your own engine and want to start with HCE (which is much
> simpler to implement than NNUE), the
> [Chess Programming Wiki](https://www.chessprogramming.org/Evaluation) has comprehensive
> coverage of evaluation techniques, piece-square tables, and all the terms described
> above. HCE is a great starting point — you can always add NNUE later.

## What Enigma Uses

Enigma uses **NNUE** (Efficiently Updatable Neural Network) — a small neural network
trained on millions of self-play positions that replaces all of the handcrafted terms
above. For the full details of how NNUE works, including the architecture, incremental
updates, quantization, and SIMD vectorization, see [NNUE Evaluation](nnue.md).
