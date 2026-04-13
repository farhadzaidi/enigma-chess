### Test Positions

This directory contains various chess position files used for testing different aspects of the chess engine.

#### Files

- `single_check.epd` - Perft positions with single check
- `double_check.epd` - Perft positions with double check
- `cpw.epd` - Perft positions from Chess Programming Wiki
- `en_passant.epd` - Perft positions with en passant
- `mixed.epd` - Mixed perft positions
- `engine.epd` - Tactical positions with known best moves (format: `FEN; bm MOVE; id NAME`)
- `not_in_check.fen` - Positions where neither side is in check
- `games.san` - Parsed opening games in SAN, used for building the internal opening book
- `openings.pgn` - Opening book for engine testing (`8moves_v3.pgn`), used by the test runner via cutechess

#### Sources

- [Chris Whittington's Chess EPDs Repository](https://github.com/ChrisWhittington/Chess-EPDs/tree/master)
- [Chess Programming Wiki – Perft Results](https://www.chessprogramming.org/Perft_Results)
- [Yottachess](https://www.yottachess.com/)
    - [Hikaru Nakamura](https://www.yottachess.com/player/nakamura,%20hikaru)
    - [Magnus Carlsen](https://www.yottachess.com/player/carlsen,%20magnus)
    - [Fabiano Caruana](https://www.yottachess.com/player/2020009)
    - [Alireza Firouzja](https://www.yottachess.com/player/Firouzja,%20Alireza)
- [Stockfish Opening Books](https://github.com/official-stockfish/books)