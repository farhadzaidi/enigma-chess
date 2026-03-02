#include <algorithm>
#include <chrono>
#include <cmath>

#include "types.hpp"
#include "search.hpp"
#include "board.hpp"
#include "move_generator.hpp"
#include "evaluate.hpp"
#include "transposition_table.hpp"
#include "opening_book.hpp"
#include "move_selector.hpp"
#include "see.hpp"

static SearchState ss;
static OpeningBook opening_book;

constexpr uint64_t TIME_CHECK_PERIOD_MASK = 2047;
constexpr int SEE_CUTOFF = -200;
constexpr int ASPIRATION_WINDOW = 25;
constexpr SearchDepth MINIMUM_NULL_MOVE_DEPTH = 3;
constexpr SearchDepth FUTILITY_CUTOFF_DEPTH = 4;

struct SearchResult {
    Move best_move;
    PositionScore score;
};

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

static inline void update_killer_table(Move move, int ply) {
    if (move != ss.killer_1[ply]) {
        ss.killer_2[ply] = ss.killer_1[ply];
        ss.killer_1[ply] = move;
    }
}

static inline void update_history_tables(Board& b, Move move, MoveScore bonus) {
    MoveScore clamped_bonus = std::clamp(bonus, MIN_MOVE_SCORE, MAX_MOVE_SCORE);
    Piece moving_piece = b.piece_map[move.from()];

    MoveScore& color_piece_to_score = ss.color_piece_to[b.to_move][moving_piece][move.to()];
    color_piece_to_score += clamped_bonus - color_piece_to_score * std::abs(clamped_bonus) / MAX_MOVE_SCORE;

    MoveScore& from_to_score = ss.from_to[move.from()][move.to()];
    from_to_score += clamped_bonus - from_to_score * std::abs(clamped_bonus) / MAX_MOVE_SCORE;
}

static inline void update_quiet_heuristic_tables(
    Board& b,
    Move cutoff_move,
    SearchDepth depth,
    const MoveList& searched_quiet_moves
) {
    int ply = ss.search_ply(b.ply);
    MoveScore bonus = depth * depth;

    update_killer_table(cutoff_move, ply);
    update_history_tables(b, cutoff_move, bonus);

    // Apply penalty to quiet moves that didn't cause a cutoff
    MoveScore malus = -(bonus / 2);
    for (const Move move : searched_quiet_moves) {
        update_history_tables(b, move, malus);
    }
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
            return alpha;
        }
    }

    // If we're not in check, search captures and promotions. Otherwise, search all moves (evasions)
    MoveList moves = in_check ? generate_moves<ALL>(b) : generate_moves<CAPTURES_AND_PROMOTIONS>(b);

    if (moves.is_empty()) {
        if (in_check) {
            // In check + no legal moves = checkmate
            return -CHECKMATE_SCORE + ss.search_ply(b.ply);
        }

        // No captures or promotions available, return early
        return alpha;
    }

    for (Move move : moves) {
        // Skip losing captures (determined via SEE) unless we're in check
        // We don't hard-prune on SEE < 0, since our SEE implementation is an approximation
        if (!in_check && move.type() == CAPTURE && see(b, move) < SEE_CUTOFF) continue;

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
static inline PositionScore negamax(
    Board& b,
    SearchDepth depth,
    PositionScore alpha,
    PositionScore beta,
    bool allow_null_move = true
) {
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
    bool is_valid_tt_entry = TT.is_valid_entry(b.zobrist_hash, tt_entry);
    if (is_valid_tt_entry) {
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

    // Store original alpha value for this node to determine if it's a fail-low TT node
    PositionScore original_alpha = alpha;
    PositionScore best_score = DUMMY_SCORE;
    Move best_move;
    MoveList searched_quiet_moves;

    Move tt_move = is_valid_tt_entry ? tt_entry.best_move : NULL_MOVE;
    MoveSelector move_selector(b, tt_move);
    bool has_moves = false;
    bool is_first_move = true;
    int num_moves = 0;

    bool in_check = b.in_check();
    bool is_pv_node = beta - alpha > 1; 
    bool has_non_pawn_material = (
        b.pieces[b.to_move][KNIGHT] |
        b.pieces[b.to_move][BISHOP] |
        b.pieces[b.to_move][ROOK]   |
        b.pieces[b.to_move][QUEEN]
    );

    // Try null move first if we can
    if (
        allow_null_move &&
        !in_check &&
        depth >= MINIMUM_NULL_MOVE_DEPTH &&
        !is_pv_node &&
        has_non_pawn_material
    ) {
        int R = 2 + (depth >= 6);
        b.make_null_move();
        PositionScore score = -negamax<SM>(b, depth - R, -beta, -beta + 1, false);
        b.unmake_null_move();

        if (ss.search_interrupted) return SEARCH_INTERRUPTED;

        // Position is so good that even giving the oppent a free move
        // doesn't drop our score below beta - we prune this since our
        // opponent would never let us play here
        if (score >= beta) return score;
    }

    bool is_close_to_mate = (
        std::abs(alpha) >= CHECKMATE_SCORE - MAX_SEARCH_PLY || 
        std::abs(beta) >= CHECKMATE_SCORE - MAX_SEARCH_PLY
    );

    bool can_use_futility_pruning = (
        !is_close_to_mate && 
        !is_pv_node && 
        !in_check && 
        depth < FUTILITY_CUTOFF_DEPTH
    );

    // Compute static eval for futility pruning
    PositionScore static_eval;
    if (can_use_futility_pruning) {
        static_eval = evaluate(b);
    }
    PositionScore futility_margin = 90*depth + 40;
    
    while (true) {
        Move move = move_selector.next_move(b, ss);
        if (move == NULL_MOVE) break;
        else has_moves = true;

        // Futility pruning - if this position is so bad that a quiet
        // move cannot seemingly save it, then we skip the exploring the move
        if (
            can_use_futility_pruning && 
            num_moves > 0 && // Ensure we don't prune when if we haven't explored any moves
            move_selector.phase == QUIET_MOVE
        ) {
            if (static_eval + futility_margin < alpha) continue;
        }

        b.make_move(move);
        num_moves++;

        // Late move reduction setting
        int R = LMR_TABLE[depth][num_moves]; 
        if (is_pv_node) R -= 1;

        // Don't reduce depth if we're in check or searching good moves
        // i.e. no quiet moves or losing captures
        if (in_check) R = 0;
        if (move_selector.phase < QUIET_MOVE) R = 0;

        // Clamp the reduction constant to prevent overflow/underflow
        R = std::clamp(R, 0, depth - 1);

        PositionScore score;
        if (is_first_move) {
            score = -negamax<SM>(b, depth - 1, -beta, -alpha);
            is_first_move = false;
        } else {
            score = -negamax<SM>(b, depth - 1 - R, -alpha - 1, -alpha);

            // Re-search at full depth but reduced window since we beat alpha at the reduced depth
            if (score > alpha && R > 0) {
                score = -negamax<SM>(b, depth - 1, -alpha - 1, -alpha);
            }

            // Re-search at full depth and full window if we still beat alpha
            if (score > alpha && score < beta) {
                score = -negamax<SM>(b, depth - 1, -beta, -alpha);
            }
        }

        b.unmake_move(move);

        // Discard the score and return early if the search has been interrupted
        if (ss.search_interrupted) {
            return SEARCH_INTERRUPTED;
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        // Update lower bound and determine if we need to prune this branch
        if (score > alpha) {
            alpha = score;
        }

        if (move.type() == QUIET) {
            searched_quiet_moves.add(move);
        }

        if (alpha >= beta) {
            if (move.type() == QUIET) {
                searched_quiet_moves.pop();
                update_quiet_heuristic_tables(b, move, depth, searched_quiet_moves);
            }
            break;
        }
    }
    
    if (!has_moves) {
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

    // Determine the type of entry based on the final score
    TTNode tt_node;
    if (best_score >= beta) {
        tt_node = FAIL_HIGH;
    } else if (best_score <= original_alpha) {
        tt_node = FAIL_LOW;
    } else {
        tt_node = EXACT;
    }

    // Normalize score before storing
    PositionScore tt_score = normalize_tt_score(best_score, ss.search_ply(b.ply));

    // Store TT entry
    TT.add_entry(TTEntry{b.zobrist_hash, best_move, depth, tt_score, tt_node});

    return best_score;
}

// Searches all root moves at a given depth and returns the best move
template <SearchMode SM>
static SearchResult search_at_depth(
    Board& b,
    SearchDepth depth,
    Move prev_best_move,
    PositionScore alpha,
    PositionScore beta
) {
    Move best_move;
    PositionScore best_score = DUMMY_SCORE;
    MoveList searched_quiet_moves;
    TTEntry& tt_entry = TT.get_entry(b.zobrist_hash);
    Move tt_move = TT.is_valid_entry(b.zobrist_hash, tt_entry) ? tt_entry.best_move : NULL_MOVE;
    MoveSelector move_selector(b, tt_move, prev_best_move);
    bool is_first_move = true;

    while (true) {
        Move move = move_selector.next_move(b, ss);
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
        if (ss.search_interrupted) {
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

        if (move.type() == QUIET) {
            searched_quiet_moves.add(move);
        }

        // If the move we found is too good and our opponent will not allow it (because
        // they found a better move elsewhere), we can break out of the loop and return
        // early, effectively pruning the branch (aka beta cutoff)
        // In other words, the move we found is worse for the opponent than their current
        // lower bound and so we'll never be allowed to play this move
        if (alpha >= beta) {
            if (move.type() == QUIET) {
                searched_quiet_moves.pop();
                update_quiet_heuristic_tables(b, move, depth, searched_quiet_moves);
            }
            break;
        }
    }

    return {best_move, best_score};
}

// Initializes search globals and performs iterative deepening search
template <SearchMode SM>
Move search(Board& b, const SearchLimits& limits) {
    // Generate moves at root
    // Useful for checking legality of book moves and also returning any legal
    // move at the end if we didn't have the time to find one
    MoveList moves = generate_moves<ALL>(b);
    if (moves.is_empty()) return NULL_MOVE;

    // Return move from opening book if we can (only after validating that it's legal)
    // We have to validate just in case we have a position hash collision in the book
    if (use_own_book) {
        Move book_move = opening_book.pick_move(b);
        for (const Move move: moves) {
            if (book_move == move) {
                return book_move;
            }
        }
    }

    ss.limits = limits;
    ss.nodes = 0;
    ss.search_interrupted = false;
    ss.ply_offset = b.ply;
    ss.killer_1.fill(NULL_MOVE);
    ss.killer_2.fill(NULL_MOVE);
    ss.color_piece_to = {};
    ss.from_to = {};

    // Calculate search deadline based on time limit if search mode is TIME
    if constexpr (SM == TIME) {
        ss.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.time);
    }

    SearchDepth depth = 1;
    Move best_move;
    int prev_score = 0;

    // Iterative search loop
    while (!should_stop_search<SM>()) {
        // Check if we've hit the max depth if search mode is DEPTH
        if constexpr (SM == DEPTH) {
            if (depth > ss.limits.depth) break;
        }

        // Use aspiration windows after the first search
        int alpha, beta;
        int alpha_delta = ASPIRATION_WINDOW, beta_delta = ASPIRATION_WINDOW;
        if (depth == 1) {
            alpha = -CHECKMATE_SCORE;
            beta = CHECKMATE_SCORE;
        } else {
            alpha = std::max(prev_score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            beta = std::min(prev_score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
        }

        SearchResult search_result;
        while (true) {
            if (ss.search_interrupted) break;

            search_result = search_at_depth<SM>(b, depth, best_move, alpha, beta);
            if (search_result.score <= alpha) {
                alpha_delta *= 2;
                alpha = std::max(prev_score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            } else if (search_result.score >= beta) {
                beta_delta *= 2;
                beta = std::min(prev_score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
            } else {
                // Score is within window
                break;
            }
        }

        if (ss.search_interrupted) break;

        prev_score = search_result.score;
        if (search_result.best_move != NULL_MOVE) {
            best_move = search_result.best_move;
        }

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
