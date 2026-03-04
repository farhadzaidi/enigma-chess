#include <iostream>
#include <set>

#include "core/types.hpp"
#include "board/board.hpp"
#include "search/opening_book.hpp"
#include "utils/notation.hpp"

// Known opening positions should return a move
static bool test_book_hits(Board& b, OpeningBook& book) {
    struct TestCase {
        std::string description;
        std::vector<std::string> moves; // moves from startpos to reach position
    };

    TestCase test_cases[] = {
        {"after 1.e4", {"e2e4"}},
        {"after 1.d4", {"d2d4"}},
        {"after 1.Nf3", {"g1f3"}},
        {"after 1.c4", {"c2c4"}},
        {"after 1.e4 c5 (Sicilian)", {"e2e4", "c7c5"}},
        {"after 1.d4 Nf6 2.c4 (Indian)", {"d2d4", "g8f6", "c2c4"}},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen();
        for (const auto& uci : tc.moves) {
            b.make_move(encode_move_from_uci(b, uci));
        }

        Move book_move = book.pick_move(b);
        if (book_move == NULL_MOVE) {
            std::clog << "[FAILURE] 'book_hits' - Expected book move " << tc.description << " but got NULL_MOVE\n";
            return false;
        }
    }

    return true;
}

// Random middlegame position should not be in the book
static bool test_book_miss(Board& b, OpeningBook& book) {
    b.load_from_fen("r1bq1rk1/pp3ppp/2n1pn2/2pp4/1bPP4/2NBPN2/PP3PPP/R1BQK2R w KQ - 4 7");

    Move book_move = book.pick_move(b);
    if (book_move != NULL_MOVE) {
        std::clog << "[FAILURE] 'book_miss' - Expected NULL_MOVE for non-book position but got a move\n";
        return false;
    }

    return true;
}

// Popular positions should yield multiple distinct moves across repeated picks
static bool test_book_move_variety(Board& b, OpeningBook& book) {
    // After 1.e4, black has many common responses (e5, c5, e6, c6, d5, etc.)
    b.load_from_fen();
    b.make_move(encode_move_from_uci(b, "e2e4"));

    std::set<uint16_t> seen_moves;
    for (int i = 0; i < 100; i++) {
        Move m = book.pick_move(b);
        if (m != NULL_MOVE) {
            seen_moves.insert(m.move);
        }
    }

    if (seen_moves.size() < 2) {
        std::clog << "[FAILURE] 'book_move_variety' - Expected at least 2 distinct moves after 1.e4, got " << seen_moves.size() << "\n";
        return false;
    }

    return true;
}

bool test_opening_book(Board& b) {
    OpeningBook book;
    if (!test_book_hits(b, book)) return false;
    if (!test_book_miss(b, book)) return false;
    if (!test_book_move_variety(b, book)) return false;
    return true;
}
