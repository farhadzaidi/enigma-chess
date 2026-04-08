*Part 7 of 17 — [← Prev: Move Generation](movegen.md) | [Next: Search →](search.md)*

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

PSTs are the single biggest improvement you can make after material counting. Without them,
the engine has no concept of where pieces belong — it might develop a knight to a1 instead
of f3 because both moves are "free" if no captures are involved. With even crude PSTs, the
engine suddenly starts playing recognizable chess: knights head for the center, pawns push
forward, and kings tuck behind castled pawns.

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

### King Safety

King safety assigns penalties when the king is exposed to attack and bonuses when it has
a healthy pawn shelter. A typical implementation counts things like: how many pawns are
still in front of the castled king, whether the files near the king are open, and how many
enemy pieces are aimed at the king's zone.

This term is critical for the engine's tactical awareness. Without it, the engine doesn't
understand that stripping the pawns in front of a king is dangerous — it might happily
trade off its own kingside pawns for material elsewhere, not realizing it's walking into a
mating attack. King safety is also one of the hardest terms to get right. Weight it too
aggressively and the engine becomes paranoid, spending moves shuffling pieces defensively
in quiet positions. Weight it too lightly and it ignores real attacks.

### Pawn Structure

Pawn structure evaluates the quality of the pawn formation: penalties for doubled pawns
(two pawns on the same file, blocking each other), isolated pawns (no friendly pawn on
adjacent files to support them), and backward pawns (stuck behind enemy pawns with no
safe way to advance). Conversely, passed pawns (no enemy pawn can block or capture them
on their way to promotion) earn large bonuses, especially as they advance.

This term teaches the engine **long-term thinking**. Pawns are slow, so pawn weaknesses
don't cause immediate problems — but they create lasting targets. Without a pawn structure
term, the engine happily creates doubled and isolated pawns whenever it wins a small
tactical exchange, then finds itself in a strategically lost endgame with no idea why.
Passed pawn bonuses are especially important: a passed pawn on the 6th rank might be worth
nearly as much as a minor piece because of its promotion threat.

### Mobility

Mobility gives a bonus based on how many legal squares a piece can move to. A bishop with
10 available squares is more useful than one with 2. The bonus is usually non-linear — the
first few squares of mobility matter more than going from 10 to 12.

Without a mobility term, the engine might park a bishop behind its own pawns because it
can't tell the bishop is doing nothing. Mobility also helps with development: pieces on
their starting squares typically have fewer moves available, so the engine is implicitly
rewarded for developing them to active squares. This single term covers a surprising
amount of positional chess without needing explicit rules for each situation.

### Bishop Pair

A flat bonus (typically 30-50 centipawns) for having both bishops. Two bishops complement
each other because they cover both light and dark squares, and their combined power
increases as the board opens up through pawn exchanges.

This matters because without it, the engine treats trading a bishop for a knight as
perfectly equal (since their base material values are close). In practice, the bishop pair
is a real advantage in many positions — especially open ones. The bonus helps the engine
avoid carelessly trading one bishop away when it could keep the pair, and it helps the
engine correctly evaluate positions where it has two bishops against bishop and knight.

### Rook on Open File

A bonus for rooks placed on files with no pawns (open files) or only enemy pawns
(semi-open files). Typical values might be +25 for a semi-open file and +40 for a fully
open file.

Rooks need open lines to be effective — a rook buried behind its own pawns on a closed
file contributes almost nothing. This term nudges the engine toward putting rooks where
they can actually exert pressure: controlling open files, infiltrating the 7th rank, and
supporting passed pawns. Without it, the engine has no preference for where its rooks go
and might leave them sitting passively on closed files for the entire game.

### Outposts

A bonus for knights (and sometimes bishops) sitting on squares in enemy territory that
cannot be attacked by enemy pawns. For example, a knight on e5 that can't be kicked away
by a black pawn on d6 or f6 is an extremely powerful piece.

Outposts matter because a piece that can't be challenged by pawns is very hard to
dislodge — the opponent has to use a more valuable piece to trade it off. Without an
outpost term, the engine doesn't distinguish between a knight that can sit on a strong
square indefinitely and one that will immediately be chased away. This term is relatively
small in centipawn value but has an outsized effect on how "human" the engine's positional
play looks.

### Other HCE Terms

Beyond the terms above, a full handcrafted eval often includes additional refinements:

- **Connected rooks** — bonus for rooks defending each other on the same rank or file

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

The phase value is typically computed by assigning weights to each piece type (for example,
knight = 1, bishop = 1, rook = 2, queen = 4, giving a max phase of 24 when all pieces
are on the board) and summing what's left. As pieces get traded, the phase drops toward
zero and the endgame score dominates.

Without tapering, the engine uses one set of values for the entire game, which causes real
problems. The most obvious: king PSTs. In the middlegame, kings want to hide in the corner
behind castled pawns — so the middlegame PST gives big bonuses for g1/h1. In the endgame,
kings need to be active in the center to support passed pawns — so the endgame PST gives
big bonuses for d4/e5. Without tapering, the engine either keeps its king cowering in the
corner during endgames (losing because the king isn't helping) or charges its king into the
center during middlegames (losing because the king gets mated). Tapering solves this
cleanly: as material comes off, the king's ideal position smoothly shifts from the corner
to the center.

The same logic applies to other terms. Pawn advancement bonuses should be bigger in the
endgame (passed pawns become decisive). Knight values decrease slightly in the endgame
(they're slow when the board is open). Bishop pair bonuses increase in the endgame (more
open lines). Tapering lets you encode all of these phase-dependent differences with just
two numbers per term instead of complex if/else logic.

### Limitations of HCE

Handcrafted evaluation works well but has a ceiling. Humans can only encode so much
understanding into explicit rules. Complex piece interactions, prophylaxis, positional
compensation for material — these are hard to express as simple formulas. Every new eval
term interacts with every other one, making tuning increasingly difficult.

This is what motivated the shift to neural network evaluation.

## Building Your Own HCE

Enigma's handcrafted evaluation code has been entirely removed in favor of pure NNUE. But
if you're building your own engine, HCE is the right place to start — it's dramatically
simpler to implement than NNUE and teaches you what evaluation actually needs to capture.
The [Chess Programming Wiki](https://www.chessprogramming.org/Evaluation) has comprehensive
coverage of all the terms described above.

Here's a practical path forward:

**Start with material + PSTs.** These two things alone get you a playable engine. Material
handles the "don't lose pieces" part, and PSTs handle basic positional play — developing
pieces, controlling the center, castling the king. Many engines have reached 2000+ Elo
with just material, PSTs, and a decent search.

**Add terms one at a time and test each one.** Run matches (at least a few hundred games)
between the version with the new term and the version without. If a term doesn't produce a
measurable Elo gain, leave it out — a bad eval term actively hurts. Mobility and passed
pawns tend to give the biggest gains after PSTs. King safety is high-impact but tricky to
get right.

**Use automated tuning rather than hand-tuning weights.** Texel tuning is the standard
approach: you take a large set of positions with known outcomes (win/draw/loss from engine
games), then use gradient descent to find weights that minimize the error between your
eval's predictions and the actual outcomes. Hand-tuning is tempting but doesn't scale —
once you have 20+ terms that all interact, no human can find the right balance. Even rough
automated tuning will outperform careful hand-tuning.

**You can always add NNUE later.** Once your engine has a working search and a reasonable
HCE, switching to NNUE is a well-understood upgrade path. NNUE replaces the eval function
but doesn't change search at all. Many strong engines started with HCE and added NNUE
later. The HCE phase isn't wasted work — it teaches you what your engine's eval needs to
care about, and it gives you a baseline to measure NNUE gains against.

## What Enigma Uses

Enigma uses **NNUE** (Efficiently Updatable Neural Network) — a small neural network
trained on millions of self-play positions that replaces all of the handcrafted terms
above. For the full details of how NNUE works, including the architecture, incremental
updates, quantization, and SIMD vectorization, see [NNUE Evaluation](nnue.md).
