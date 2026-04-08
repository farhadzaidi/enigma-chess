#include "time_manager.hpp"

#include <algorithm>
#include <cmath>

// Allocator constants
constexpr double MOVES_LEFT_BASE = 10.0;
constexpr double MOVES_LEFT_PHASE_SCALE = 90.0;
constexpr double MIN_MOVES_LEFT_NO_INC = 33.0;
constexpr double INCREMENT_CREDIT = 0.55;
constexpr double SOFT_FACTOR_INC = 0.90;
constexpr double SOFT_FACTOR_NO_INC = 0.85;
constexpr double RESERVE_NO_INC_FRACTION = 0.05;
constexpr double RESERVE_NO_INC_MIN_MS = 5000.0;
constexpr double RESERVE_INC_FRACTION = 0.02;
constexpr double RESERVE_INC_MIN_MS = 2000.0;
constexpr double RESERVE_INC_MULT = 5.0;
constexpr double HARD_MULT = 5.0;
constexpr double EMERGENCY_TRIGGER = 7.0;
constexpr double EMERGENCY_SOFT_DIVISOR = 9.0;
constexpr double EMERGENCY_HARD_DIVISOR = 21.0;

// Guard constants
constexpr int SCORE_DROP_THRESHOLD = 233;
constexpr int BEST_MOVE_MIN_STABILITY = 0;
constexpr int WINNING_SCORE_START = 200;
constexpr int WINNING_SCORE_FULL = 500;
constexpr double WINNING_SCORE_MIN_FACTOR = 0.70;

int TimeManager::allocate_time(
    int remaining,
    int increment,
    int movestogo,
    double phase_ratio,
    bool is_ponder
) {
    enabled_ = true;

    // Hold back a safety reserve so we never flag on time.
    double reserve = 0.0;
    if (increment > 0) {
        // With increment we can afford a smaller reserve since we get time back each move.
        reserve = std::max({
            RESERVE_INC_MIN_MS,
            static_cast<double>(remaining) * RESERVE_INC_FRACTION,
            static_cast<double>(increment) * RESERVE_INC_MULT
        });
    } else {
        // Without increment we need a larger cushion.
        reserve = std::max(
            RESERVE_NO_INC_MIN_MS,
            static_cast<double>(remaining) * RESERVE_NO_INC_FRACTION
        );
    }

    double spendable_remaining = std::max(
        1.0,
        static_cast<double>(remaining) - reserve
    );

    // Estimate how many moves are left in the game.
    double moves = 0.0;
    if (movestogo != -1) {
        // The GUI told us directly.
        moves = std::max(1, movestogo);
    } else {
        // Guess from the game phase — early positions assume many moves
        // remain, endgames assume few.
        moves = MOVES_LEFT_BASE + MOVES_LEFT_PHASE_SCALE * phase_ratio;
        if (increment == 0) {
            // Without increment, be more conservative to avoid running low.
            moves = std::max(MIN_MOVES_LEFT_NO_INC, moves);
        }
    }

    // Divide the spendable clock evenly across estimated remaining moves,
    // then credit a portion of the increment as bonus time per move.
    double base = spendable_remaining / moves + increment * INCREMENT_CREDIT;

    // The soft target is slightly below the base allocation to leave headroom
    // for the search to finish gracefully after a completed depth iteration.
    double soft_base = base * (increment > 0 ? SOFT_FACTOR_INC : SOFT_FACTOR_NO_INC);

    // Hard max is an absolute ceiling the search must never exceed, capped at
    // a multiple of the base or the full spendable clock, whichever is smaller.
    double hard_max = std::min(
        std::max(1.0, static_cast<double>(remaining) - reserve),
        base * HARD_MULT
    );

    // If the clock is critically low, override both limits to tiny fractions
    // of the remaining time so we don't lose on time.
    if (remaining < base * EMERGENCY_TRIGGER) {
        soft_base = remaining / EMERGENCY_SOFT_DIVISOR;
        hard_max = remaining / EMERGENCY_HARD_DIVISOR;
    }

    // When pondering we get extra thinking time since the opponent's clock
    // is running, so we can afford a larger soft target.
    if (is_ponder) {
        soft_base += soft_base / 4;
    }

    soft_base_ = soft_base;
    hard_max_ = hard_max;

    return static_cast<int>(hard_max);
}

void TimeManager::disable() {
    enabled_ = false;
    soft_base_ = 0.0;
    hard_max_ = 0.0;
}

void TimeManager::init_search(int num_legal_moves) {
    num_legal_moves_ = num_legal_moves;
    best_move_stability_ = 0;
    best_move_changes_ = 0;
    prev_score_ = 0;
    has_prev_score_ = false;
}

void TimeManager::on_root_best_move_change() {
    best_move_changes_++;
}

bool TimeManager::should_stop_after_depth(
    int depth,
    int score,
    uint64_t elapsed_ms
) {
    if (!enabled_) {
        return false;
    }

    // Track whether the best move has been consistent across recent depths.
    best_move_stability_ = best_move_changes_ > 0 ? 0 : best_move_stability_ + 1;
    best_move_changes_ = 0;

    // Detect sudden score drops between iterations, which suggest the
    // position is more complicated than we thought.
    bool score_dropped = has_prev_score_ && (prev_score_ - score) > SCORE_DROP_THRESHOLD;
    prev_score_ = score;
    has_prev_score_ = true;

    // Too early or no time limit — skip the stop decision but still update
    // the tracking state above so it's ready for future iterations.
    if (soft_base_ <= 0 || depth <= 1) {
        return false;
    }

    auto elapsed = static_cast<double>(elapsed_ms);
    double soft_target = std::min(soft_base_, hard_max_);
    bool stable = best_move_stability_ > BEST_MOVE_MIN_STABILITY;

    // When we're clearly winning and the best move is stable, reduce the
    // soft target so we move faster.
    if (!score_dropped && stable && score > WINNING_SCORE_START) {
        // The further ahead we are, the more aggressively we cut time.
        double win_ratio = 
            static_cast<double>(score - WINNING_SCORE_START) / (WINNING_SCORE_FULL - WINNING_SCORE_START);
        double win_proximity = std::clamp(win_ratio, 0.0, 1.0);
        double factor = 1.0 - win_proximity * (1.0 - WINNING_SCORE_MIN_FACTOR);
        soft_target *= factor;
    }

    if (elapsed <= soft_target) {
        return false;
    }

    // Only stop early if the position looks safe — best move is stable
    // and the score hasn't suddenly dropped.
    return !score_dropped && stable;
}
