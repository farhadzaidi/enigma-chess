#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "types.hpp"
#include "constants.hpp"
#include "board/board.hpp"
#include "driver/thread_pool.hpp"
#include "print.hpp"
#include "search/helpers.hpp"
#include "search/negamax.hpp"
#include "move_generator/move_generator.hpp"
#include "search/move_selector.hpp"
#include "search/opening_book.hpp"
#include "transposition_table.hpp"

namespace {

constexpr int ASPIRATION_WINDOW = 25;
constexpr int SCORE_DROP_THRESHOLD = 50;

struct SearchResult {
    Move best_move;
    PositionScore score;
};

inline void signal_stop(const ThreadContext& ctx) {
    if (ctx.is_main_thread) {
        g_shared.main_finished = true;
    }
}

inline void emit_search_info(const ThreadContext& ctx, SearchDepth depth, PositionScore score) {
    if (!ctx.is_main_thread) {
        return;
    }

    uint64_t elapsed_ms = ctx.elapsed_ms();
    uint64_t nodes = ctx.thread_pool ? ctx.thread_pool->total_nodes() : ctx.nodes;

    uint64_t nps = elapsed_ms > 0 ? (nodes * 1000) / elapsed_ms : 0;
    bool is_mate = std::abs(score) > CHECKMATE_SCORE - MAX_SEARCH_PLY;
    int plies_to_mate = CHECKMATE_SCORE - std::abs(score);
    int mate_in = (plies_to_mate + 1) / 2 * (score > 0 ? 1 : -1);
    std::string score_str = is_mate ? "mate " + std::to_string(mate_in) : "cp " + std::to_string(score);

    uci_print(
        "info"
        " depth " + std::to_string(depth) +
        " score " + score_str +
        " nodes " + std::to_string(nodes) +
        " nps " + std::to_string(nps) +
        " time " + std::to_string(elapsed_ms)
    );
}

inline SearchResult search_at_depth(
    Board& b,
    ThreadContext& ctx,
    SearchDepth depth,
    Move prev_best_move,
    PositionScore alpha,
    PositionScore beta
) {
    Move best_move;
    PositionScore best_score = DUMMY_SCORE;
    MoveList searched_quiet_moves;
    TTEntry* tt_entry = g_shared.transposition_table.get_entry(b.position_hash);
    Move tt_move = tt_entry ? tt_entry->move() : NULL_MOVE;
    MoveSelector move_selector(b, tt_move, prev_best_move);
    bool is_first_move = true;

    while (true) {
        Move move = move_selector.next_move(b, ctx);
        if (move == NULL_MOVE) break;

        b.make_move(move);

        PositionScore score;
        if (is_first_move) {
            score = -negamax(b, ctx, depth - 1, -beta, -alpha);
            is_first_move = false;
        } else {
            score = -negamax(b, ctx, depth - 1, -alpha - 1, -alpha);
            if (score > alpha && score < beta) {
                score = -negamax(b, ctx, depth - 1, -beta, -alpha);
            }
        }

        b.unmake_move(move);

        if (should_stop_search(ctx)) {
            return {NULL_MOVE, 0};
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (move.type() == MT_QUIET) {
            searched_quiet_moves.add(move);
        }

        if (alpha >= beta) {
            handle_beta_cutoff(b, ctx, move, depth, searched_quiet_moves);
            break;
        }
    }

    return {best_move, best_score};
}

} // namespace


inline Move search(Board& b, ThreadContext& ctx, SearchDepth depth) {
    MoveList moves = generate_moves<MoveGenMode::All>(b);
    if (moves.is_empty()) {
        signal_stop(ctx);
        return NULL_MOVE;
    }

    if (g_shared.use_opening_book) {
        Move book_move = g_shared.opening_book.pick_move(b);
        for (const Move move : moves) {
            if (book_move == move) {
                signal_stop(ctx);
                return book_move;
            }
        }
    }

    Move prev_best_move;
    Move best_move;
    int best_move_stability = 0;

    PositionScore prev_score = 0;
    PositionScore score = 0;
    ctx.reset(b.ply);
    ctx.search_start = std::chrono::steady_clock::now();
    ctx.set_deadlines_from(ctx.search_start);

    while (true) {
        if (should_stop_search(ctx)) {
            break;
        }

        if (ctx.is_main_thread && depth > ctx.max_depth) {
            break;
        }

        if (ctx.is_main_thread && ctx.has_runtime_limits() && ctx.soft_time != -1) {
            bool soft_limit_hit = std::chrono::steady_clock::now() >= ctx.soft_deadline;
            if (soft_limit_hit) {
                bool score_dropped = (prev_score - score) > SCORE_DROP_THRESHOLD;
                if (!score_dropped && best_move_stability > 0) {
                    break;
                }
            }
        }

        int alpha, beta;
        int alpha_delta = ASPIRATION_WINDOW;
        int beta_delta = ASPIRATION_WINDOW;
        if (depth == 1) {
            alpha = -CHECKMATE_SCORE;
            beta = CHECKMATE_SCORE;
        } else {
            alpha = std::max(score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            beta = std::min(score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
        }

        SearchResult search_result;
        while (true) {
            if (should_stop_search(ctx)) {
                break;
            }

            search_result = search_at_depth(b, ctx, depth, best_move, alpha, beta);
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

        if (should_stop_search(ctx)) {
            break;
        }

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

        emit_search_info(ctx, depth, score);

        depth++;
    }

    signal_stop(ctx);
    return best_move == NULL_MOVE && !moves.is_empty() ? moves[0] : best_move;
}

inline Move search(Board& b, const SearchLimits& limits, int num_threads = 1) {
    ThreadPool pool;
    return pool.search(b, limits, num_threads);
}

inline Move search_time(Board& b, int soft_time, int hard_time) {
    SearchLimits limits;
    limits.soft_time = soft_time;
    limits.hard_time = hard_time;
    return search(b, limits, 1);
}

inline Move search_nodes(Board& b, uint64_t nodes) {
    SearchLimits limits;
    limits.max_nodes = nodes;
    return search(b, limits, 1);
}

inline Move search_depth(Board& b, SearchDepth depth) {
    SearchLimits limits;
    limits.max_depth = depth;
    return search(b, limits, 1);
}

inline Move search_infinite(Board& b) {
    return search(b, SearchLimits{}, 1);
}
