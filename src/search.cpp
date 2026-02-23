#include <algorithm>
#include <chrono>

#include "types.hpp"
#include "search.hpp"
#include "board.hpp"
#include "move_generator.hpp"
#include "evaluate.hpp"
#include "transposition_table.hpp"
#include "opening_book.hpp"

static SearchState ss;
static OpeningBook opening_book;

constexpr uint64_t TIME_CHECK_PERIOD_MASK = 2047;

// Treats fifty-move rule and twofold repetition as automatic draws for search purposes.
// In standard chess both require a player claim, and repetition requires threefold occurrence,
// but pruning these lines early is optimal
static inline bool is_engine_draw(const Board& b) {
    return b.halfmoves >= FIFTY_MOVE_PLY_LIMIT || b.has_repeated();
}

template <SearchMode SM>
static inline bool should_stop_search() {
    // Stop when the search interrupted flag is set or if stop is requested via UCI
    if (ss.search_interrupted || stop_requested) {
        return true;
    }

    if constexpr (SM == TIME) {
        // Check if the search has exceeded its time limit (if search mode is TIME)
        // Only check every N nodes (where N = TIME_CHECK_PERIOD_MASK + 1)
        return (
            (ss.nodes & TIME_CHECK_PERIOD_MASK) == 0
            && std::chrono::steady_clock::now() >= ss.deadline
        );
    } else if constexpr (SM == NODES) {
        // Check if search has exceeded the number of nodes to search (if search mode is NODE)
        return ss.nodes >= ss.limits.nodes;
    } else {
        // In all other cases, we shouldn't stop the search
        // INFINITE = keep going forever (or until stop flag)
        // DEPTH is handled in the iterative search loop
        return false;
    }
}

static inline int score_move(Move m) {
    return m.is_promotion() ? 3 : m.type() == CAPTURE ? 2 : 1;
}

template <bool UsePrevBestMove>
static inline void order_moves(Board& b, MoveList& moves, Move prev_best_move = NULL_MOVE) {
    // Store the TT move if we have a hit
    Move tt_move;
    TTEntry& tt_entry = TT.get_entry(b.zobrist_hash);
    if (TT.is_valid_entry(b.zobrist_hash, tt_entry)) {
        tt_move = tt_entry.best_move;
    }

    // Prioritize promotions, captures, then quiet moves (in that order)
    std::sort(moves.begin(), moves.end(), [prev_best_move, tt_move](const Move& m1, const Move& m2) {
        // Always try the previous best move first if we have it
        if constexpr (UsePrevBestMove) {
            if (m1 == prev_best_move) return true;
            if (m2 == prev_best_move) return false;
        }

        // Then try the TT move if we have one
        if (tt_move != NULL_MOVE) {
            if (m1 == tt_move) return true;
            if (m2 == tt_move) return false;
        }

        return score_move(m1) > score_move(m2);
    });
}

// Normalizes checkmate scores from current search ply to relative distance
// This helps determine how far the mate is from the current ply if this score is retrieved
// from the transposition table
static inline PositionScore normalize_tt_score(PositionScore score, int ply) {
    if (score >= CHECKMATE_SCORE - MAX_SEARCH_PLY) {
        // Winning checkmate - add current ply to score to encode relative distance to mate
        return score + ply;
    } else if (score <= -CHECKMATE_SCORE + MAX_SEARCH_PLY) {
        // Losing checkmate - same as above, but we subtract ply here
        return score - ply;
    }

    // For non-checkmate scores, just return the score as is
    return score;
}

// Denormalizes checkmate score from TT (reverse of above)
static inline PositionScore denormalize_tt_score(PositionScore score, int ply) {
    if (score >= CHECKMATE_SCORE - MAX_SEARCH_PLY) return score - ply;
    if (score <= -CHECKMATE_SCORE + MAX_SEARCH_PLY) return score + ply;
    return score;
}

template <SearchMode SM>
static inline PositionScore quiescence_search(Board& b, PositionScore alpha, PositionScore beta) {
    ss.nodes++;

    if (should_stop_search<SM>()) {
        ss.search_interrupted = true;
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
            return beta;
        }
    }

    // If we're not in check, search captures and promotions. Otherwise, search all moves (evasions)
    MoveList moves = in_check ? generate_moves<ALL>(b) : generate_moves<CAPTURES_AND_PROMOTIONS>(b);
    if (moves.is_empty()) {
        if (in_check) {
            // In check + no legal moves - checkmate
            return -CHECKMATE_SCORE + ss.search_ply(b.ply);
        }

        // No captures or promotions available, return early
        return alpha;
    }

    for (Move move : moves) {
        b.make_move(move);
        PositionScore score = -quiescence_search<SM>(b, -beta, -alpha);
        b.unmake_move(move);

        if (ss.search_interrupted) {
            return SEARCH_INTERRUPTED;
        }

        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }

    return alpha;
}

template <SearchMode SM>
static inline PositionScore negamax(Board& b, SearchDepth depth, PositionScore alpha, PositionScore beta) {
    ss.nodes++;

    if (should_stop_search<SM>()) {
        ss.search_interrupted = true;
        return SEARCH_INTERRUPTED; // Dummy value (for semantics) - will not be used
    }

    // Draw by fifty-move rule or repetition.
    if (is_engine_draw(b)) {
        return STALEMATE_SCORE;
    }

    if (depth == 0) {
        return quiescence_search<SM>(b, alpha, beta);
    }

    // Probe transposition table
    TTEntry& tt_entry = TT.get_entry(b.zobrist_hash);
    if (TT.is_valid_entry(b.zobrist_hash, tt_entry)) {
        // Denormalize score before returning
        PositionScore tt_score = denormalize_tt_score(tt_entry.score, ss.search_ply(b.ply));

        // We can use the TT entry score to cutoff early if the depth of the entry
        // is greater than or equal to the current depth of this node.
        // Cutoff policy:
        // - EXACT: exact score at this depth, always usable
        // - FAIL_HIGH: lower bound, usable when it exceeds beta
        // - FAIL_LOW: upper bound, usable when it is below alpha
        if (
            tt_entry.depth >= depth
            && (
                tt_entry.node == EXACT
                || (tt_entry.node == FAIL_HIGH && tt_score >= beta)
                || (tt_entry.node == FAIL_LOW && tt_score <= alpha)
            )
        ) {
            return tt_score;
        }
    }

    MoveList moves = generate_moves<ALL>(b);
    order_moves<false>(b, moves);

    // Side to move has no remaining moves
    if (moves.is_empty()) {
        if (b.in_check()) {
            // If we're in check with no moves, then that is a checkmate
            // Add ply to the score to incentivize drawing out the game for the
            // losing side or ending the game quicker for the winning side
            return -CHECKMATE_SCORE + ss.search_ply(b.ply);
        } else {
            // If we're not in check with no moves, then that is a stalemate
            return STALEMATE_SCORE;
        }
    }

    // Store original alpha value for this node to determine if it's a fail-low TT node
    PositionScore original_alpha = alpha;
    Move best_move;

    for (Move move : moves) {
        b.make_move(move);
        PositionScore score = -negamax<SM>(b, depth - 1, -beta, -alpha);
        b.unmake_move(move);

        // Discard the score and return early if the search has been interrupted
        if (ss.search_interrupted) {
            return SEARCH_INTERRUPTED;
        }

        // Update lower bound and determine if we need to prune this branch
        if (score > alpha) {
            alpha = score;
            best_move = move;
        }

        if (alpha >= beta) {
            break;
        }
    }

    // Determine the type of entry based on the final score
    TTNode tt_node;
    if (alpha >= beta) {
        tt_node = FAIL_HIGH;
    } else if (alpha <= original_alpha) {
        tt_node = FAIL_LOW;
    } else {
        tt_node = EXACT;
    }

    // Normalize score before storing
    PositionScore tt_score = normalize_tt_score(alpha, ss.search_ply(b.ply));

    // Store TT entry
    TT.add_entry(TTEntry{b.zobrist_hash, best_move, depth, tt_score, tt_node});

    return alpha;
}

// Searches all root moves at a given depth and returns the best move
template <SearchMode SM>
static Move search_at_depth(Board& b, SearchDepth depth, Move prev_best_move) {
    Move best_move;

    // Alpha will serve as our lower bound (best score so far at this depth)
    PositionScore alpha = MIN_SCORE;

    // Beta will serve as our upper bound - if we find a move better than beta
    // then that move is too good and our opponent won't allow it (it's worse
    // for them than their lower bound)
    PositionScore beta = MAX_SCORE;

    MoveList moves = generate_moves<ALL>(b);
    order_moves<true>(b, moves, prev_best_move);

    for (Move move : moves) {
        b.make_move(move);
        PositionScore score = -negamax<SM>(b, depth - 1, -beta, -alpha);
        b.unmake_move(move);

        // Same here - return early if the search is interrutpted, otherwise negate
        // the score to process it for the parent
        if (ss.search_interrupted) {
            return NULL_MOVE;
        }

        // If we found a move better than the current best move at this depth,
        // update the best score (alpha) and the best move at this depth
        if (score > alpha) {
            alpha = score;
            best_move = move;
        }

        // If the move we found is too good and our opponent will not allow it (because
        // they found a better move elsewhere), we can break out of the loop and return
        // early, effectively pruning the branch (aka beta cutoff)
        // In other words, the move we found is worse for the opponent than their current
        // lower bound and so we'll never be allowed to play this move
        if (alpha >= beta) {
            break;
        }
    }

    return best_move;
}

// Initializes search globals and performs iterative deepening search
template <SearchMode SM>
Move search(Board& b, const SearchLimits& limits) {
    // Generate moves at root
    // Useful for checking legality of book moves and also returning any legal
    // move at the end if we didn't have the time to find one
    MoveList moves = generate_moves<ALL>(b);

    // Return move from opening book if we can (only after validating that it's legal)
    // We have to validate just in case we have a position hash collision in the book
    Move book_move = opening_book.pick_move(b);
    for (const Move move: moves) {
        if (book_move == move) {
            return book_move;
        }
    }

    ss.limits = limits;
    ss.nodes = 0;
    ss.search_interrupted = false;
    ss.ply_offset = b.ply;

    // Calculate search deadline based on time limit if search mode is TIME
    if constexpr (SM == TIME) {
        ss.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.time);
    }

    SearchDepth depth = 1;
    Move best_move;

    // Iterative search loop
    while (!should_stop_search<SM>()) {
        // Check if we've hit the max depth if search mode is DEPTH
        if constexpr (SM == DEPTH) {
            if (depth > ss.limits.depth) break;
        }

        Move best_move_at_depth = search_at_depth<SM>(b, depth, best_move);

        if (best_move_at_depth != NULL_MOVE) {
            best_move = best_move_at_depth;
        }

        if (ss.search_interrupted) break;
        depth++;
    }

    // In the rare case where we have legal moves at this position, but we weren't able
    // to complete our first search (depth = 1), we return an arbitrary move
    return best_move == NULL_MOVE && !moves.is_empty() ? moves[0] : best_move;
}

// Explicit template instantiations
template Move search<TIME>(Board& b, const SearchLimits& limits);
template Move search<NODES>(Board& b, const SearchLimits& limits);
template Move search<DEPTH>(Board& b, const SearchLimits& limits);
template Move search<INFINITE>(Board& b, const SearchLimits& limits);
