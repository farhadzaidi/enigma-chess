#pragma once

#include <algorithm>

#include "board.hpp"
#include "precompute.hpp"
#include "types.hpp"
#include "utils.hpp"
#include "pawn_table.hpp"

// --- Pawn Terms ---

constexpr auto EARLY_PASSED_PAWN_BONUS = []() {
    std::array<PositionScore, BOARD_SIZE> bonus = { 0, 0, 3, 8, 15, 25, 40, 0 };
    std::array<std::array<PositionScore, BOARD_SIZE>, NUM_COLORS> early_passed_pawn_bonus = {};
    for (int r = RANK_1; r <= RANK_8; r++) {
        early_passed_pawn_bonus[WHITE][r] = bonus[r];
        early_passed_pawn_bonus[BLACK][r] = bonus[BOARD_SIZE - 1 - r];
    }

    return early_passed_pawn_bonus;
}();

constexpr auto LATE_PASSED_PAWN_BONUS = []() {
    std::array<PositionScore, BOARD_SIZE> bonus = { 0, 0, 5, 12, 25, 45, 75, 0 };
    std::array<std::array<PositionScore, BOARD_SIZE>, NUM_COLORS> late_passed_pawn_bonus = {};
    for (int r = RANK_1; r <= RANK_8; r++) {
        late_passed_pawn_bonus[WHITE][r] = bonus[r];
        late_passed_pawn_bonus[BLACK][r] = bonus[BOARD_SIZE - 1 - r];
    }

    return late_passed_pawn_bonus;
}();

constexpr PositionScore EARLY_ISOLATED_PAWN_PENALTY = -10;
constexpr PositionScore LATE_ISOLATED_PAWN_PENALTY  = -12;

constexpr PositionScore EARLY_STACKED_PAWN_PENALTY  =  -8;
constexpr PositionScore LATE_STACKED_PAWN_PENALTY   = -10;

inline PawnTableEntry get_pawn_score(Board& b) {
    // Check pawn table before computing
    PawnTableEntry pt_entry = PT.get_entry(b.pawn_hash);
    if (PT.is_valid_entry(b.pawn_hash, pt_entry)) {
        return pt_entry;
    }

    PawnTableEntry new_pt_entry;
    new_pt_entry.hash = b.pawn_hash;

    for (Color c = WHITE; c < NUM_COLORS; c++) {
        Color us = c;
        Color them = c ^ 1;

        PositionScore early_score = 0;
        PositionScore late_score = 0;

        Bitboard our_pawns = b.pieces[us][PAWN];
        Bitboard our_pawns_copy = our_pawns;
        Bitboard their_pawns = b.pieces[them][PAWN];

        while (our_pawns_copy) {
            Square sq = pop_lsb(our_pawns_copy);
            int file = get_file(sq);
            int rank = get_rank(sq);

            // Passed pawn term
            Bitboard passed_pawn_mask = PASSED_PAWN_MASKS[us][sq];
            bool is_passed_pawn = (passed_pawn_mask & their_pawns) == 0;
            if (is_passed_pawn) {
                early_score += EARLY_PASSED_PAWN_BONUS[us][rank];
                late_score += LATE_PASSED_PAWN_BONUS[us][rank];
            }

            // Isolated pawn term
            Bitboard adjacent_file_mask = ADJACENT_FILE_MASKS[sq];
            bool is_isolated_pawn = (adjacent_file_mask & our_pawns) == 0;
            if (is_isolated_pawn) {
                early_score += EARLY_ISOLATED_PAWN_PENALTY;
                late_score += LATE_ISOLATED_PAWN_PENALTY;
            }

            // Stacked pawn term
            Bitboard sq_mask = get_mask(sq);
            Bitboard file_mask = FILE_MASKS[file] ^ sq_mask;
            bool is_stacked_pawn = (file_mask & our_pawns) != 0;
            if (is_stacked_pawn) {
                early_score += EARLY_STACKED_PAWN_PENALTY;
                late_score += LATE_STACKED_PAWN_PENALTY;
            }
        }

        new_pt_entry.early_pawn_score[us] = early_score;
        new_pt_entry.late_pawn_score[us] = late_score;
    }

    // Store results in pawn table and return
    PT.add_entry(new_pt_entry);
    return new_pt_entry;
}

inline PositionScore evaluate(Board& b) {
    Color us = b.to_move;
    Color them = us ^ 1;

    PawnTableEntry pt_entry = get_pawn_score(b);
    PositionScore early_pawn_score = pt_entry.early_pawn_score[us] - pt_entry.early_pawn_score[them];
    PositionScore late_pawn_score = pt_entry.late_pawn_score[us] - pt_entry.late_pawn_score[them];

    PositionScore net_early_score = b.early_score[us] - b.early_score[them] + early_pawn_score;
    PositionScore net_late_score = b.late_score[us] - b.late_score[them] + late_pawn_score;

    // Clamp early game phase multiplier in case of early promotion
    int early_multiplier = std::min(b.game_phase, MAX_GAME_PHASE);
    int late_multiplier = MAX_GAME_PHASE - early_multiplier;

    return (net_early_score * early_multiplier + net_late_score * late_multiplier) / MAX_GAME_PHASE;
}
