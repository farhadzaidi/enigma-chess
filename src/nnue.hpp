#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

#include "types.hpp"

constexpr size_t FEATURE_SIZE = 40960;
constexpr size_t L1_SIZE = 256;
constexpr size_t L2_SIZE = 32;
constexpr size_t L3_SIZE = 32;
constexpr int32_t Q1 = 127;
constexpr int32_t Q2 = 64;

/** NNUE evaluation network with incremental accumulator updates */
class NNUE {
public:
    NNUE();

    // --- Feature Updates ---

    /** Add a piece feature to both perspective accumulators */
    void add_feature(const std::array<Square, NUM_SIDES>& king_squares, Side side, Piece piece, Square square);
    /** Remove a piece feature from both perspective accumulators */
    void remove_feature(const std::array<Square, NUM_SIDES>& king_squares, Side side, Piece piece, Square square);
    /** Recompute both accumulators from scratch from the given board state */
    void refresh_features(
        const std::array<Square, NUM_SIDES>& king_squares,
        const std::array<std::array<Bitboard, NUM_PIECES>, NUM_SIDES>& pieces
    );

    // --- Stack Operations ---

    /** Save current accumulators onto the stack before making a move */
    void push();
    /** Restore accumulators from the stack after unmaking a move */
    void pop();
    /** Drop saved accumulator history while keeping the current accumulators. */
    void clear_history();

    // --- Evaluation --

    PositionScore evaluate(Side us);

private:
    using Accumulator = std::array<int16_t, L1_SIZE>;
    using Accumulators = std::array<Accumulator, NUM_SIDES>;

    alignas(64) Accumulators accumulators;
    std::vector<Accumulators> accumulator_stack;

    /** Reset both accumulators to the layer 1 bias values */
    void reset_accumulators();
};
