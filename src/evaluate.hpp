#pragma once

#include <array>

#include "board.hpp"
#include "types.hpp"

/** Combined piece-value + piece-square lookup, indexed [side][piece][square]. */
using EvalTable = std::array<std::array<std::array<int, NUM_SQUARES>, NUM_PIECES>, NUM_SIDES>;

/** Middlegame (opening) evaluation table. */
extern const EvalTable EARLY_EVAL_TABLE;

/** Endgame evaluation table. */
extern const EvalTable LATE_EVAL_TABLE;

/** Phase contribution of each piece type (used for tapered eval blending). */
constexpr std::array<int, NUM_PIECES> GAME_PHASE_INCREMENT = {0, 1, 1, 2, 4, 0};

/** Total game phase value at the start (both sides combined). */
constexpr int MAX_GAME_PHASE = (
    8 * GAME_PHASE_INCREMENT[PAWN]   +
    2 * GAME_PHASE_INCREMENT[KNIGHT] +
    2 * GAME_PHASE_INCREMENT[BISHOP] +
    2 * GAME_PHASE_INCREMENT[ROOK]   +
    1 * GAME_PHASE_INCREMENT[QUEEN]  +
    1 * GAME_PHASE_INCREMENT[KING]
) * 2;

/** Return a tapered evaluation score from the side-to-move's perspective. */
PositionScore evaluate(const Board& b);
