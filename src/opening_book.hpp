#pragma once

#include <unordered_map>
#include <vector>

#include "move.hpp"
#include "board.hpp"

/** Frequency-weighted opening book built from a compiled PGN database */
class OpeningBook {
public:
    OpeningBook();

    /** Return a weighted-random book move for the position, or NULL_MOVE if out of book */
    Move pick_move(const Board& b);

private:
    struct BookMove {
        Move move;
        int frequency;

        BookMove(Move move, int frequency);
    };

    std::unordered_map<ZobristHash, std::vector<BookMove>> book_;
    bool initialized_ = false;

    /** Increment the frequency count for a move in a position, or add it */
    void add_book_move(ZobristHash position, Move move);
    /** Parse all stored games and populate the book map */
    void initialize_book();
    /** Prune moves whose relative frequency falls below the threshold */
    void remove_low_frequency_moves();
};
