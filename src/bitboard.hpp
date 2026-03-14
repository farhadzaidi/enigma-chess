#pragma once

#include <bit>
#include <array>
#ifdef __BMI2__
#include <immintrin.h>
#endif

#include "board.hpp"

constexpr Bitboard EMPTY_BITBOARD = 0;

// --- Masks ---

/** Return a bitboard with all bits set on the given rank */
constexpr Bitboard rank_mask(int rank) {
    return Bitboard{0xFF} << (rank * 8);
}

/** Return a bitboard with all bits set on the given file */
constexpr Bitboard file_mask(int file) {
    return Bitboard{0x0101010101010101} << file;
}

/** Precomputed rank masks indexed by rank (0-7) */
constexpr std::array<Bitboard, BOARD_SIZE> RANK_MASKS = []() {
    std::array<Bitboard, BOARD_SIZE> masks{};
    for (int rank = 0; rank < BOARD_SIZE; rank++) {
        masks[rank] = rank_mask(rank);
    }
    return masks;
}();

/** Precomputed file masks indexed by file (0-7) */
constexpr std::array<Bitboard, BOARD_SIZE> FILE_MASKS = []() {
    std::array<Bitboard, BOARD_SIZE> masks{};
    for (int file = 0; file < BOARD_SIZE; file++) {
        masks[file] = file_mask(file);
    }
    return masks;
}();

constexpr Bitboard NOT_A_FILE = ~file_mask(A_FILE);
constexpr Bitboard NOT_H_FILE = ~file_mask(H_FILE);

/** Return a bitboard with a single bit set for the given square */
constexpr Bitboard get_mask(Square square) { return uint64_t{1} << square;}

// --- Bit Manipulation ---

/** Return the index of the least significant set bit */
constexpr Square get_lsb(Bitboard b) {
    return std::countr_zero(b);
}

/** Return and clear the least significant set bit */
constexpr Square pop_lsb(Bitboard& b) {
    Square sq = std::countr_zero(b);
    b &= b - 1; // pop
    return sq;
}

/** Return and clear the most significant set bit */
constexpr Square pop_msb(Bitboard& b) {
    Square sq = 63 - std::countl_zero(b);
    b &= ~(1ULL << sq);
    return sq;
}

/** Pop the next bit in traversal order appropriate for direction D */
template <Direction D>
constexpr Square pop_next(Bitboard& b) {
    // North/East directions scan low-to-high; South/West scan high-to-low
    constexpr bool use_pop_lsb = D == NORTH || D == EAST || D == NORTHEAST || D == NORTHWEST;
    if constexpr (use_pop_lsb) return pop_lsb(b);
    else return pop_msb(b);
}

/** Shift all bits in a bitboard by one step in direction D, masking wraparound */
template <Direction D>
constexpr Bitboard shift(Bitboard b) {
    switch(D) {
        case NORTH: return b << 8;
        case SOUTH: return b >> 8;
        case NORTH_NORTH: return b << 16;
        case SOUTH_SOUTH: return b >> 16;
        case EAST: return (b << 1) & NOT_A_FILE;
        case WEST: return (b >> 1) & NOT_H_FILE;
        case NORTHEAST: return (b << 9) & NOT_A_FILE;
        case NORTHWEST: return (b << 7) & NOT_H_FILE;
        case SOUTHEAST: return (b >> 7) & NOT_A_FILE;
        case SOUTHWEST: return (b >> 9) & NOT_H_FILE;
    }
}
