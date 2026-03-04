#pragma once

#include <algorithm>
#include <bit>

#include "core/types.hpp"
#include "board/board.hpp"
#include "move_generator/attacks.hpp"
#include "core/bitboard.hpp"

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

struct MobilityScore {
    PositionScore early = 0;
    PositionScore late = 0;
};

inline MobilityScore compute_color_mobility(
    const Board& b,
    Color color,
    Bitboard occupied
) {
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
}

inline MobilityScore get_mobility_score(const Board& b) {
    Color us = b.to_move;
    Color them = opposite_color(us);
    Bitboard occupied = b.occupied;

    MobilityScore ours = compute_color_mobility(b, us, occupied);
    MobilityScore theirs = compute_color_mobility(b, them, occupied);

    return {
        static_cast<PositionScore>(ours.early - theirs.early),
        static_cast<PositionScore>(ours.late - theirs.late)
    };
}
