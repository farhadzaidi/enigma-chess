#include <sstream>
#include <vector>
#include <random>

#include "opening_book.hpp"
#include "types.hpp"
#include "utils.hpp"
#include "board.hpp"
#include "random.hpp"

constexpr size_t OPENING_CUTOFF_PLY = 30;
constexpr double OPENING_MIN_MOVE_FREQUENCY = 0.05;
constexpr double OPENING_MOVE_TEMPERATURE = 1.0;

OpeningBook::OpeningBook() {
    initialize_book();

    // This is to prevent picking low frequency moves which may correlate
    // to being bad moves
    remove_low_frequency_moves();

    // This is to reduce the memory footprint of the opening book
    // Besides we don't want to rely too much on the opening book
}

Move OpeningBook::pick_move(const Board& b) {
    if (!book.contains(b.zobrist_hash)) {
        return NULL_MOVE;
    }

    std::vector<BookMove>& book_moves = book[b.zobrist_hash];
    std::vector<double> weights;
    for (const BookMove& book_move: book_moves) {
        double weight = std::pow(book_move.frequency, 1 / OPENING_MOVE_TEMPERATURE);
        weights.push_back(weight);
    }

    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    int index = dist(rng);
    return book_moves[index].move;
}

void OpeningBook::initialize_book() {
    Board b;
    std::vector<std::string> games;
    read_file(games, GAMES_SAN);

    for (const std::string& game: games) {
        // Reset board to start position
        b.load_from_fen();

        int ply = 0;
        std::istringstream iss(game);
        std::string san;
        while(std::getline(iss, san, ' ') && ply < OPENING_CUTOFF_PLY) {
            Move move = parse_move_from_san(b, san);
            add_book_move(b.zobrist_hash, move);
            b.make_move(move);
            ply += 1;
        }
    }
}

void OpeningBook::add_book_move(ZobristHash position, Move move) {
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

void OpeningBook::remove_low_frequency_moves() {
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
