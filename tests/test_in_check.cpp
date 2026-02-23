#include <vector>
#include <string>
#include <iostream>

#include "types.hpp"
#include "board.hpp"
#include "utils.hpp"

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

bool test_in_check(Board& b) {
    if (!test_positions_in_check(b)) return false;
    if (!test_positions_not_in_check(b)) return false;
    return true;
}
