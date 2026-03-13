#pragma once

#include <algorithm>
#include <bit>

#include "eval/constants.hpp"
#include "eval/pawn_eval.hpp"
#include "eval/mobility.hpp"
#include "precompute/eval.hpp"
#include "search/context.hpp"

inline void clear_eval_cache() {
    g_shared.pawn_table.clear();
}

inline PawnTableEntry get_pawn_score(const Board& b) {
    return get_pawn_score(b, g_shared.pawn_table);
}

inline PositionScore evaluate(const Board& b) {
    Side friendly_side = b.to_move;
    Side enemy_side = opposite_side(friendly_side);

    // Pawn structure
    PawnTableEntry pt_entry = get_pawn_score(b);
    PositionScore early_pawn_score = pt_entry.early_pawn_score[friendly_side] - pt_entry.early_pawn_score[enemy_side];
    PositionScore late_pawn_score = pt_entry.late_pawn_score[friendly_side] - pt_entry.late_pawn_score[enemy_side];

    // Bishop pair
    PositionScore early_bishop_pair_score = 0;
    PositionScore late_bishop_pair_score = 0;

    if (std::popcount(b.pieces[friendly_side][BISHOP]) >= 2) {
        early_bishop_pair_score += EARLY_BISHOP_PAIR_BONUS;
        late_bishop_pair_score += LATE_BISHOP_PAIR_BONUS;
    }

    if (std::popcount(b.pieces[enemy_side][BISHOP]) >= 2) {
        early_bishop_pair_score -= EARLY_BISHOP_PAIR_BONUS;
        late_bishop_pair_score -= LATE_BISHOP_PAIR_BONUS;
    }

    // Mobility
    MobilityScore mobility = get_mobility_score(b);

    // King safety - pawn shield (early game only)
    int friendly_shield = std::popcount(KING_SHIELD_MASKS[friendly_side][b.king_squares[friendly_side]] & b.pieces[friendly_side][PAWN]);
    int enemy_shield = std::popcount(KING_SHIELD_MASKS[enemy_side][b.king_squares[enemy_side]] & b.pieces[enemy_side][PAWN]);
    PositionScore king_safety_score = KING_SHIELD_PENALTY[friendly_shield] - KING_SHIELD_PENALTY[enemy_shield];

    PositionScore net_early_score = (
        b.early_score[friendly_side] - b.early_score[enemy_side] +
        early_pawn_score +
        early_bishop_pair_score +
        king_safety_score +
        mobility.early
    );

    PositionScore net_late_score = (
        b.late_score[friendly_side] - b.late_score[enemy_side] +
        late_pawn_score +
        late_bishop_pair_score +
        mobility.late
    );

    // Clamp early game phase multiplier in case of early promotion
    int early_multiplier = std::min(b.game_phase, MAX_GAME_PHASE);
    int late_multiplier = MAX_GAME_PHASE - early_multiplier;

    return (net_early_score * early_multiplier + net_late_score * late_multiplier) / MAX_GAME_PHASE;
}
