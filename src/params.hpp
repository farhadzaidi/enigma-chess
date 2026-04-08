#pragma once

#include "data/params.hpp"
#include "types.hpp"

struct Params {
    // Search
    int aspiration_window = DEFAULT_ASPIRATION_WINDOW;

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
    int null_move_deep_reduction = DEFAULT_NULL_MOVE_DEEP_REDUCTION;
    int history_malus_divisor = DEFAULT_HISTORY_MALUS_DIVISOR;
};

inline Params prm;
