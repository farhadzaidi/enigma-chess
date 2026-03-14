#include "opening_book.hpp"

#include <cmath>
#include <random>
#include <sstream>

#include "data/book.hpp"
#include "notation.hpp"

namespace {

constexpr double OPENING_MIN_MOVE_FREQUENCY = 0.05; // drop moves played less than 5% of the time
constexpr double OPENING_MOVE_TEMPERATURE = 1.0;    // softmax temperature for move selection

std::mt19937_64& opening_book_rng() {
    static std::mt19937_64 rng(std::random_device{}());
    return rng;
}

} // namespace

OpeningBook::BookMove::BookMove(Move move, int frequency)
    : move(move), frequency(frequency) {}

OpeningBook::OpeningBook() = default;

Move OpeningBook::pick_move(const Board& b) {
    // Lazy init: parse games on first lookup
    if (!initialized) {
        initialize_book();
        remove_low_frequency_moves();
        initialized = true;
    }

    if (!book.contains(b.position_hash())) {
        return NULL_MOVE;
    }

    std::vector<BookMove>& book_moves = book[b.position_hash()];
    std::vector<double> weights;
    for (const BookMove& book_move : book_moves) {
        double weight = std::pow(book_move.frequency, 1 / OPENING_MOVE_TEMPERATURE);
        weights.push_back(weight);
    }

    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    int index = dist(opening_book_rng());
    return book_moves[index].move;
}

void OpeningBook::add_book_move(ZobristHash position, Move move) {
    for (BookMove& book_move : book[position]) {
        if (book_move.move == move) {
            book_move.frequency++;
            return;
        }
    }

    book[position].push_back(BookMove(move, 1));
}

void OpeningBook::initialize_book() {
    Board b;

    for (size_t i = 0; i < BOOK_SIZE; i++) {
        b.load_from_fen();

        std::string game(BOOK_DATA[i]);
        std::istringstream iss(game);
        std::string san;
        while (std::getline(iss, san, ' ')) {
            Move move = parse_move_from_san(b, san);
            add_book_move(b.position_hash(), move);
            b.make_move(move);
        }
    }
}

void OpeningBook::remove_low_frequency_moves() {
    for (auto& [_, book_moves] : book) {
        int frequency_sum = 0;
        for (BookMove& book_move : book_moves) {
            frequency_sum += book_move.frequency;
        }

        std::erase_if(book_moves, [&](const BookMove& book_move) {
            double relative_frequency = static_cast<double>(book_move.frequency) / frequency_sum;
            return relative_frequency < OPENING_MIN_MOVE_FREQUENCY;
        });
    }
}
