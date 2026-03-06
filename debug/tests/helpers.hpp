#pragma once

#include "core/types.hpp"
#include "core/move.hpp"
#include "core/globals.hpp"
#include "board/board.hpp"
#include "search/search_state.hpp"

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

inline bool board_position_equal(const Board& a, const Board& b) {
    if (a.position_hash != b.position_hash) return false;
    if (a.pawn_hash != b.pawn_hash) return false;
    if (a.occupied != b.occupied) return false;
    if (a.to_move != b.to_move) return false;
    if (a.castling_rights != b.castling_rights) return false;
    if (a.en_passant_target != b.en_passant_target) return false;
    if (a.halfmoves != b.halfmoves) return false;
    if (a.fullmoves != b.fullmoves) return false;
    if (a.ply != b.ply) return false;
    if (a.game_phase != b.game_phase) return false;

    for (int side = 0; side < NUM_SIDES; side++) {
        if (a.sides[side] != b.sides[side]) return false;
        if (a.king_squares[side] != b.king_squares[side]) return false;
        if (a.early_score[side] != b.early_score[side]) return false;
        if (a.late_score[side] != b.late_score[side]) return false;

        for (int piece = 0; piece < NUM_PIECES; piece++) {
            if (a.pieces[side][piece] != b.pieces[side][piece]) return false;
        }
    }

    for (int sq = 0; sq < NUM_SQUARES; sq++) {
        if (a.piece_map[sq] != b.piece_map[sq]) return false;
    }

    return true;
}

inline bool move_list_contains(const MoveList& moves, Move target) {
    for (const Move& move : moves) {
        if (move == target) return true;
    }
    return false;
}

inline void reset_search_state_for_test(const Board& b, bool clear_stop_requested = true) {
    g_search_state = {};
    g_search_state.ply_offset = b.ply;
    g_search_state.killer_1.fill(NULL_MOVE);
    g_search_state.killer_2.fill(NULL_MOVE);
    g_search_state.side_piece_to_history = {};
    g_search_state.from_to_history = {};
    g_search_state.search_interrupted = false;
    g_search_state.nodes = 0;

    if (clear_stop_requested) {
        g_stop_requested = false;
    }
}
