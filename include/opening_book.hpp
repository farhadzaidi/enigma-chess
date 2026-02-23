#pragma once

#include <vector>
#include <unordered_map>

#include "move.hpp"
#include "board.hpp"

struct BookMove {
    Move move;
    int frequency;
    BookMove(Move m, int f): move(m), frequency(f) {}
};

struct OpeningBook {
    OpeningBook();
    Move pick_move(const Board& b);

private:
    std::unordered_map<ZobristHash, std::vector<BookMove>> book;

    void add_book_move(ZobristHash position, Move move);
    void initialize_book();
    void remove_low_frequency_moves();
};
