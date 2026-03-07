#pragma once

#include <array>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "core/bitboard.hpp"
#include "precompute/eval.hpp"
#include "eval/pawn_table.hpp"
#include "core/globals.hpp"

namespace {

// constants

constexpr auto EARLY_PASSED_PAWN_BONUS = []() {
    std::array<PositionScore, BOARD_SIZE> bonus = { 0, 0, 3, 8, 15, 25, 40, 0 };
    std::array<std::array<PositionScore, BOARD_SIZE>, NUM_SIDES> early_passed_pawn_bonus = {};
    for (int rank = RANK_1; rank <= RANK_8; rank++) {
        early_passed_pawn_bonus[WHITE][rank] = bonus[rank];
        early_passed_pawn_bonus[BLACK][rank] = bonus[BOARD_SIZE - 1 - rank];
    }

    return early_passed_pawn_bonus;
}();

constexpr auto LATE_PASSED_PAWN_BONUS = []() {
    std::array<PositionScore, BOARD_SIZE> bonus = { 0, 0, 5, 12, 25, 45, 75, 0 };
    std::array<std::array<PositionScore, BOARD_SIZE>, NUM_SIDES> late_passed_pawn_bonus = {};
    for (int rank = RANK_1; rank <= RANK_8; rank++) {
        late_passed_pawn_bonus[WHITE][rank] = bonus[rank];
        late_passed_pawn_bonus[BLACK][rank] = bonus[BOARD_SIZE - 1 - rank];
    }

    return late_passed_pawn_bonus;
}();

constexpr PositionScore EARLY_ISOLATED_PAWN_PENALTY = -10;
constexpr PositionScore LATE_ISOLATED_PAWN_PENALTY  = -12;

constexpr PositionScore EARLY_STACKED_PAWN_PENALTY  =  -8;
constexpr PositionScore LATE_STACKED_PAWN_PENALTY   = -10;

} // namespace


inline PawnTableEntry get_pawn_score(const Board& b) {
    // Check pawn table before computing
    PawnTableEntry pt_entry = g_pawn_table.get_entry(b.pawn_hash);
    if (g_pawn_table.is_valid_entry(b.pawn_hash, pt_entry)) {
        return pt_entry;
    }

    PawnTableEntry new_pt_entry;
    new_pt_entry.hash = b.pawn_hash;

    for (Side side = WHITE; side < NUM_SIDES; side++) {
        Side enemy_side = opposite_side(side);

        PositionScore early_score = 0;
        PositionScore late_score = 0;

        Bitboard friendly_pawns = b.pieces[side][PAWN];
        Bitboard friendly_pawns_copy = friendly_pawns;
        Bitboard enemy_pawns = b.pieces[enemy_side][PAWN];

        while (friendly_pawns_copy) {
            Square sq = pop_lsb(friendly_pawns_copy);
            int file = get_file(sq);
            int rank = get_rank(sq);

            // Passed pawn term
            Bitboard passed_pawn_mask = PASSED_PAWN_MASKS[side][sq];
            bool is_passed_pawn = (passed_pawn_mask & enemy_pawns) == 0;
            if (is_passed_pawn) {
                early_score += EARLY_PASSED_PAWN_BONUS[side][rank];
                late_score += LATE_PASSED_PAWN_BONUS[side][rank];
            }

            // Isolated pawn term
            Bitboard adjacent_file_mask = ADJACENT_FILE_MASKS[sq];
            bool is_isolated_pawn = (adjacent_file_mask & friendly_pawns) == 0;
            if (is_isolated_pawn) {
                early_score += EARLY_ISOLATED_PAWN_PENALTY;
                late_score += LATE_ISOLATED_PAWN_PENALTY;
            }

            // Stacked pawn term
            Bitboard sq_mask = get_mask(sq);
            Bitboard file_mask = FILE_MASKS[file] ^ sq_mask;
            bool is_stacked_pawn = (file_mask & friendly_pawns) != 0;
            if (is_stacked_pawn) {
                early_score += EARLY_STACKED_PAWN_PENALTY;
                late_score += LATE_STACKED_PAWN_PENALTY;
            }
        }

        new_pt_entry.early_pawn_score[side] = early_score;
        new_pt_entry.late_pawn_score[side] = late_score;
    }

    // Store results in pawn table and return
    g_pawn_table.add_entry(new_pt_entry);
    return new_pt_entry;
}
