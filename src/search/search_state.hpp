#pragma once

#include <chrono>
#include <cstdint>

#include "core/types.hpp"
#include "core/move.hpp"

struct SearchLimits {
    int soft_time;
    int hard_time;
    uint64_t nodes;
    SearchDepth depth;
};

struct SearchState {
    // --- Board-local type aliases ---
    using SidePieceToHistory = std::array<std::array<std::array<MoveScore, NUM_SQUARES>, NUM_PIECES>, NUM_SIDES>;
    using FromToHistory = std::array<std::array<MoveScore, NUM_SQUARES>, NUM_SQUARES>;

    SearchLimits limits;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point soft_deadline;
    uint64_t nodes;
    int ply_offset;
    bool search_interrupted;

    // Killer moves
    KillerMoves killer_1;
    KillerMoves killer_2;

    // History heuristic tables (for quiet moves)
    SidePieceToHistory side_piece_to_history;
    FromToHistory from_to_history;

    int search_ply(int board_ply) const {
        return board_ply - ply_offset;
    }
};
