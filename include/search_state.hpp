#pragma once

#include <chrono>
#include <cstdint>

#include "types.hpp"
#include "move.hpp"

struct SearchLimits {
    int time;
    uint64_t nodes;
    SearchDepth depth;
};

struct SearchState {
    SearchLimits limits;
    std::chrono::steady_clock::time_point deadline;
    uint64_t nodes;
    int ply_offset;
    bool search_interrupted;

    // Killer moves
    KillerMoves killer_1;
    KillerMoves killer_2;

    // History heuristic tables (for quiet moves)
    ColorPieceToHistory color_piece_to;
    FromToHistory from_to;

    int search_ply(int board_ply) const {
        return board_ply - ply_offset;
    }
};
