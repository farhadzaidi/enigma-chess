#include <iostream>

#include "core/types.hpp"
#include "core/globals.hpp"
#include "board/board.hpp"
#include "search/search.hpp"
#include "move_generator/move_generator.hpp"
#include "helpers.hpp"

static bool test_search_nodes_zero_fallback(Board& b) {
    const bool old_book = g_use_own_book;
    g_use_own_book = false;

    b.load_from_fen(START_POS_FEN);
    Board before = b;

    MoveList legal_moves = generate_moves<MoveGenMode::All>(b);
    if (legal_moves.is_empty()) {
        std::clog << "[FAILURE] 'search_nodes_zero_fallback' - Start position must have legal moves\n";
        g_use_own_book = old_book;
        return false;
    }

    Move best = search_nodes(b, 0);

    g_use_own_book = old_book;

    if (!board_position_equal(before, b, true)) {
        std::clog << "[FAILURE] 'search_nodes_zero_fallback' - Search mutated board\n";
        return false;
    }

    if (best != legal_moves[0]) {
        std::clog << "[FAILURE] 'search_nodes_zero_fallback' - Expected fallback to first legal move\n";
        return false;
    }

    return true;
}

static bool test_search_g_stop_requested_fallback(Board& b) {
    const bool old_book = g_use_own_book;
    g_use_own_book = false;

    b.load_from_fen(START_POS_FEN);
    Board before = b;
    MoveList legal_moves = generate_moves<MoveGenMode::All>(b);

    g_stop_requested = true;
    Move best = search_depth(b, 4);
    g_stop_requested = false;

    g_use_own_book = old_book;

    if (!board_position_equal(before, b, true)) {
        std::clog << "[FAILURE] 'search_g_stop_requested_fallback' - Search mutated board\n";
        return false;
    }

    if (best != legal_moves[0]) {
        std::clog << "[FAILURE] 'search_g_stop_requested_fallback' - Expected fallback to first legal move when stop is pre-set\n";
        return false;
    }

    return true;
}

static bool test_search_nodes_small_limit_returns_legal(Board& b) {
    const bool old_book = g_use_own_book;
    g_use_own_book = false;

    b.load_from_fen(START_POS_FEN);
    Board before = b;

    MoveList legal_moves = generate_moves<MoveGenMode::All>(b);
    Move best = search_nodes(b, 1);

    g_use_own_book = old_book;

    if (!board_position_equal(before, b, true)) {
        std::clog << "[FAILURE] 'search_nodes_small_limit' - Search mutated board\n";
        return false;
    }

    if (best == NULL_MOVE || !move_list_contains(legal_moves, best)) {
        std::clog << "[FAILURE] 'search_nodes_small_limit' - Expected legal non-null move\n";
        return false;
    }

    return true;
}

bool test_search_limits(Board& b) {
    if (!test_search_nodes_zero_fallback(b)) return false;
    if (!test_search_g_stop_requested_fallback(b)) return false;
    if (!test_search_nodes_small_limit_returns_legal(b)) return false;
    return true;
}
