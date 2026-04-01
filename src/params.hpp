#pragma once

#include "data/params.hpp"
#include "types.hpp"

struct EngineParams {
    // Search
    int aspiration_window = DEFAULT_ASPIRATION_WINDOW;
    int score_drop_threshold = DEFAULT_SCORE_DROP_THRESHOLD;

    int null_move_base_reduction = DEFAULT_NULL_MOVE_BASE_REDUCTION;
    int null_move_deeper_threshold = DEFAULT_NULL_MOVE_DEEPER_THRESHOLD;
    int null_move_min_depth = DEFAULT_NULL_MOVE_MIN_DEPTH;

    int reverse_futility_margin_per_depth = DEFAULT_REVERSE_FUTILITY_MARGIN_PER_DEPTH;
    int reverse_futility_margin_base = DEFAULT_REVERSE_FUTILITY_MARGIN_BASE;
    int reverse_futility_max_depth = DEFAULT_REVERSE_FUTILITY_MAX_DEPTH;

    int futility_margin_per_depth = DEFAULT_FUTILITY_MARGIN_PER_DEPTH;
    int futility_margin_base = DEFAULT_FUTILITY_MARGIN_BASE;
    int futility_max_depth = DEFAULT_FUTILITY_MAX_DEPTH;

    int lmp_base = DEFAULT_LMP_BASE;
    int lmp_max_depth = DEFAULT_LMP_MAX_DEPTH;

    int razoring_margin = DEFAULT_RAZORING_MARGIN;
    int razoring_max_depth = DEFAULT_RAZORING_MAX_DEPTH;

    int see_pruning_max_depth = DEFAULT_SEE_PRUNING_MAX_DEPTH;

    int reduced_search_min_depth = DEFAULT_REDUCED_SEARCH_MIN_DEPTH;
    int reduced_search_depth_divisor = DEFAULT_REDUCED_SEARCH_DEPTH_DIVISOR;
    int reduced_search_margin_multiplier = DEFAULT_REDUCED_SEARCH_MARGIN_MULTIPLIER;
    int reduced_search_tt_depth_margin = DEFAULT_REDUCED_SEARCH_TT_DEPTH_MARGIN;

    int see_cutoff = DEFAULT_SEE_CUTOFF;

    double lmr_tuning_constant = DEFAULT_LMR_TUNING_CONSTANT;

    int minimum_iid_depth = DEFAULT_MINIMUM_IID_DEPTH;
    int iid_depth_divisor = DEFAULT_IID_DEPTH_DIVISOR;

    int lmr_pv_reduction = DEFAULT_LMR_PV_REDUCTION;
    int best_move_min_stability = DEFAULT_BEST_MOVE_MIN_STABILITY;
    int null_move_deep_reduction = DEFAULT_NULL_MOVE_DEEP_REDUCTION;
    int history_malus_divisor = DEFAULT_HISTORY_MALUS_DIVISOR;

    // Time management
    int moves_left_base = DEFAULT_MOVES_LEFT_BASE;
    int moves_left_phase_scale = DEFAULT_MOVES_LEFT_PHASE_SCALE;
    int min_moves_no_increment = DEFAULT_MIN_MOVES_NO_INCREMENT;
    double increment_fraction = DEFAULT_INCREMENT_FRACTION;
    double soft_factor_no_increment = DEFAULT_SOFT_FACTOR_NO_INCREMENT;
    double soft_factor_increment = DEFAULT_SOFT_FACTOR_INCREMENT;
    double hard_factor = DEFAULT_HARD_FACTOR;
    int hard_cap_divisor = DEFAULT_HARD_CAP_DIVISOR;
    int emergency_trigger = DEFAULT_EMERGENCY_TRIGGER;
    int emergency_soft_divisor = DEFAULT_EMERGENCY_SOFT_DIVISOR;
    int emergency_hard_divisor = DEFAULT_EMERGENCY_HARD_DIVISOR;
};

inline EngineParams prm;
