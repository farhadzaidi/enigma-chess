#include <iostream>

#include "core/types.hpp"
#include "board/board.hpp"
#include "core/move.hpp"
#include "search/see.hpp"
#include "utils/notation.hpp"
#include "tests/helpers.hpp"

namespace {

bool assert_see_score(
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

    if (!board_position_equal(before, b)) {
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

bool test_see_cases(Board& b) {
    struct TestCase {
        std::string fen;
        std::string move_uci;
        int expected_score;
        std::string description;
    };

    // SEE piece values: P=100, N=300, B=325, R=500, Q=900, K=0

    TestCase tests[] = {
        // --- Undefended captures ---
        {
            "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1",
            "e4d5",
            100,
            "pawn captures undefended pawn"
        },
        {
            "4k3/8/8/3p4/8/4N3/8/4K3 w - - 0 1",
            "e3d5",
            100,
            "knight captures undefended pawn"
        },
        {
            "r3k3/8/8/8/8/8/8/R3K3 w - - 0 1",
            "a1a8",
            500,
            "rook captures undefended rook"
        },
        {
            "7k/5b2/8/8/2B5/8/8/4K3 w - - 0 1",
            "c4f7",
            325,
            "bishop captures undefended bishop"
        },
        {
            "4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1",
            "e4d5",
            900,
            "pawn captures undefended queen"
        },

        // --- Equal exchanges ---
        {
            "4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1",
            "e4d5",
            0,
            "pawn captures pawn defended by pawn (equal trade)"
        },
        {
            "r3k3/8/8/r7/8/8/8/R3K3 w - - 0 1",
            "a1a5",
            0,
            "rook captures rook defended by rook (equal trade)"
        },

        // --- Losing exchanges ---
        {
            "4k3/8/2p5/3p4/3Q4/8/8/4K3 w - - 0 1",
            "d4d5",
            -800,
            "queen captures pawn defended by pawn"
        },
        {
            "4k3/8/2p5/3p4/8/2N5/8/4K3 w - - 0 1",
            "c3d5",
            -200,
            "knight captures pawn defended by pawn"
        },
        {
            "4k3/8/2p5/3n4/8/5B2/8/4K3 w - - 0 1",
            "f3d5",
            -25,
            "bishop captures knight defended by pawn"
        },
        {
            "r3k3/8/8/r7/8/8/8/Q3K3 w - - 0 1",
            "a1a5",
            -400,
            "queen captures rook defended by rook"
        },

        // --- X-ray attacks ---
        {
            "4k3/8/5n2/3r4/8/8/3R4/3RK3 w - - 0 1",
            "d2d5",
            300,
            "rook x-ray: RxR, NxR, RxN"
        },
        {
            "4k3/8/2p5/3p4/4P3/8/6B1/4K3 w - - 0 1",
            "e4d5",
            100,
            "bishop x-ray after pawn exchange: PxP, PxP, BxP"
        },

        // --- King recapture limitation ---
        {
            "8/8/4k3/3p4/8/1B6/8/3RK3 w - - 0 1",
            "b3d5",
            100,
            "king cannot recapture when opponent has x-ray defender"
        },

        // --- En passant ---
        {
            "6k1/2p5/8/3pP3/8/8/8/3R2K1 w - d6 0 1",
            "e5d6",
            100,
            "en passant with rook x-ray preventing recapture loss"
        },
    };

    for (const auto& tc : tests) {
        if (!assert_see_score(b, tc.fen, tc.move_uci, tc.expected_score, tc.description)) {
            return false;
        }
    }

    return true;
}

} // namespace

bool test_see(Board& b) {
    if (!test_see_cases(b)) return false;
    return true;
}
