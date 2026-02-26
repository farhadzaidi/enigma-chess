#include <algorithm>

#include "types.hpp"
#include "evaluate.hpp"

PositionScore evaluate(Board& b) {
    Color us = b.to_move;
    Color them = us ^ 1;

    PositionScore net_early_score = b.early_score[us] - b.early_score[them];
    PositionScore net_late_score = b.late_score[us] - b.late_score[them];

    // Clamp early game phase multiplier in case of early promotion
    int early_multiplier = std::min(b.game_phase, MAX_GAME_PHASE);
    int late_multiplier = MAX_GAME_PHASE - early_multiplier;

    return (net_early_score * early_multiplier + net_late_score * late_multiplier) / MAX_GAME_PHASE;
}
