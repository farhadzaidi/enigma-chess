#include "types.hpp"
#include "evaluate.hpp"

PositionScore evaluate(Board& b) {
    Color us = b.to_move;
    Color them = us ^ 1;

    return b.material[us] - b.material[them];
}
