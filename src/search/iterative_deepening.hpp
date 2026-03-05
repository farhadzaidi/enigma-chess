#pragma once

#include <algorithm>
#include <chrono>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "search/helpers.hpp"
#include "search/negamax.hpp"
#include "move_generator/move_generator.hpp"
#include "search/move_selector.hpp"
#include "core/globals.hpp"
#include "search/opening_book.hpp"
#include "core/transposition_table.hpp"

constexpr int ASPIRATION_WINDOW = 25;
constexpr int SCORE_DROP_THRESHOLD = 50;

struct SearchResult {
    Move best_move;
    PositionScore score;
};

// Searches all root moves at a given depth and returns the best move
template <SearchMode SM>
inline SearchResult search_at_depth(
    Board& b,
    SearchDepth depth,
    Move prev_best_move,
    PositionScore alpha,
    PositionScore beta
) {
    Move best_move;
    PositionScore best_score = DUMMY_SCORE;
    MoveList searched_quiet_moves;
    TTEntry* tt_entry = g_transposition_table.get_entry(b.position_hash);
    Move tt_move = tt_entry ? tt_entry->best_move : NULL_MOVE;
    MoveSelector move_selector(b, tt_move, prev_best_move);
    bool is_first_move = true;

    while (true) {
        Move move = move_selector.next_move(b, g_search_state);
        if (move == NULL_MOVE) break;

        b.make_move(move);

        PositionScore score;
        if (is_first_move) {
            score = -negamax<SM>(b, depth - 1, -beta, -alpha);
            is_first_move = false;
        } else {
            score = -negamax<SM>(b, depth - 1, -alpha - 1, -alpha);
            if (score > alpha && score < beta) {
                score = -negamax<SM>(b, depth - 1, -beta, -alpha);
            }
        }

        b.unmake_move(move);

        // Same here - return early if the search is interrutpted, otherwise negate
        // the score to process it for the parent
        if (g_search_state.search_interrupted) {
            return {NULL_MOVE, 0};
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        // Update lower bound used for pruning
        if (score > alpha) {
            alpha = score;
        }

        if (move.type() == MoveType::Quiet) {
            searched_quiet_moves.add(move);
        }

        // If the move we found is too good and our opponent will not allow it (because
        // they found a better move elsewhere), we can break out of the loop and return
        // early, effectively pruning the branch (aka beta cutoff)
        // In other words, the move we found is worse for the opponent than their current
        // lower bound and so we'll never be allowed to play this move
        if (alpha >= beta) {
            handle_beta_cutoff(b, move, depth, searched_quiet_moves);
            break;
        }
    }

    return {best_move, best_score};
}

// --- Iterative Deepening Search ---

template <SearchMode SM>
inline Move search(Board& b, const SearchLimits& limits) {

    // --- Root move generation & book lookup ---

    MoveList moves = generate_moves<MoveGenMode::All>(b);
    if (moves.is_empty()) return NULL_MOVE;

    // Return move from opening book if we can (only after validating that it's legal)
    // We have to validate just in case we have a position hash collision in the book
    if (g_use_own_book) {
        Move book_move = g_opening_book.pick_move(b);
        for (const Move move: moves) {
            if (book_move == move) {
                return book_move;
            }
        }
    }

    // --- Search state initialization ---

    g_search_state.limits = limits;
    g_search_state.nodes = 0;
    g_search_state.search_interrupted = false;
    g_search_state.ply_offset = b.ply;
    g_search_state.killer_1.fill(NULL_MOVE);
    g_search_state.killer_2.fill(NULL_MOVE);
    g_search_state.side_piece_to_history = {};
    g_search_state.from_to_history = {};

    g_search_state.soft_deadline = std::chrono::steady_clock::time_point::max();
    if constexpr (SM == SearchMode::Time) {
        auto now = std::chrono::steady_clock::now();
        g_search_state.deadline = now + std::chrono::milliseconds(limits.hard_time);
        if (limits.soft_time != -1) {
            g_search_state.soft_deadline = now + std::chrono::milliseconds(limits.soft_time);
        }
    }

    // --- Iterative deepening loop ---

    SearchDepth depth = 1;

    Move prev_best_move;
    Move best_move;
    int best_move_stability = 0;

    PositionScore prev_score = 0;
    PositionScore score = 0;

    while (!should_stop_search<SM>()) {
        if constexpr (SM == SearchMode::Depth) {
            if (depth > g_search_state.limits.depth) break;
        }

        // Soft time management: continue past soft limit only if score dropped
        // or best move is unstable. Skip while pondering.
        if constexpr (SM == SearchMode::Time) {
            if (!g_pondering && std::chrono::steady_clock::now() >= g_search_state.soft_deadline) {
                bool score_dropped = (prev_score - score) > SCORE_DROP_THRESHOLD;
                if (!score_dropped && best_move_stability > 0) break;
            }
        }

        // --- Aspiration window search ---

        int alpha, beta;
        int alpha_delta = ASPIRATION_WINDOW, beta_delta = ASPIRATION_WINDOW;
        if (depth == 1) {
            alpha = -CHECKMATE_SCORE;
            beta = CHECKMATE_SCORE;
        } else {
            alpha = std::max(score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            beta = std::min(score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
        }

        SearchResult search_result;
        while (true) {
            if (g_search_state.search_interrupted) break;

            search_result = search_at_depth<SM>(b, depth, best_move, alpha, beta);
            if (search_result.score <= alpha) {
                alpha_delta *= 2;
                alpha = std::max(score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            } else if (search_result.score >= beta) {
                beta_delta *= 2;
                beta = std::min(score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
            } else {
                break;
            }
        }

        if (g_search_state.search_interrupted) break;

        // --- Update iteration state ---

        prev_score = score;
        score = search_result.score;
        if (search_result.best_move != NULL_MOVE) {
            prev_best_move = best_move;
            best_move = search_result.best_move;
        }

        if (best_move == prev_best_move) {
            best_move_stability++;
        } else {
            best_move_stability = 0;
        }

        depth++;
    }

    // Fallback: if we didn't complete depth 1, return an arbitrary legal move
    return best_move == NULL_MOVE && !moves.is_empty() ? moves[0] : best_move;
}
