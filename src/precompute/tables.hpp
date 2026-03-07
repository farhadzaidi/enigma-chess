#pragma once

#include <cmath>
#include <array>

#include "core/types.hpp"
#include "core/constants.hpp"

namespace {

// constants
constexpr int LMR_MAX_MOVES = 128;
constexpr double LMR_TUNING_CONSTANT = 2.0;

} // namespace


// --- Castling Rights Updates ---
// Lookup table for which castling rights are lost when a piece moves from/to a square.

constexpr auto castling_rights_updates = []() {
    std::array<CastlingRights, NUM_SQUARES> castling_rights_updates = {NO_CASTLING_RIGHTS};
    castling_rights_updates[E1] = WHITE_SHORT | WHITE_LONG;
    castling_rights_updates[H1] = WHITE_SHORT;
    castling_rights_updates[A1] = WHITE_LONG;
    castling_rights_updates[E8] = BLACK_SHORT | BLACK_LONG;
    castling_rights_updates[H8] = BLACK_SHORT;
    castling_rights_updates[A8] = BLACK_LONG;
    return castling_rights_updates;
}();

// --- LMR Table ---
// R(depth, move_index) ~ floor(ln(depth + 1) * ln(move_index + 1) / tuning_constant)

inline const auto LMR_TABLE = []() {
    std::array<std::array<int, LMR_MAX_MOVES>, MAX_SEARCH_PLY> table{};
    for (int depth = 0; depth < MAX_SEARCH_PLY; depth++) {
        for (int move_index = 0; move_index < LMR_MAX_MOVES; move_index++) {
            table[depth][move_index] = std::log(depth + 1) * std::log(move_index + 1) / LMR_TUNING_CONSTANT;
        }
    }
    return table;
}();
