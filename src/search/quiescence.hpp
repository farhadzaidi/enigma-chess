#pragma once

#include <algorithm>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "search/helpers.hpp"
#include "move_generator/move_generator.hpp"
#include "eval/eval.hpp"
#include "search/see.hpp"

namespace {

// constants
constexpr int SEE_CUTOFF = -200;

} // namespace


template <SearchMode SM>
inline PositionScore quiescence_search(Board& b, PositionScore alpha, PositionScore beta) {
    g_search_state.nodes++;

    if (should_stop_search<SM>()) {
        g_search_state.search_interrupted = true;
        return SEARCH_INTERRUPTED;
    }

    // Draw by fifty-move rule or repetition.
    if (is_engine_draw(b)) {
        return STALEMATE_SCORE;
    }

    bool in_check = b.in_check();

    // First, we get a static evaluation of the position without searching any captures or promotions
    // This serves as a baseline to prevent forcing bad tactical moves
    // Additionally, we can stop the search early if the static evaluation is higher than the beta cutoff
    // This can only be done if we're not in check - otherwise we MUST make a move
    if (!in_check) {
        PositionScore static_eval = evaluate(b);
        alpha = std::max(alpha, static_eval);
        if (alpha >= beta) {
            return alpha;
        }
    }

    // If we're not in check, search captures and promotions. Otherwise, search all moves (evasions)
    MoveList moves = in_check ? generate_moves<MoveGenMode::All>(b) : generate_moves<MoveGenMode::TacticalOnly>(b);

    if (moves.is_empty()) {
        if (in_check) {
            // In check + no legal moves = checkmate
            return -CHECKMATE_SCORE + g_search_state.search_ply(b.ply);
        }

        // No captures or promotions available, return early
        return alpha;
    }

    for (Move move : moves) {
        // Skip losing captures (determined via SEE) unless we're in check
        // We don't hard-prune on SEE < 0, since our SEE implementation is an approximation
        if (!in_check && move.type() == MoveType::Capture && see(b, move) < SEE_CUTOFF) continue;

        b.make_move(move);
        PositionScore score = -quiescence_search<SM>(b, -beta, -alpha);
        b.unmake_move(move);

        if (g_search_state.search_interrupted) {
            return SEARCH_INTERRUPTED;
        }

        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }

    return alpha;
}
