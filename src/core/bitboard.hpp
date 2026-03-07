#pragma once

#include <bit>
#include <string>

#ifdef __BMI2__
#include <immintrin.h>
#endif

#include "core/types.hpp"
#include "core/constants.hpp"

namespace {

// constants
constexpr Bitboard NOT_A_FILE = ~FILE_MASKS[A_FILE];
constexpr Bitboard NOT_H_FILE = ~FILE_MASKS[H_FILE];

// functions
inline Square pop_msb(Bitboard& b) {
    Square sq = 63 - std::countl_zero(b);
    b &= ~(1ULL << sq);
    return sq;
}

} // namespace


constexpr Bitboard EMPTY_BITBOARD = 0;

// --- Bitboard Shift ---

template <Direction D>
constexpr Bitboard shift(Bitboard b) {
    switch(D) {
        case NORTH:         return b << 8;
        case SOUTH:         return b >> 8;
        case NORTH_NORTH:   return b << 16;
        case SOUTH_SOUTH:   return b >> 16;
        case EAST:          return (b << 1) & NOT_A_FILE;
        case WEST:          return (b >> 1) & NOT_H_FILE;
        case NORTHEAST:     return (b << 9) & NOT_A_FILE;
        case NORTHWEST:     return (b << 7) & NOT_H_FILE;
        case SOUTHEAST:     return (b >> 7) & NOT_A_FILE;
        case SOUTHWEST:     return (b >> 9) & NOT_H_FILE;
    }
}

// --- Bitboard Manipulation ---

constexpr Bitboard get_mask(Square square) { return uint64_t{1} << square;}

inline Square pop_lsb(Bitboard& b) {
    Square sq = std::countr_zero(b);
    b &= b - 1; // pop
    return sq;
}

constexpr Square get_lsb(Bitboard b) {
    return std::countr_zero(b);
}

template <Direction D>
constexpr Square pop_next(Bitboard& b) {
    constexpr bool use_pop_lsb = D == NORTH || D == EAST || D == NORTHEAST || D == NORTHWEST;
    if constexpr (use_pop_lsb) return pop_lsb(b);
    else                       return pop_msb(b);
}

// --- Side & Square Utilities ---

constexpr Side opposite_side(Side s) { return s ^ 1; }

// --- Square Utilities ---

constexpr Square get_square(int rank, int file) { return rank * BOARD_SIZE + file; }
constexpr int get_rank(Square square) { return square / BOARD_SIZE; }
constexpr int get_file(Square square) { return square % BOARD_SIZE; }

// --- Piece Classification ---

// Determines if a given piece is the relevant sliding piece based on the direction.
// For example, it would return true if we find a rook or queen while going in straight directions.
template <Direction D>
inline bool is_relevant_sliding_piece(Piece piece) {
    if constexpr (D == NORTH || D == SOUTH || D == EAST || D == WEST) {
        return piece == ROOK || piece == QUEEN;
    }

    else if constexpr (D == NORTHEAST || D == NORTHWEST || D == SOUTHEAST || D == SOUTHWEST) {
        return piece == BISHOP || piece == QUEEN;
    }

    else return false;
}

constexpr bool is_slider(Piece p) { return (p == BISHOP || p == ROOK || p == QUEEN); }

// --- Square Notation ---

inline Square notation_to_square(const std::string& s) {
    return get_square(s[1] - '1', s[0] - 'a');
}

// --- Attack Table Indexing ---

inline uint64_t get_attack_table_index(Bitboard subset, Bitboard blocker_mask, uint64_t magic_number) {
// Use PEXT if the compiler flag is set and the CPU supports it
#if defined(__BMI2__)
    if (__builtin_cpu_supports("bmi2")) {
        return _pext_u64(subset, blocker_mask);
    }
#endif

    // Fall back to using magic number if we can't use PEXT
    return (subset * magic_number) >> (64 - std::popcount(blocker_mask));
}
