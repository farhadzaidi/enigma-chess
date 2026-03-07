#pragma once

#include <algorithm>
#include <cstdlib>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "search/helpers.hpp"
#include "search/quiescence.hpp"
#include "search/move_selector.hpp"
#include "eval/eval.hpp"
#include "core/transposition_table.hpp"

namespace {

// constants
constexpr SearchDepth MINIMUM_NULL_MOVE_DEPTH = 3;
constexpr SearchDepth FUTILITY_CUTOFF_DEPTH = 4;
constexpr SearchDepth MINIMUM_IID_DEPTH = 4;

constexpr int NULL_MOVE_BASE_REDUCTION = 2;
constexpr SearchDepth NULL_MOVE_DEEPER_THRESHOLD = 6;

constexpr int FUTILITY_MARGIN_PER_DEPTH = 90;
constexpr int FUTILITY_MARGIN_BASE = 40;

// functions
inline bool can_apply_null_move(
    bool in_check,
    SearchDepth depth,
    bool is_pv_node,
    bool allow_null_move,
    bool has_non_pawn_material
) {
    return (
        allow_null_move &&
        !in_check &&
        depth >= MINIMUM_NULL_MOVE_DEPTH &&
        !is_pv_node &&
        has_non_pawn_material
    );
}

inline bool can_apply_futility(
    PositionScore alpha,
    PositionScore beta,
    bool is_pv_node,
    bool in_check,
    SearchDepth depth
) {
    constexpr PositionScore MATE_THRESHOLD = CHECKMATE_SCORE - MAX_SEARCH_PLY;
    return (
        std::abs(alpha) < MATE_THRESHOLD && // not near mate
        std::abs(beta) < MATE_THRESHOLD &&
        !is_pv_node &&
        !in_check &&
        depth < FUTILITY_CUTOFF_DEPTH
    );
}

template <SearchMode SM>
inline TTProbeResult probe_tt(
    Board& b,
    SearchDepth depth,
    PositionScore alpha,
    PositionScore beta,
    bool is_pv_node
) {
    TTEntry* tt_entry = g_transposition_table.get_entry(b.position_hash);

    if (tt_entry) {
        PositionScore tt_score = denormalize_tt_score(tt_entry->score, g_search_state.search_ply(b.ply));
        Move tt_move = tt_entry->best_move;

        // Use TT score for early cutoff when the stored depth is sufficient.
        // EXACT: always usable, FAIL_HIGH: lower bound >= beta, FAIL_LOW: upper bound <= alpha
        if (
            tt_entry->depth >= depth
            && (
                tt_entry->node == TTNode::Exact
                || (tt_entry->node == TTNode::FailHigh && tt_score >= beta)
                || (tt_entry->node == TTNode::FailLow && tt_score <= alpha)
            )
        ) {
            return {tt_move, true, tt_score};
        }

        return {tt_move, false, 0};
    }

    // No TT hit on a PV node — do a shallow search to seed a TT move
    if (is_pv_node && depth >= MINIMUM_IID_DEPTH) {
        SearchDepth iid_depth = std::max(0, depth / 2);
        negamax<SM>(b, iid_depth, alpha, beta);

        tt_entry = g_transposition_table.get_entry(b.position_hash);
        if (tt_entry) {
            return {tt_entry->best_move, false, 0};
        }
    }

    return {NULL_MOVE, false, 0};
}

} // namespace


template <SearchMode SM>
inline PositionScore negamax(
    Board& b,
    SearchDepth depth,
    PositionScore alpha,
    PositionScore beta,
    bool allow_null_move
) {
    g_search_state.nodes++;

    // --- Early exits ---

    if (should_stop_search<SM>()) {
        g_search_state.search_interrupted = true;
        return SEARCH_INTERRUPTED;
    }

    if (is_engine_draw(b)) {
        return STALEMATE_SCORE;
    }

    if (depth == 0) {
        return quiescence_search<SM>(b, alpha, beta);
    }

    bool is_pv_node = beta - alpha > 1;

    // --- TT probe ---

    TTProbeResult tt_result = probe_tt<SM>(b, depth, alpha, beta, is_pv_node);
    if (tt_result.has_cutoff) return tt_result.cutoff_score;

    PositionScore original_alpha = alpha;
    bool in_check = b.in_check();

    // --- Null move pruning ---

    if (can_apply_null_move(in_check, depth, is_pv_node, allow_null_move, b.has_non_pawn_material(b.to_move))) {
        int reduction = NULL_MOVE_BASE_REDUCTION + (depth >= NULL_MOVE_DEEPER_THRESHOLD);
        b.make_null_move();
        PositionScore score = -negamax<SM>(b, depth - reduction, -beta, -beta + 1, false);
        b.unmake_null_move();

        if (g_search_state.search_interrupted) return SEARCH_INTERRUPTED;

        // Position is so good that even giving the opponent a free move
        // doesn't drop our score below beta — prune this branch
        if (score >= beta) return score;
    }

    // --- Futility pruning setup ---

    bool can_use_futility_pruning = can_apply_futility(alpha, beta, is_pv_node, in_check, depth);

    PositionScore static_eval;
    if (can_use_futility_pruning) {
        static_eval = evaluate(b);
    }
    PositionScore futility_margin = FUTILITY_MARGIN_PER_DEPTH * depth + FUTILITY_MARGIN_BASE;

    // --- Move loop ---

    PositionScore best_score = DUMMY_SCORE;
    Move best_move;
    MoveList searched_quiet_moves;

    MoveSelector move_selector(b, tt_result.tt_move);
    bool has_moves = false;
    bool is_first_move = true;
    int num_moves = 0;

    while (true) {
        Move move = move_selector.next_move(b, g_search_state);
        if (move == NULL_MOVE) break;
        else has_moves = true;

        // Futility pruning: skip quiet moves that can't raise score above alpha
        if (
            can_use_futility_pruning &&
            num_moves > 0 &&
            move_selector.phase == MoveSelPhase::Quiet
        ) {
            if (static_eval + futility_margin < alpha) continue;
        }

        b.make_move(move);
        num_moves++;

        // Late move reduction
        int reduction = LMR_TABLE[depth][num_moves];
        if (is_pv_node) reduction -= 1;
        if (in_check) reduction = 0;
        if (move_selector.in_tactical_phase()) reduction = 0;

        // Check extension: search deeper if this move gives check
        int extension = b.in_check() ? 1 : 0;

        reduction = std::clamp(reduction, 0, depth - 1);

        // Principal Variation Search
        PositionScore score;
        if (is_first_move) {
            score = -negamax<SM>(b, depth - 1 + extension, -beta, -alpha);
            is_first_move = false;
        } else {
            // Search with reduced depth and null window
            score = -negamax<SM>(b, depth - 1 - reduction + extension, -alpha - 1, -alpha);

            // Re-search at full depth if reduced search beat alpha
            if (score > alpha && reduction > 0) {
                score = -negamax<SM>(b, depth - 1 + extension, -alpha - 1, -alpha);
            }

            // Re-search with full window if null window search beat alpha
            if (score > alpha && score < beta) {
                score = -negamax<SM>(b, depth - 1 + extension, -beta, -alpha);
            }
        }

        b.unmake_move(move);

        if (g_search_state.search_interrupted) {
            return SEARCH_INTERRUPTED;
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (move.type() == MoveType::Quiet) {
            searched_quiet_moves.add(move);
        }

        if (alpha >= beta) {
            handle_beta_cutoff(b, move, depth, searched_quiet_moves);
            break;
        }
    }

    // --- Terminal node handling ---

    if (!has_moves) {
        if (b.in_check()) {
            return -CHECKMATE_SCORE + g_search_state.search_ply(b.ply);
        } else {
            return STALEMATE_SCORE;
        }
    }

    store_tt_result(b, best_move, depth, best_score, original_alpha, beta);

    return best_score;
}
