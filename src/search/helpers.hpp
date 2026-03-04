#pragma once

#include <chrono>
#include <algorithm>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "core/move.hpp"
#include "board/board.hpp"
#include "core/globals.hpp"
#include "search/search_state.hpp"
#include "core/transposition_table.hpp"
#include "precompute/tables.hpp"

constexpr uint64_t TIME_CHECK_PERIOD_MASK = 2047;

// --- Helper Functions ---

template <SearchMode SM>
inline bool should_stop_search() {
    // Stop when the search interrupted flag is set or if stop is requested via UCI
    if (search_state.search_interrupted || stop_requested) {
        return true;
    }

    if constexpr (SM == SearchMode::Time) {
        // Check if the search has exceeded its time limit (if search mode is TIME)
        // Only check every N nodes (where N = TIME_CHECK_PERIOD_MASK + 1)
        // Skip time check while pondering
        return (
            !pondering
            && (search_state.nodes & TIME_CHECK_PERIOD_MASK) == 0
            && std::chrono::steady_clock::now() >= search_state.deadline
        );
    } else if constexpr (SM == SearchMode::Nodes) {
        // Check if search has exceeded the number of nodes to search (if search mode is NODE)
        return search_state.nodes >= search_state.limits.nodes;
    } else {
        // In all other cases, we shouldn't stop the search
        // INFINITE = keep going forever (or until stop flag)
        // DEPTH is handled in the iterative search loop
        return false;
    }
}

// Treats fifty-move rule and twofold repetition as automatic draws for search purposes.
// In standard chess both require a player claim, and repetition requires threefold occurrence,
// but pruning these lines early is optimal
inline bool is_engine_draw(const Board& b) {
    return b.halfmoves >= FIFTY_MOVE_PLY_LIMIT || b.has_repeated();
}

inline void update_killer_table(Move move, int ply) {
    if (move != search_state.killer_1[ply]) {
        search_state.killer_2[ply] = search_state.killer_1[ply];
        search_state.killer_1[ply] = move;
    }
}

inline void update_history_tables(Board& b, Move move, MoveScore bonus) {
    MoveScore clamped_bonus = std::clamp(bonus, MIN_MOVE_SCORE, MAX_MOVE_SCORE);
    Piece moving_piece = b.piece_map[move.from()];

    MoveScore& side_piece_to_history_score = search_state.side_piece_to_history[b.to_move][moving_piece][move.to()];
    side_piece_to_history_score += clamped_bonus - side_piece_to_history_score * std::abs(clamped_bonus) / MAX_MOVE_SCORE;

    MoveScore& from_to_history_score = search_state.from_to_history[move.from()][move.to()];
    from_to_history_score += clamped_bonus - from_to_history_score * std::abs(clamped_bonus) / MAX_MOVE_SCORE;
}

// On a beta cutoff by a quiet move, reward the cutoff move and penalize
// all previously searched quiet moves that failed to cause a cutoff.
// Non-quiet cutoffs are ignored since tactical moves use different ordering.
inline void handle_beta_cutoff(
    Board& b,
    Move cutoff_move,
    SearchDepth depth,
    MoveList& searched_quiet_moves
) {
    if (cutoff_move.type() != MoveType::Quiet) return;

    // Pop the cutoff move itself — it was added to searched_quiet_moves before
    // the beta check, but shouldn't receive a penalty
    searched_quiet_moves.pop();

    int ply = search_state.search_ply(b.ply);
    MoveScore bonus = depth * depth;

    update_killer_table(cutoff_move, ply);
    update_history_tables(b, cutoff_move, bonus);

    MoveScore malus = -(bonus / 2);
    for (const Move move : searched_quiet_moves) {
        update_history_tables(b, move, malus);
    }
}

// --- TT Helpers ---

inline PositionScore normalize_tt_score(PositionScore score, int ply) {
    if (score >= CHECKMATE_SCORE - MAX_SEARCH_PLY) return score + ply;
    if (score <= -CHECKMATE_SCORE + MAX_SEARCH_PLY) return score - ply;
    return score;
}

inline PositionScore denormalize_tt_score(PositionScore score, int ply) {
    if (score >= CHECKMATE_SCORE - MAX_SEARCH_PLY) return score - ply;
    if (score <= -CHECKMATE_SCORE + MAX_SEARCH_PLY) return score + ply;
    return score;
}

inline void store_tt_result(
    const Board& b,
    Move best_move,
    SearchDepth depth,
    PositionScore best_score,
    PositionScore original_alpha,
    PositionScore beta
) {
    TTNode tt_node;
    if (best_score >= beta)                tt_node = TTNode::FailHigh;
    else if (best_score <= original_alpha) tt_node = TTNode::FailLow;
    else                                   tt_node = TTNode::Exact;

    PositionScore tt_score = normalize_tt_score(best_score, search_state.search_ply(b.ply));
    transposition_table.add_entry(TTEntry{b.position_hash, best_move, depth, tt_score, tt_node});
}

struct TTProbeResult {
    Move tt_move;
    bool has_cutoff;
    PositionScore cutoff_score;
};

// Forward declaration needed since probe_tt (IID) calls negamax
template <SearchMode SM>
inline PositionScore negamax(
    Board& b,
    SearchDepth depth,
    PositionScore alpha,
    PositionScore beta,
    bool allow_null_move = true
);
