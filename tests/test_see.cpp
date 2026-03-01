#include <iostream>
#include <string>

#include "types.hpp"
#include "board.hpp"
#include "move.hpp"
#include "utils.hpp"
#include "see.hpp"
#include "helpers.hpp"

static bool assert_see_score(
    Board& b,
    const std::string& fen,
    const std::string& move_uci,
    int expected_score,
    const std::string& description
) {
    b.reset();
    b.load_from_fen(fen);
    Move move = encode_move_from_uci(b, move_uci);

    if (!b.is_legal_move(move)) {
        std::clog << "[FAILURE] 'see' - Setup move must be legal\n";
        std::clog << "Case: " << description << "\n";
        std::clog << "FEN: " << fen << "\n";
        std::clog << "Move: " << move_uci << "\n";
        return false;
    }

    Board before = b;
    int actual_score = see(b, move);

    if (!board_position_equal(before, b, true)) {
        std::clog << "[FAILURE] 'see' - Board mutated during SEE evaluation\n";
        std::clog << "Case: " << description << "\n";
        std::clog << "FEN: " << fen << "\n";
        std::clog << "Move: " << move_uci << "\n";
        return false;
    }

    if (actual_score != expected_score) {
        std::clog << "[FAILURE] 'see' - Incorrect SEE score\n";
        std::clog << "Case: " << description << "\n";
        std::clog << "FEN: " << fen << "\n";
        std::clog << "Move: " << move_uci << "\n";
        std::clog << "Expected: " << expected_score << " Got: " << actual_score << "\n";
        return false;
    }

    return true;
}

static bool test_see_basic_cases(Board& b) {
    struct TestCase {
        std::string fen;
        std::string move_uci;
        int expected_score;
        std::string description;
    };

    TestCase tests[] = {
        {
            "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1",
            "e4d5",
            100,
            "pawn capture wins undefended pawn"
        },
        {
            "4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1",
            "e4d5",
            0,
            "pawn capture traded back by pawn recapture"
        },
        {
            "4k3/8/2p5/3p4/3Q4/8/8/4K3 w - - 0 1",
            "d4d5",
            -800,
            "queen captures defended pawn and loses exchange"
        },
        {
            "6k1/2p5/8/3pP3/8/8/8/3R2K1 w - d6 0 1",
            "e5d6",
            100,
            "en passant updates occupancy so rook x-ray recapture is seen"
        },
    };

    for (const auto& tc : tests) {
        if (!assert_see_score(b, tc.fen, tc.move_uci, tc.expected_score, tc.description)) {
            return false;
        }
    }

    return true;
}

bool test_see(Board& b) {
    if (!test_see_basic_cases(b)) return false;
    return true;
}
