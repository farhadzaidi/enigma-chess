#pragma once

#include "types.hpp"
#include "move.hpp"
#include "board.hpp"

// Test assertion macros.
// msg is streamed, so it can contain << operators: ASSERT(x, "test", "got " << x)

#define ASSERT(cond, test_name, msg) \
    if (!(cond)) { std::clog << "[FAILURE] '" << (test_name) << "' - " << msg << "\n"; return false; }

#define ASSERT_EQ(actual, expected, test_name, msg) \
    if ((actual) != (expected)) { \
        std::clog << "[FAILURE] '" << (test_name) << "' - " << msg << "\n"; \
        std::clog << "  Expected: " << (expected) << "  Got: " << (actual) << "\n"; \
        return false; \
    }

#define ASSERT_BOARD(board, before, test_name, msg) \
    if (!board_position_equal(board, before)) { \
        std::clog << "[FAILURE] '" << (test_name) << "' - " << msg << "\n"; \
        return false; \
    }

bool board_position_equal(const Board& a, const Board& b);
bool move_list_contains(const MoveList& moves, Move target);
