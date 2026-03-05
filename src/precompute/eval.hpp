#pragma once

#include <array>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "core/bitboard.hpp"

constexpr auto ADJACENT_FILE_MASKS = []() {
    std::array<Bitboard, NUM_SQUARES> adjacent_file_masks = {};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int file = get_file(sq);
        // Left/right file masks from white's perspective (perspective doesn't matter
        // though since we compute both)
        Bitboard left_file = file - 1 >= A_FILE ? FILE_MASKS[file - 1] : EMPTY_BITBOARD;
        Bitboard right_file = file + 1 <= H_FILE ? FILE_MASKS[file + 1] : EMPTY_BITBOARD;
        adjacent_file_masks[sq] = left_file | right_file;
    }

    return adjacent_file_masks;
}();

// Includes the indexed square's file as well
constexpr auto ADJACENT_FILE_MASKS_INCLUSIVE = []() {
    std::array<Bitboard, NUM_SQUARES> adjacent_file_masks_inclusive = {};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int file = get_file(sq);
        adjacent_file_masks_inclusive[sq] = ADJACENT_FILE_MASKS[sq] | FILE_MASKS[file];
    }

    return adjacent_file_masks_inclusive;
}();

constexpr auto PASSED_PAWN_MASKS = []() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SIDES> passed_pawn_masks = {};

    // White pawns
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int rank = get_rank(sq);
        Bitboard ranks_in_front = EMPTY_BITBOARD;
        for (int front_rank = rank + 1; front_rank < BOARD_SIZE; front_rank++) {
            ranks_in_front |= RANK_MASKS[front_rank];
        }

        passed_pawn_masks[WHITE][sq] = ranks_in_front & ADJACENT_FILE_MASKS_INCLUSIVE[sq];
    }

    // Black pawns
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int rank = get_rank(sq);
        Bitboard ranks_in_front = EMPTY_BITBOARD; // From black's perspective
        for (int front_rank = rank - 1; front_rank >= RANK_1; front_rank--) {
            ranks_in_front |= RANK_MASKS[front_rank];
        }

        passed_pawn_masks[BLACK][sq] = ranks_in_front & ADJACENT_FILE_MASKS_INCLUSIVE[sq];
    }

    return passed_pawn_masks;
}();

constexpr auto KING_SHIELD_MASKS = []() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SIDES> king_shield_masks = {};

    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int rank = get_rank(sq);
        Bitboard white_rank_in_front = rank < RANK_8 ? RANK_MASKS[rank + 1] : 0;
        Bitboard white_shield_mask = ADJACENT_FILE_MASKS_INCLUSIVE[sq] & white_rank_in_front;
        king_shield_masks[WHITE][sq] = white_shield_mask;

        Bitboard black_rank_in_front = rank > RANK_1 ? RANK_MASKS[rank - 1] : 0;
        Bitboard black_shield_mask = ADJACENT_FILE_MASKS_INCLUSIVE[sq] & black_rank_in_front;
        king_shield_masks[BLACK][sq] = black_shield_mask;
    }

    return king_shield_masks;
}();
