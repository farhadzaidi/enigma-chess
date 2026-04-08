*Part 2 of 17 — [← Prev: Architecture Overview](overview.md) | [Next: Bitboards →](bitboards.md)*

# Bit Manipulation

## Why You Need This

Chess engines use **bitboards** — 64-bit integers where each bit represents a square on
the board. To work with them effectively, you need to understand how integers are stored
in memory, how binary arithmetic works, and some clever bit manipulation tricks.

If you already know binary, two's complement, and basic bitwise operations, feel free to
skip ahead to [Bitboards](bitboards.md). Otherwise, read on.

## How Integers Are Stored

Computers store integers as sequences of bits (0s and 1s). Each bit position represents a
power of 2, reading right to left:

```
bit position:  7    6    5    4    3    2    1    0
value:        128   64   32   16    8    4    2    1
```

So the byte `00001010` = 8 + 2 = 10. An **unsigned** integer uses all its bits for the
value — an 8-bit unsigned integer ranges from 0 to 255. A **signed** integer reserves
the highest bit for the sign (look up "two's complement" if you want the details).

The type that matters most for chess engines is `uint64_t`: a 64-bit unsigned integer. It
has exactly 64 bits — one per square on the board. That's not a coincidence; it's why
bitboards work.

### LSB and MSB

- **LSB** (Least Significant Bit): bit 0, the rightmost bit, worth 1.
- **MSB** (Most Significant Bit): the leftmost bit (bit 7 for a byte, bit 63 for a
  64-bit integer).

These terms come up constantly when working with bitboards. "Finding the LSB" of a 64-bit
integer means finding the position of the lowest set bit — which translates directly to
finding the first occupied square.

## Bitwise Operations

These are the fundamental operations for working with bits. Each operates on individual
bits independently — bit 0 of the result depends only on bit 0 of the inputs, bit 1
depends only on bit 1, and so on.

### AND (`&`)

Both bits must be 1 for the result to be 1. Think of it as "keep only the bits that are
set in **both** operands."

```
  1 0 1 0
& 1 1 0 0
---------
  1 0 0 0
```

Common uses:
- **Masking**: extract specific bits from a value. `value & 0xFF` keeps only the lowest
  8 bits.
- **Testing a bit**: `value & (1 << n)` is nonzero if bit `n` is set.
- **Intersection**: if two integers represent sets, AND gives the elements in both sets.

### OR (`|`)

Either bit being 1 makes the result 1. Think "set all bits that are in **either**
operand."

```
  1 0 1 0
| 1 1 0 0
---------
  1 1 1 0
```

Common uses:
- **Setting bits**: `value |= (1 << n)` sets bit `n`.
- **Union**: if two integers represent sets, OR gives all elements from both.
- **Combining flags**: `READ | WRITE | EXECUTE`.

### XOR (`^`)

Bits must differ for the result to be 1. Think "flip the bits that are set in the second
operand."

```
  1 0 1 0
^ 1 1 0 0
---------
  0 1 1 0
```

The most important property: **XOR is its own inverse**. `a ^ b ^ b = a`. XORing the
same value twice cancels out. This makes XOR perfect for toggling things on and off —
which is exactly how Zobrist hashing works (see
[Transposition Table](transposition-table.md)).

### NOT (`~`)

Flips every bit. 0 becomes 1, 1 becomes 0.

```
~1010 = 0101   (for a 4-bit number)
```

Common uses:
- **Complement**: `~set` gives everything NOT in the set.
- **Clearing bits**: `value &= ~(1 << n)` clears bit `n`. The NOT creates a mask with
  every bit set except bit `n`, then AND keeps everything except that bit.

### Left Shift (`<<`)

Shifts all bits toward the MSB by N positions. Zeros fill from the right. Equivalent to
multiplying by 2^N.

```
00001010 << 2 = 00101000   (10 × 4 = 40)
```

Common uses:
- **Creating bit masks**: `1 << n` creates a value with only bit `n` set.
- **Multiplication by powers of 2**: `x << 3` is `x × 8`.

### Right Shift (`>>`)

Shifts all bits toward the LSB by N positions. For unsigned types, zeros fill from the
left. Equivalent to dividing by 2^N (rounding down).

```
00101000 >> 2 = 00001010   (40 / 4 = 10)
```

Common uses:
- **Extracting fields**: shift a value right to move the bits you want into the lowest
  positions, then AND with a mask.
- **Division by powers of 2**: `x >> 3` is `x / 8`.

## Useful Bit Tricks

These tricks come up repeatedly in chess engine code. They're worth memorizing.

### Single-Bit Mask

Create a value with exactly one bit set at position `n`:

```cpp
uint64_t mask = 1ULL << n;
```

The `ULL` suffix ensures the literal is 64-bit. Without it, `1 << 32` would overflow a
32-bit integer — a common bug.

### Test, Set, Clear, Toggle a Bit

```cpp
if (value & (1ULL << n))     // test: is bit n set?
value |= (1ULL << n);        // set bit n
value &= ~(1ULL << n);       // clear bit n
value ^= (1ULL << n);        // toggle bit n
```

### Population Count (popcount)

Count the number of set bits in an integer. "How many 1s are there?"

```cpp
int count = std::popcount(value);  // C++20
```

The CPU has a hardware `popcnt` instruction that does this in one cycle.

### Count Trailing Zeros (CTZ)

Find the position of the lowest set bit. Returns the number of zeros below it.

```cpp
int pos = std::countr_zero(value);  // C++20
```

For `value = 01101000` (binary), CTZ returns 3 (bits 0, 1, 2 are zero, bit 3 is the
first set bit). Hardware `tzcnt` instruction: one cycle.

If the value is zero (no bits set), returns the bit width (64 for `uint64_t`).

### Count Leading Zeros (CLZ)

Find the position of the highest set bit. Returns the number of zeros above it.

```cpp
int leading = std::countl_zero(value);  // C++20
int pos = 63 - leading;                 // position of the highest set bit
```

### Clear Lowest Set Bit (Carry-Ripple)

```cpp
value &= value - 1;
```

This is the **carry-ripple trick**. How it works: `value - 1` flips all bits from the LSB
downward (the subtraction borrows through them). ANDing with the original clears exactly
the LSB.

```
value:       01101000
value - 1:   01100111
AND result:  01100000   ← bit 3 (the lowest set bit) is cleared
```

On x86, this compiles to a single `blsr` (Reset Lowest Set Bit) instruction.

### Extract and Clear Lowest Set Bit

The combination of CTZ + carry-ripple is the fundamental **iteration primitive** for
sets stored as bitmasks:

```cpp
int bit = std::countr_zero(value);  // which bit is it?
value &= value - 1;                 // remove it from the set
```

This lets you loop over all set bits:

```cpp
while (value) {
    int bit = std::countr_zero(value);
    value &= value - 1;
    // do something with 'bit'
}
```

Each iteration extracts one set bit and removes it. When `value` reaches zero, all bits
have been visited.

### Subset Enumeration

To enumerate all subsets of a bitmask:

```cpp
uint64_t subset = mask;
do {
    // process subset
    subset = (subset - 1) & mask;
} while (subset);
```

This generates every subset of the mask in decreasing order. The `& mask` constrains the
subtraction so only bits within the original mask can be set. This trick is used during
magic bitboard table construction (see [Bitboards](bitboards.md)).

## Hexadecimal Notation

Hex is a convenient shorthand for binary. Each hex digit represents exactly 4 bits:

```
0 = 0000    4 = 0100    8 = 1000    C = 1100
1 = 0001    5 = 0101    9 = 1001    D = 1101
2 = 0010    6 = 0110    A = 1010    E = 1110
3 = 0011    7 = 0111    B = 1011    F = 1111
```

In code, hex literals are prefixed with `0x`:

```cpp
uint64_t a = 0xFF;                    // 8 bits set: 11111111
uint64_t b = 0x0101010101010101ULL;   // one bit every 8 positions
```

Hex is easier to read than decimal for bitmasks because the bit patterns are visible.
`0xFF` is obviously "8 bits set." `255` is less obvious.

## Square Representation

Before diving into bitboards, it's worth understanding how chess squares are typically
represented as integers. There are two common conventions:

### Rank-File (what Enigma uses)

```
square = rank × 8 + file
```

where rank 0 = first rank (white's back rank) and file 0 = A-file. This gives:

```
A1=0,  B1=1,  C1=2,  ... H1=7
A2=8,  B2=9,  C2=10, ... H2=15
...
A8=56, B8=57, C8=58, ... H8=63
```

The board fills left-to-right, bottom-to-top from white's perspective. Extracting the
rank and file from a square is simple division:

```
rank = square / 8
file = square % 8
```

### File-Rank (the alternative)

Some engines use `file × 8 + rank` instead, filling top-to-bottom by file. The choice is
arbitrary — what matters is consistency. Enigma uses rank-file because it maps naturally
to bitboard bit indices: bit 0 is A1 (rank 0, file 0), bit 63 is H8 (rank 7, file 7).

### Why 0-63?

A square index from 0 to 63 fits in 6 bits (2^6 = 64). This is exactly the minimum
needed, which is why move encoding can pack two squares (from and to) into just 12 bits.
It also means a square can index directly into a 64-bit integer — bit `n` of a bitboard
corresponds to square `n`.

See `src/types.hpp:65-74` for the square enumeration and `src/square.hpp` for the
conversion functions.

## Further Reading

With these foundations, you're ready for [Bitboards](bitboards.md), which explains how
chess engines use 64-bit integers and these bit operations to represent and manipulate
the board efficiently.
