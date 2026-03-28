#pragma once

#include "types.hpp"

struct EngineParams {
    // --- Search ---

    int aspiration_window = 67;
    int score_drop_threshold = 180;

    int null_move_base_reduction = 4;
    int null_move_deeper_threshold = 8;
    int null_move_min_depth = 2;

    int futility_margin_per_depth = 10;
    int futility_margin_base = 77;
    int futility_max_depth = 4;

    int see_cutoff = -113;

    double lmr_tuning_constant = 1.317;

    int minimum_iid_depth = 6;

    // --- Time Management ---

    int moves_left_base = 11;
    int moves_left_phase_scale = 78;
    int min_moves_no_increment = 35;
    double increment_fraction = 0.918;
    double soft_factor_no_increment = 0.933;
    double soft_factor_increment = 0.844;
    double hard_factor = 4.510;
    int hard_cap_divisor = 2;
    int emergency_trigger = 8;
    int emergency_soft_divisor = 11;
    int emergency_hard_divisor = 22;

    // --- Previous defaults (pre-tuning) ---
    // int aspiration_window = 25;
    // int score_drop_threshold = 50;
    // int null_move_base_reduction = 2;
    // int null_move_deeper_threshold = 6;
    // int null_move_min_depth = 3;
    // int futility_margin_per_depth = 90;
    // int futility_margin_base = 40;
    // int futility_max_depth = 4;
    // int see_cutoff = -200;
    // double lmr_tuning_constant = 2.0;
    // int minimum_iid_depth = 4;
    // int moves_left_base = 10;
    // int moves_left_phase_scale = 30;
    // int min_moves_no_increment = 45;
    // double increment_fraction = 0.5;
    // double soft_factor_no_increment = 0.5;
    // double soft_factor_increment = 0.6;
    // double hard_factor = 2.625;
    // int hard_cap_divisor = 3;
    // int emergency_trigger = 4;
    // int emergency_soft_divisor = 15;
    // int emergency_hard_divisor = 8;

};

inline EngineParams g_params;
