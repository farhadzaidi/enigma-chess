#include <iostream>
#include <string_view>

#include "types.hpp"
#include "board.hpp"
#include "tests/helpers.hpp"

namespace {

bool test_single_check(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view description;
    };

    TestCase test_cases[] = {
        // Rook checks
        {"R3k3/8/8/8/8/8/8/4K3 b - - 0 1",   "rook check on back rank"},
        {"4k3/8/8/8/8/4R3/8/K7 b - - 0 1",    "rook check on file"},
        {"7k/4r3/8/8/8/8/8/4K3 w - - 0 1",    "black rook checks white king"},

        // Bishop checks
        {"4k3/8/8/7B/8/8/8/K7 b - - 0 1",     "bishop check on diagonal"},
        {"4k3/8/8/1B6/8/8/8/K7 b - - 0 1",    "bishop check on anti-diagonal"},
        {"7k/8/8/8/7b/8/8/4K3 w - - 0 1",     "black bishop checks white king"},

        // Queen checks
        {"4k3/8/8/8/4Q3/8/8/K7 b - - 0 1",    "queen check on file"},
        {"4k3/8/8/7Q/8/8/8/K7 b - - 0 1",     "queen check on diagonal"},
        {"7k/8/8/8/8/2q5/8/4K3 w - - 0 1",    "black queen checks white king"},

        // Knight checks
        {"4k3/8/5N2/8/8/8/8/K7 b - - 0 1",    "knight check from f6"},
        {"4k3/8/3N4/8/8/8/8/K7 b - - 0 1",    "knight check from d6"},
        {"7k/8/8/8/8/5n2/8/4K3 w - - 0 1",    "black knight checks white king"},

        // Pawn checks
        {"4k3/3P4/8/8/8/8/8/K7 b - - 0 1",    "white pawn checks black king"},
        {"k7/8/8/8/8/8/5p2/4K3 w - - 0 1",    "black pawn checks white king"},

        // Complex positions
        {"rnb2rk1/pp2bppp/4pn2/2P1N3/2p5/2N3P1/PP2PPBP/R1BqK2R w KQ - 0 1", "middlegame single check"},
        {"r7/7p/1pb1k1p1/4pP2/2p5/P3KPP1/NP1R3P/8 b - - 0 1",               "endgame single check"},
        {"6k1/pp6/2pb2p1/3p2Pp/6bQ/2N1q2K/PP6/4R3 w - - 0 1",                "complex middlegame check"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        ASSERT(b.in_check(), "check",
            "Expected side to move to be in check\n"
            << "Case: " << tc.description << "\n"
            << "FEN: " << tc.fen);
    }

    return true;
}

bool test_double_check(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {"8/5kpp/8/8/1p3P2/6PP/r3KP2/1R1q4 w - - 0 1",                "rook + queen double check"},
        {"3qN3/1pp2pkp/1p4p1/3PQ3/r6P/8/PP4P1/R4RK1 b - - 0 1",       "knight + queen double check"},
        {"5rk1/1p4r1/pP1p3R/P2P4/R6n/6p1/5K2/3B4 w - - 0 1",          "rook + bishop double check"},
        {"r2q4/2k1rQ1p/1P2p1pN/p6n/2R2P2/PP2PB2/6PP/2R3K1 b - - 0 1", "queen + knight double check"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        ASSERT(b.in_check(), "check",
            "Expected side to move to be in double check\n"
            << "Case: " << tc.description << "\n"
            << "FEN: " << tc.fen);
    }

    return true;
}

bool test_not_in_check(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {START_POS_FEN,                                                          "starting position"},
        {KIWIPETE_FEN,                                                           "kiwipete"},
        {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",                              "open castling position"},
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",                                  "en passant available"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1",                                     "promotion available"},
        {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",        "after 1.e4"},

        // Pieces nearby but not giving check
        {"4k3/8/8/3N4/8/8/8/K7 b - - 0 1",                                     "knight near king but not checking"},
        {"4k3/8/8/8/2B5/8/8/K7 b - - 0 1",                                     "bishop not on attacking diagonal"},
        {"4k3/8/8/4p3/8/8/8/4R2K b - - 0 1",                                   "rook blocked by pawn on file"},
        {"4k3/8/8/8/8/8/8/K2R4 b - - 0 1",                                     "rook on adjacent file"},

        // Complex positions
        {"1B1bk3/1p1p2p1/2p2nbN/1PB4p/B1n1P3/1q3N2/PQ1P2PP/5RK1 b - - 2 9",   "complex middlegame not in check"},
        {"1B1k3r/1ppp1ppp/5nb1/bPP5/B6N/1n3N2/PpqP1KPP/R2QR3 b - - 6 8",      "busy position not in check"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        ASSERT(!b.in_check(), "check",
            "Expected side to move to NOT be in check\n"
            << "Case: " << tc.description << "\n"
            << "FEN: " << tc.fen);
    }

    return true;
}

// in_check(Side) can query check status for a specific side.
bool test_check_by_side(Board& b) {
    // Black to move and black king is checked by white queen.
    b.load_from_fen("4k3/8/8/4Q3/8/8/8/4K3 b - - 0 1");
    ASSERT(b.in_check(),       "check", "Expected side to move (black) to be in check");
    ASSERT(b.in_check(BLACK),  "check", "Expected black king to be in check");
    ASSERT(!b.in_check(WHITE), "check", "Expected white king to not be in check");

    // Black to move but white king is checked by black queen.
    b.load_from_fen("4k3/8/8/8/4q3/8/8/4K3 b - - 0 1");
    ASSERT(!b.in_check(),      "check", "Expected side to move (black) to not be in check");
    ASSERT(!b.in_check(BLACK), "check", "Expected black king to not be in check");
    ASSERT(b.in_check(WHITE),  "check", "Expected white king to be in check");

    return true;
}

} // namespace

bool test_check(Board& b) {
    if (!test_single_check(b)) return false;
    if (!test_double_check(b)) return false;
    if (!test_not_in_check(b)) return false;
    if (!test_check_by_side(b)) return false;
    return true;
}
