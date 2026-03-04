#include <vector>
#include <string>
#include <iostream>

#include "core/types.hpp"
#include "board/board.hpp"
#include "utils/notation.hpp"
#include "utils/file_io.hpp"

static bool test_positions_in_check(Board& b) {
    std::vector<std::string> in_check_buffer;
    read_file(in_check_buffer, SINGLE_CHECK_EPD);
    read_file(in_check_buffer, DOUBLE_CHECK_EPD);
    for (const auto& line : in_check_buffer) {
        auto result = parse_perft_epd_line(line);

        b.reset();
        b.load_from_fen(result.fen);

        if (!b.in_check()) {
            std::clog << "[FAILURE] 'in_check' - Expected side to move (" << (b.to_move == WHITE ? "white" : "black")
                << ") to be in check, but function returned false\n";
            std::clog << "FEN: " << result.fen << "\n";
            return false;
        }
    }

    return true;
}

static bool test_positions_not_in_check(Board& b) {
    std::vector<std::string> not_in_check_buffer;
    read_file(not_in_check_buffer, NOT_IN_CHECK_FEN);
    for (const auto& fen : not_in_check_buffer) {
        b.reset();
        b.load_from_fen(fen);

        if (b.in_check()) {
            std::clog << "[FAILURE] 'in_check' - Expected side to move (" << (b.to_move == WHITE ? "white" : "black")
                << ") to not be in check, but function returned true\n";
            std::clog << "FEN: " << fen << "\n";
            return false;
        }
    }

    return true;
}

static bool test_in_check_side(Board& b) {
    // Black to move and black king is checked by white queen.
    b.reset();
    b.load_from_fen("4k3/8/8/4Q3/8/8/8/4K3 b - - 0 1");
    if (!b.in_check()) return false;
    if (!b.in_check(BLACK)) {
        std::clog << "[FAILURE] 'in_check_side' - Expected black king to be in check\n";
        return false;
    }
    if (b.in_check(WHITE)) {
        std::clog << "[FAILURE] 'in_check_side' - Expected white king to not be in check\n";
        return false;
    }

    // Black to move and white king is checked by black queen.
    b.reset();
    b.load_from_fen("4k3/8/8/8/4q3/8/8/4K3 b - - 0 1");
    if (b.in_check()) {
        std::clog << "[FAILURE] 'in_check_side' - Expected side to move (black) to not be in check\n";
        return false;
    }
    if (b.in_check(BLACK)) {
        std::clog << "[FAILURE] 'in_check_side' - Expected black king to not be in check\n";
        return false;
    }
    if (!b.in_check(WHITE)) {
        std::clog << "[FAILURE] 'in_check_side' - Expected white king to be in check\n";
        return false;
    }

    return true;
}

bool test_in_check(Board& b) {
    if (!test_positions_in_check(b)) return false;
    if (!test_positions_not_in_check(b)) return false;
    if (!test_in_check_side(b)) return false;
    return true;
}
