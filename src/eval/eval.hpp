#pragma once

#include <algorithm>
#include <bit>

#include "eval/constants.hpp"
#include "eval/pawn_eval.hpp"
#include "eval/mobility.hpp"
#include "precompute/eval.hpp"

inline PositionScore evaluate(const Board& b) {
    Color us = b.to_move;
    Color them = opposite_color(us);

    // Pawn structure
    PawnTableEntry pt_entry = get_pawn_score(b);
    PositionScore early_pawn_score = pt_entry.early_pawn_score[us] - pt_entry.early_pawn_score[them];
    PositionScore late_pawn_score = pt_entry.late_pawn_score[us] - pt_entry.late_pawn_score[them];

    // Bishop pair
    PositionScore early_bishop_pair_score = 0;
    PositionScore late_bishop_pair_score = 0;

    if (std::popcount(b.pieces[us][BISHOP]) >= 2) {
        early_bishop_pair_score += EARLY_BISHOP_PAIR_BONUS;
        late_bishop_pair_score += LATE_BISHOP_PAIR_BONUS;
    }

    if (std::popcount(b.pieces[them][BISHOP]) >= 2) {
        early_bishop_pair_score -= EARLY_BISHOP_PAIR_BONUS;
        late_bishop_pair_score -= LATE_BISHOP_PAIR_BONUS;
    }

    // Mobility
    MobilityScore mobility = get_mobility_score(b);

    // King safety - pawn shield (early game only)
    int our_shield = std::popcount(KING_SHIELD_MASKS[us][b.king_squares[us]] & b.pieces[us][PAWN]);
    int their_shield = std::popcount(KING_SHIELD_MASKS[them][b.king_squares[them]] & b.pieces[them][PAWN]);
    PositionScore king_safety_score = KING_SHIELD_PENALTY[our_shield] - KING_SHIELD_PENALTY[their_shield];

    PositionScore net_early_score = (
        b.early_score[us] - b.early_score[them] +
        early_pawn_score +
        early_bishop_pair_score +
        king_safety_score +
        mobility.early
    );

    PositionScore net_late_score = (
        b.late_score[us] - b.late_score[them] +
        late_pawn_score +
        late_bishop_pair_score +
        mobility.late
    );

    // Clamp early game phase multiplier in case of early promotion
    int early_multiplier = std::min(b.game_phase, MAX_GAME_PHASE);
    int late_multiplier = MAX_GAME_PHASE - early_multiplier;

    return (net_early_score * early_multiplier + net_late_score * late_multiplier) / MAX_GAME_PHASE;
}
