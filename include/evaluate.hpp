#pragma once

#include <algorithm>
#include <bit>

#include "board.hpp"
#include "move_generator.hpp"
#include "precompute.hpp"
#include "types.hpp"
#include "utils.hpp"
#include "pawn_table.hpp"

// --- Pawn Bonuses ---

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

// --- Bishop Bonuses ---

constexpr PositionScore EARLY_BISHOP_PAIR_BONUS     = 20;
constexpr PositionScore LATE_BISHOP_PAIR_BONUS      = 40;

// --- Mobility ---

constexpr size_t MAX_KNIGHT_MOBILITY = 9;
constexpr size_t MAX_BISHOP_MOBILITY = 15;
constexpr size_t MAX_ROOK_MOBILITY   = 16;
constexpr size_t MAX_QUEEN_MOBILITY  = 29;

constexpr std::array<PositionScore, MAX_KNIGHT_MOBILITY> KNIGHT_MOBILITY_EARLY = {
    -25, -15,  -5,   0,   5,  10,  14,  16,  18
};
constexpr std::array<PositionScore, MAX_KNIGHT_MOBILITY> KNIGHT_MOBILITY_LATE = {
    -30, -18,  -6,   0,   6,  12,  16,  18,  20
};

constexpr std::array<PositionScore, MAX_BISHOP_MOBILITY> BISHOP_MOBILITY_EARLY = {
    -20, -15, -10,  -5,   0,   5,   8,  10,  12,  13,  14,  14,  15,  15,  15
};
constexpr std::array<PositionScore, MAX_BISHOP_MOBILITY> BISHOP_MOBILITY_LATE = {
    -25, -18, -12,  -6,   0,   6,  10,  13,  15,  16,  17,  17,  18,  18,  18
};

constexpr std::array<PositionScore, MAX_ROOK_MOBILITY> ROOK_MOBILITY_EARLY = {
    -20, -15, -10,  -5,  -2,   0,   3,   5,   7,   8,   9,  10,  10,  10,  10,  10
};
constexpr std::array<PositionScore, MAX_ROOK_MOBILITY> ROOK_MOBILITY_LATE = {
    -30, -20, -12,  -6,  -2,   0,   5,   8,  11,  13,  14,  15,  16,  16,  16,  16
};

constexpr std::array<PositionScore, MAX_QUEEN_MOBILITY> QUEEN_MOBILITY_EARLY = {
    -15, -10,  -8,  -5,  -3,  -1,   0,   1,   2,   3,   4,   5,   5,   6,   6,
      6,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7
};
constexpr std::array<PositionScore, MAX_QUEEN_MOBILITY> QUEEN_MOBILITY_LATE = {
    -20, -14, -10,  -6,  -3,  -1,   0,   2,   4,   6,   7,   8,   9,  10,  10,
     11,  11,  11,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12
};

// --- King Safety ---

// Penalty indexed by number of shield pawns
constexpr std::array<PositionScore, 4> KING_SHIELD_PENALTY = {-45, -25, -10, 0};


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

struct MobilityScore {
    PositionScore early = 0;
    PositionScore late = 0;
};

inline MobilityScore get_mobility_score(Board& b) {
    Color us = b.to_move;
    Color them = us ^ 1;
    Bitboard occupied = b.occupied;

    auto compute = [&](Color color) -> MobilityScore {
        MobilityScore score;
        Bitboard friendly = b.colors[color];

        Bitboard knights = b.pieces[color][KNIGHT];
        while (knights) {
            Square sq = pop_lsb(knights);
            int moves = std::popcount(KNIGHT_ATTACK_MAP[sq] & ~friendly);
            score.early += KNIGHT_MOBILITY_EARLY[moves];
            score.late  += KNIGHT_MOBILITY_LATE[moves];
        }

        Bitboard bishops = b.pieces[color][BISHOP];
        while (bishops) {
            Square sq = pop_lsb(bishops);
            int moves = std::popcount(generate_sliding_attack_mask<BISHOP>(sq, occupied) & ~friendly);
            score.early += BISHOP_MOBILITY_EARLY[moves];
            score.late  += BISHOP_MOBILITY_LATE[moves];
        }

        Bitboard rooks = b.pieces[color][ROOK];
        while (rooks) {
            Square sq = pop_lsb(rooks);
            int moves = std::popcount(generate_sliding_attack_mask<ROOK>(sq, occupied) & ~friendly);
            score.early += ROOK_MOBILITY_EARLY[moves];
            score.late  += ROOK_MOBILITY_LATE[moves];
        }

        Bitboard queens = b.pieces[color][QUEEN];
        while (queens) {
            Square sq = pop_lsb(queens);
            int moves = std::popcount(
                (generate_sliding_attack_mask<BISHOP>(sq, occupied) |
                 generate_sliding_attack_mask<ROOK>(sq, occupied)) & ~friendly
            );
            score.early += QUEEN_MOBILITY_EARLY[moves];
            score.late  += QUEEN_MOBILITY_LATE[moves];
        }

        return score;
    };

    MobilityScore ours = compute(us);
    MobilityScore theirs = compute(them);

    return {
        static_cast<PositionScore>(ours.early - theirs.early),
        static_cast<PositionScore>(ours.late - theirs.late)
    };
}

inline PositionScore evaluate(Board& b) {
    Color us = b.to_move;
    Color them = us ^ 1;

    // Pawn structure
    PawnTableEntry pt_entry = get_pawn_score(b);
    PositionScore early_pawn_score = pt_entry.early_pawn_score[us] - pt_entry.early_pawn_score[them];
    PositionScore late_pawn_score = pt_entry.late_pawn_score[us] - pt_entry.late_pawn_score[them];

    // Bishop pair
    PositionScore early_bishop_pair_score = 0;
    PositionScore late_bishop_pair_score = 0;

    if (std::popcount(b.pieces[us][BISHOP]) >= 2) {
        early_bishop_pair_score += EARLY_BISHOP_PAIR_BONUS;
        late_bishop_pair_score += LATE_BISHOP_PAIR_BONUS;
    }

    if (std::popcount(b.pieces[them][BISHOP]) >= 2) {
        early_bishop_pair_score -= EARLY_BISHOP_PAIR_BONUS;
        late_bishop_pair_score -= LATE_BISHOP_PAIR_BONUS;
    }

    // Mobility
    MobilityScore mobility = get_mobility_score(b);

    // King safety - pawn shield (early game only)
    int our_shield = std::popcount(KING_SHIELD_MASKS[us][b.king_squares[us]] & b.pieces[us][PAWN]);
    int their_shield = std::popcount(KING_SHIELD_MASKS[them][b.king_squares[them]] & b.pieces[them][PAWN]);
    PositionScore king_safety_score = KING_SHIELD_PENALTY[our_shield] - KING_SHIELD_PENALTY[their_shield];

    PositionScore net_early_score = (
        b.early_score[us] - b.early_score[them] +
        early_pawn_score +
        early_bishop_pair_score +
        king_safety_score +
        mobility.early
    );

    PositionScore net_late_score = (
        b.late_score[us] - b.late_score[them] +
        late_pawn_score +
        late_bishop_pair_score +
        mobility.late
    );

    // Clamp early game phase multiplier in case of early promotion
    int early_multiplier = std::min(b.game_phase, MAX_GAME_PHASE);
    int late_multiplier = MAX_GAME_PHASE - early_multiplier;

    return (net_early_score * early_multiplier + net_late_score * late_multiplier) / MAX_GAME_PHASE;
}
