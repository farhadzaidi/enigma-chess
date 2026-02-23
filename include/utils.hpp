#pragma once

#include <vector>
#include <string>
#include <bit>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#ifdef __BMI2__
#include <immintrin.h>
#endif

#include "types.hpp"
#include "move.hpp"

class Board;

struct PerftEpdResult {
    std::string fen;
    std::unordered_map<SearchDepth, uint64_t> depth_nodes;
};

struct EngineEpdResult {
    std::string fen;
    std::string best_move_san;
};

// Parsing

constexpr Square get_square(int rank, int file) { return rank * BOARD_SIZE + file; }
constexpr Rank get_rank(Square square) { return square / BOARD_SIZE; }
constexpr File get_file(Square square) { return square % BOARD_SIZE; }

bool is_pos_int(const std::string& s);
Square uci_to_index(const std::string& square);
std::string index_to_uci(Square square);
Move encode_move_from_uci(const Board& b, const std::string& uci_move);
std::string decode_move_to_uci(Move move);
Move parse_move_from_san(Board& b, const std::string& san);

// File Utilites

void read_file(std::vector<std::string>& buffer, std::filesystem::path file_path, int max_lines = -1);
PerftEpdResult parse_perft_epd_line(std::string line);
EngineEpdResult parse_engine_epd_line(std::string line);

// Bitboards

static constexpr Bitboard NOT_A_FILE = ~A_FILE_MASK;
static constexpr Bitboard NOT_H_FILE = ~H_FILE_MASK;

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

constexpr Bitboard get_mask(Square square) { return uint64_t{1} << square;}

inline Square pop_lsb(Bitboard& b) {
    Square sq = std::countr_zero(b);
    b &= b - 1; // pop
    return sq;
}

inline Square pop_msb(Bitboard& b) {
    Square sq = 63 - std::countl_zero(b);
    b &= ~(1ULL << sq);
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

// Other

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

template <Piece P>
constexpr bool is_slider() { return (P == BISHOP || P == ROOK || P == QUEEN); }

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

