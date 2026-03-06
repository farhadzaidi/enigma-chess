#pragma once

#include <vector>
#include <unordered_map>
#include <sstream>
#include <cmath>

#include "core/move.hpp"
#include "board/board.hpp"
#include "core/random.hpp"
#include "utils/notation.hpp"
#include "data/book.hpp"

constexpr double OPENING_MIN_MOVE_FREQUENCY = 0.05;
constexpr double OPENING_MOVE_TEMPERATURE = 1.0;

struct BookMove {
    Move move;
    int frequency;
    BookMove(Move m, int f): move(m), frequency(f) {}
};

struct OpeningBook {
    inline OpeningBook() {
        initialize_book();
        remove_low_frequency_moves();
    }

    inline Move pick_move(const Board& b) {
        if (!book.contains(b.position_hash)) {
            return NULL_MOVE;
        }

        std::vector<BookMove>& book_moves = book[b.position_hash];
        std::vector<double> weights;
        for (const BookMove& book_move: book_moves) {
            double weight = std::pow(book_move.frequency, 1 / OPENING_MOVE_TEMPERATURE);
            weights.push_back(weight);
        }

        std::discrete_distribution<int> dist(weights.begin(), weights.end());
        int index = dist(rng);
        return book_moves[index].move;
    }

private:
    std::unordered_map<ZobristHash, std::vector<BookMove>> book;

    inline void add_book_move(ZobristHash position, Move move) {
        for (BookMove& book_move: book[position]) {
            if (book_move.move == move) {
                book_move.frequency++;
                return;
            }
        }

        book[position].push_back(BookMove(move, 1));
    }

    inline void initialize_book() {
        Board b;

        for (size_t i = 0; i < BOOK_SIZE; i++) {
            b.load_from_fen();

            std::string game(BOOK_DATA[i]);
            std::istringstream iss(game);
            std::string san;
            while (std::getline(iss, san, ' ')) {
                Move move = parse_move_from_san(b, san);
                add_book_move(b.position_hash, move);
                b.make_move(move);
            }
        }
    }

    inline void remove_low_frequency_moves() {
        for (auto& [_, book_moves]: book) {
            int frequency_sum = 0;
            for (BookMove& book_move: book_moves) {
                frequency_sum += book_move.frequency;
            }

            std::erase_if(book_moves, [&](const BookMove& book_move) {
                double relative_frequency = static_cast<double>(book_move.frequency) / frequency_sum;
                return relative_frequency < OPENING_MIN_MOVE_FREQUENCY;
            });
        }
    }
};
