#pragma once

#include <vector>
#include <unordered_map>
#include <sstream>
#include <random>
#include <cmath>
#include <filesystem>

#include "core/move.hpp"
#include "board/board.hpp"
#include "core/random.hpp"
#include "utils/notation.hpp"
#include "utils/file_io.hpp"

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "./"
#endif

inline const std::filesystem::path GAMES_SAN_PATH = DATA_DIR / "games.san";

constexpr size_t OPENING_CUTOFF_PLY = 30;
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

        // This is to prevent picking low frequency moves which may correlate to being bad moves
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
        // Loop through the already seen moves in this position and if this
        // move has already been seen, then increment its frequency.
        for (BookMove& book_move: book[position]) {
            if (book_move.move == move) {
                book_move.frequency++;
                return;
            }
        }

        // Otherwise, add it to the book
        book[position].push_back(BookMove(move, 1));
    }

    inline void initialize_book() {
        Board b;
        std::vector<std::string> games;
        read_file(games, GAMES_SAN_PATH);

        for (const std::string& game: games) {
            // Reset board to start position
            b.load_from_fen();

            int ply = 0;
            std::istringstream iss(game);
            std::string san;
            while(std::getline(iss, san, ' ') && ply < static_cast<int>(OPENING_CUTOFF_PLY)) {
                Move move = parse_move_from_san(b, san);
                add_book_move(b.position_hash, move);
                b.make_move(move);
                ply += 1;
            }
        }
    }

    inline void remove_low_frequency_moves() {
        for (auto& [_, book_moves]: book) {

            // Get sum of all move frequencies at this position
            int frequency_sum = 0;
            for (BookMove& book_move: book_moves) {
                frequency_sum += book_move.frequency;
            }

            // Remove all moves whose frequencies are below the threshold
            std::erase_if(book_moves, [&](const BookMove& book_move) {
                double relative_frequency = static_cast<double>(book_move.frequency) / frequency_sum;
                return relative_frequency < OPENING_MIN_MOVE_FREQUENCY;
            });
        }
    }
};
