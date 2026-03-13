#pragma once

#include <algorithm>
#include <chrono>

#include "types.hpp"
#include "constants.hpp"
#include "move.hpp"
#include "board/board.hpp"
#include "search/context.hpp"
#include "transposition_table.hpp"
#include "precompute/tables.hpp"

namespace {

constexpr uint64_t TIME_CHECK_PERIOD_MASK = 2047;

// functions
inline void update_killer_table(ThreadContext& ctx, Move move, int ply) {
    if (move != ctx.killer_1[ply]) {
        ctx.killer_2[ply] = ctx.killer_1[ply];
        ctx.killer_1[ply] = move;
    }
}

inline bool should_stop_search(ThreadContext& ctx) {
    if (ctx.search_interrupted || g_shared.external_stop || (!ctx.is_main_thread && g_shared.main_finished)) {
        return true;
    }

    if ((ctx.nodes & TIME_CHECK_PERIOD_MASK) != 0) {
        return false;
    }

    if (!ctx.is_main_thread || !ctx.has_runtime_limits()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();

    if (ctx.hard_time != -1 && now >= ctx.hard_deadline) {
        ctx.search_interrupted = true;
        g_shared.external_stop = true;
        return true;
    }

    if (ctx.max_nodes > 0 && ctx.nodes >= ctx.max_nodes) {
        ctx.search_interrupted = true;
        g_shared.external_stop = true;
        return true;
    }

    return false;
}

} // namespace

// Treats fifty-move rule and twofold repetition as automatic draws for search purposes.
// In standard chess both require a player claim, and repetition requires threefold occurrence,
// but pruning these lines early is optimal
inline bool is_engine_draw(const Board& b) {
    return b.halfmoves >= FIFTY_MOVE_PLY_LIMIT || b.has_repeated();
}

namespace {

// functions
inline void update_history_tables(Board& b, ThreadContext& ctx, Move move, MoveScore bonus) {
    MoveScore clamped_bonus = std::clamp(bonus, MIN_MOVE_SCORE, MAX_MOVE_SCORE);
    Piece moving_piece = b.piece_map[move.from()];

    MoveScore& side_piece_to_history_score = ctx.side_piece_to_history[b.to_move][moving_piece][move.to()];
    side_piece_to_history_score += clamped_bonus - side_piece_to_history_score * std::abs(clamped_bonus) / MAX_MOVE_SCORE;

    MoveScore& from_to_history_score = ctx.from_to_history[move.from()][move.to()];
    from_to_history_score += clamped_bonus - from_to_history_score * std::abs(clamped_bonus) / MAX_MOVE_SCORE;
}

} // namespace

// On a beta cutoff by a quiet move, reward the cutoff move and penalize
// all previously searched quiet moves that failed to cause a cutoff.
// Non-quiet cutoffs are ignored since tactical moves use different ordering.
inline void handle_beta_cutoff(
    Board& b,
    ThreadContext& ctx,
    Move cutoff_move,
    SearchDepth depth,
    MoveList& searched_quiet_moves
) {
    if (cutoff_move.type() != MT_QUIET) return;

    // Pop the cutoff move itself — it was added to searched_quiet_moves before
    // the beta check, but shouldn't receive a penalty
    searched_quiet_moves.pop();

    int ply = ctx.search_ply(b.ply);
    MoveScore bonus = depth * depth;

    update_killer_table(ctx, cutoff_move, ply);
    update_history_tables(b, ctx, cutoff_move, bonus);

    MoveScore malus = -(bonus / 2);
    for (const Move move : searched_quiet_moves) {
        update_history_tables(b, ctx, move, malus);
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
    ThreadContext& ctx,
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

    PositionScore tt_score = normalize_tt_score(best_score, ctx.search_ply(b.ply));
    g_shared.transposition_table.add_entry(TTEntry{b.position_hash, best_move, depth, tt_score, tt_node});
}

struct TTProbeResult {
    Move tt_move;
    bool has_cutoff;
    PositionScore cutoff_score;
};

// Forward declaration needed since probe_tt (IID) calls negamax
inline PositionScore negamax(
    Board& b,
    ThreadContext& ctx,
    SearchDepth depth,
    PositionScore alpha,
    PositionScore beta,
    bool allow_null_move = true
);
