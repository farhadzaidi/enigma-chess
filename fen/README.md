### Test Positions

This directory contains various chess position files in FEN and EPD formats used for testing different aspects of the chess engine:

#### File Types

**Perft EPD Files** (`.epd` with depth/node counts)
- `single_check.epd` - Positions with single check
- `double_check.epd` - Positions with double check
- `cpw.epd` - Positions from Chess Programming Wiki
- `en_passant.epd` - En passant positions
- `mixed.epd` - Mixed tactical positions

These files contain FEN strings paired with expected node counts at various depths (format: `FEN; D1 nodes; D2 nodes; ...`).
Used for verifying the correctness of move generation and perft implementations.

**Engine Test EPD Files**
- `engine.epd` - Tactical positions with known best moves (format: `FEN; bm MOVE; id NAME`)

Used for testing search quality by comparing the engine's best move against known optimal moves.

**Plain FEN Files** (`.fen`)
- `not_in_check.fen` - Positions where neither side is in check

Simple FEN strings (one per line) without additional metadata.

**SAN Game Files** (`.san`)
- `games.san` - Parsed opening games in Standard Algebraic Notation

Used for building the opening book. Each line contains a sequence of SAN moves from a single game.

#### Sources
The positions and results were adapted from:
- [Chris Whittington's Chess EPDs Repository](https://github.com/ChrisWhittington/Chess-EPDs/tree/master)
- [Chess Programming Wiki – Perft Results](https://www.chessprogramming.org/Perft_Results)
- [Yottachess – Hikaru Nakamura](https://www.yottachess.com/player/nakamura,%20hikaru)
- [Yottachess – Magnus Carlsen](https://www.yottachess.com/player/carlsen,%20magnus)
