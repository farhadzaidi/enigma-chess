*Part 9 of 16 — [← Prev: Search](search.md) | [Next: Move Ordering →](move-ordering.md)*

# Transposition Table

## What Is a Transposition Table?

A **transposition table** (TT) is a cache of previously searched positions. When the
engine finishes searching a position, it stores the result — the score, the best move,
and how deep the search went. The next time it encounters the same position, it can look
up the cached result instead of searching again.

The name comes from chess terminology: a **transposition** is when the same position
arises from a different move order. `1. e4 e5 2. Nf3 Nc6` and `1. Nf3 Nc6 2. e4 e5`
lead to the exact same position. Without caching, the engine would analyze both from
scratch, doing identical work twice.

This isn't a rare occurrence. In a typical search, **30-50% of nodes are transpositions**
— different paths reaching the same position. That's an enormous amount of redundant work.

The TT serves three purposes:
1. **Score cutoffs** — skip the search for positions we've already evaluated deeply enough
2. **Move hints** — even without a score cutoff, the stored best move is excellent for
   move ordering
3. **Communication** — in Lazy SMP, the TT is the implicit channel between threads

The transposition table implementation lives in `src/transposition_table.hpp` and
`src/transposition_table.cpp`.

## Zobrist Hashing: How Positions Are Identified

To look up a position in the TT, we need a way to quickly identify it. The obvious
approach — comparing all 64 squares, side to move, castling rights, and en passant — is
far too slow for millions of lookups per second. We need a **hash**: a compact numerical
fingerprint that uniquely (or nearly uniquely) identifies a position.

The challenge is that there are more possible chess positions than a 64-bit integer can
represent — roughly 10^44 positions vs. 10^19 possible hash values. So collisions (two
different positions producing the same hash) are theoretically inevitable. In practice,
with 64-bit hashes the collision probability is astronomically low — low enough that
engines treat it as zero. The goal isn't a perfect hash; it's one that's fast to compute,
fast to update incrementally, and well-distributed enough that collisions are negligible.

### The Zobrist Technique

Zobrist hashing is the standard technique in chess engines. The idea:

1. Before the engine starts, generate a random 64-bit number for every possible
   "component" of a position:
   - 768 numbers for (side, piece type, square) combinations
   - 16 numbers for castling rights configurations
   - 8 numbers for en passant files
   - 1 number for "black to move"

2. The hash of a position is the **XOR** of all the random numbers for components that
   are present.

XOR is perfect for this because:

- **Incremental**: XOR is its own inverse (`a ^ a = 0`). Moving a piece from A1 to B3:
  `hash ^= piece_at_A1; hash ^= piece_at_B3`. Each `make_move` does 3-6 XOR operations
  regardless of how many pieces are on the board.

- **Order-independent**: XOR is commutative. The hash doesn't depend on the order of
  operations, so you can update components in any order during `make_move`.

- **Well-distributed**: random 64-bit numbers XOR'd together produce well-distributed
  hashes.

### Example: Hashing a Move

White plays e2-e4 from the starting position:

```
hash ^= ZOBRIST_PIECES[WHITE][PAWN][E2]        // remove pawn from E2
hash ^= ZOBRIST_PIECES[WHITE][PAWN][E4]        // place pawn on E4
hash ^= ZOBRIST_EN_PASSANT_TARGETS[E_FILE]     // e.p. now available
hash ^= ZOBRIST_SIDE_TO_MOVE                   // now black's turn
```

Four XOR operations. Done.

### Hash Components

The Zobrist tables are defined in `src/zobrist.hpp` and `src/zobrist.cpp`:

- `ZOBRIST_PIECES[2][6][64]` — 768 random values, one per (side, piece type, square)
- `ZOBRIST_CASTLING_RIGHTS[16]` — 16 values, indexed by the 4-bit castling rights mask
- `ZOBRIST_EN_PASSANT_TARGETS[8]` — 8 values, indexed by file. Only included when a pawn
  can actually make the en passant capture (prevents "phantom" en passant targets from
  changing the hash)
- `ZOBRIST_SIDE_TO_MOVE` — one value, XOR'd when it's black's turn

All generated from a fixed PRNG seed so the hash is deterministic across runs.

The hash type (`src/types.hpp:15`):

```cpp
using ZobristHash = uint64_t;
```

### Collision Risk

With a 64-bit hash and a TT of ~4M entries (64 MB), the probability of any single lookup
colliding is roughly `4M / 2^64 ≈ 2 × 10^-13`. Over a billion-node search, the expected
number of collisions is ~0.0002. The TT's hash verification (below) catches most of those.
In practice, Zobrist collisions are not something engines worry about.

## How Entries Work

### What's Stored

Each TT entry is 16 bytes, defined in `src/transposition_table.hpp:16-43`. The fields:

- **Best move** (16 bits): the move that scored highest when this position was searched.
  Even if the entry's depth is too shallow for a score cutoff, the move is almost always
  worth trying first.

- **Score** (16 bits): the evaluation result, in centipawns.

- **Depth** (8 bits): how deep the search was that produced this score. A depth-12 result
  is more reliable than depth-6.

- **Node type** (8 bits): what kind of score this is. Defined in `src/types.hpp:33-38`:

  ```
  TT_EXACT     the true minimax score (best move found within window)
  TT_FAIL_HIGH a lower bound (a move beat beta, exact score unknown)
  TT_FAIL_LOW  an upper bound (nothing beat alpha)
  TT_NONE      empty/invalid entry
  ```

- **Age** (16 bits): which generation (search call) produced this entry. Used for
  replacement decisions.

### Hash Verification: The XOR Trick

The entry stores `zobrist_hash XOR packed_data` instead of the raw hash. On lookup, XOR
with the stored data to recover the original hash. If it matches the lookup key, the entry
is valid. If not — corrupted by a race condition or a hash collision — it's rejected.

This XOR trick is a compact integrity check: the hash field does double duty as both key
and checksum. It uses no extra space and catches the vast majority of corruptions.

## Buckets

A hash table with millions of entries will have collisions — different positions mapping
to the same slot. With a single entry per slot, every collision evicts the previous
occupant, even if that entry was deeply searched and still valuable. How do you reduce
this pressure without making lookups expensive?

Entries are grouped into **buckets of 4** (`src/transposition_table.hpp:62`):

```
TTEntry bucket[4];   // 4 entries per bucket
```

The bucket index is `hash & (num_buckets - 1)` — the count is always a power of 2, so
this is a single AND instruction (`src/transposition_table.hpp:72`).

Each bucket is 4 × 16 = 64 bytes, which matches a **CPU cache line** on most
architectures. A TT probe loads exactly one cache line — optimal for memory access.

### Why 4-Way?

Direct-mapped tables (1 entry per index) have severe collision problems: any two positions
that hash to the same bucket overwrite each other. With 4 entries per bucket, up to 4
different positions can coexist at the same index, dramatically reducing eviction pressure.

8-way or 16-way would reduce pressure further but cost more per probe (more entries to
scan, larger cache footprint). 4-way is the sweet spot most engines have converged on.

## Replacement Policy

When all 4 slots are occupied, the new entry evicts the least valuable existing entry
(`src/transposition_table.hpp:73-74`):

```
value = depth - 4 × (current_generation - entry_age)
```

Two factors compete:

**Depth matters.** A depth-12 result took orders of magnitude more computation than
depth-4. Evicting it means re-searching an expensive subtree later.

**Freshness matters.** An entry from a previous search (different root position) is less
likely to be relevant. The age penalty weight (4, defined in
`src/transposition_table.hpp:63`) is steep — each generation of staleness costs several
depth units.

**Same-hash replacement.** If the bucket already has an entry with a matching hash, it's
always overwritten. The new result was computed with more current context.

## Table Size

Default: **64 MB**. Configurable from 1 MB to 16 GB via UCI `Hash`
(`src/transposition_table.hpp:11-13`):

```
DEFAULT_HASH_MB = 64
MIN_HASH_MB = 1
MAX_HASH_MB = 16384
```

At 64 MB with 64-byte buckets: ~1 million buckets, ~4 million entries. For a typical
search (depth 12-20, a few million nodes), this is more than enough.

The bucket count is rounded down to the nearest power of 2 for fast indexing. A 100 MB
setting would round down to 64 MB worth of buckets — powers of 2 (64, 128, 256) are the
natural choices.

## Mate Score Adjustment

Mate scores encode distance-to-mate, which is **ply-dependent**. "Checkmate in 5 plies"
has a specific score, but whether we're 5 plies away depends on where we are in the tree.

Before storing, mate scores are adjusted to be relative to the root:
- Near-positive-mate (winning): add current ply
- Near-negative-mate (losing): subtract current ply

On retrieval, reverse the adjustment. This ensures a mate score stored deep in the tree
is correctly interpreted when retrieved at a different ply.

If you forget this adjustment (a common bug in engine development), mate scores appear to
indicate mates that are impossibly close or far, causing the engine to misjudge mating
sequences.

## How the Search Uses the TT

### Probing

The TT is probed at the start of each `negamax()` call, after draw detection but before
expensive work. The probe (`probe_tt()` in `src/engine.hpp:199-206`) can produce:

1. **Score cutoff.** The entry has sufficient depth and the right bound type. At non-PV
   nodes: return the stored score. At PV nodes: only EXACT entries produce cutoffs (we
   need accurate scores for the principal variation).

2. **Move hint.** Even without a cutoff, the best move is fed to the move selector as the
   first move to try. This is one of the strongest move ordering signals (see
   [Move Ordering](move-ordering.md)).

3. **Nothing.** Table miss. If this is a PV node at sufficient depth, IID runs a shallow
   search to populate the TT.

### Storing

After completing a node's search (`store_tt_result()` in `src/engine.hpp:175-183`):

- Best score >= beta → **FAIL_HIGH** (the true score is at least this high)
- Best score <= original alpha → **FAIL_LOW** (nothing beat alpha)
- Otherwise → **EXACT** (the true minimax value)

The best move is always stored, regardless of node type.

### Thread Safety

In Lazy SMP, multiple threads read and write the TT concurrently with **no locks**. This
means entries can be "torn" — a thread reads an entry half-written by another thread. The
XOR hash verification catches most of these: torn entries produce garbled hashes.

Very rarely, a torn entry might pass verification by accident. The impact is negligible
(a single wrong score at one node in a million-node search). This lockless design is a
deliberate tradeoff: correct behavior in 99.999% of cases, in exchange for zero
synchronization overhead in the hottest path of the engine.

The global TT instance (`src/transposition_table.hpp:77`):

```
TranspositionTable g_tt;   // one global instance, shared by all threads
```
