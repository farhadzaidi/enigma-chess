#pragma once

#include "precompute/attacks.hpp"

// Ray masks from each square to the end of the board (not including the square)
using RayMap = std::array<Bitboard, NUM_SQUARES>;

template <Direction D>
constexpr RayMap compute_rays() {
    RayMap ray_map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        ray_map[sq] = walk<D>(sq);
    }
    return ray_map;
};

constexpr RayMap NORTH_RAY_MAP     = compute_rays<NORTH>();
constexpr RayMap SOUTH_RAY_MAP     = compute_rays<SOUTH>();
constexpr RayMap EAST_RAY_MAP      = compute_rays<EAST>();
constexpr RayMap WEST_RAY_MAP      = compute_rays<WEST>();
constexpr RayMap NORTHEAST_RAY_MAP = compute_rays<NORTHEAST>();
constexpr RayMap NORTHWEST_RAY_MAP = compute_rays<NORTHWEST>();
constexpr RayMap SOUTHEAST_RAY_MAP = compute_rays<SOUTHEAST>();
constexpr RayMap SOUTHWEST_RAY_MAP = compute_rays<SOUTHWEST>();
constexpr RayMap EMPTY_RAY_MAP{};



// Using custom absolute value function since std::abs is not constexpr
constexpr int abs_val(int x) { return x > 0 ? x : -x;}

// Get the direction from square a to square b if they are collinear, else return NO_DIRECTION
constexpr Direction get_direction(Square a, Square b) {
    if (a == b) return NO_DIRECTION;

    int a_rank = get_rank(a);
    int a_file = get_file(a);

    int b_rank = get_rank(b);
    int b_file = get_file(b);

    // Check collinearity
    int dx = abs_val(a_file - b_file);
    int dy = abs_val(a_rank - b_rank);
    bool are_collinear = (
        dx == 0 || // same file
        dy == 0 || // same rank
        dx == dy // same diagonal
    );
    if (!are_collinear) return NO_DIRECTION;

    Direction vertical = a_rank != b_rank
        ? (a_rank < b_rank ? NORTH : SOUTH)
        : NO_DIRECTION;

    Direction horizontal = a_file != b_file
        ? (a_file < b_file ? EAST : WEST)
        : NO_DIRECTION;

    return vertical + horizontal;
}

// Maps directions to ray maps since we can't index with directions
constexpr const RayMap& get_ray_map(Direction direction) {
    switch (direction) {
        case NORTH:        return NORTH_RAY_MAP;
        case SOUTH:        return SOUTH_RAY_MAP;
        case EAST:         return EAST_RAY_MAP;
        case WEST:         return WEST_RAY_MAP;
        case NORTHEAST:    return NORTHEAST_RAY_MAP;
        case NORTHWEST:    return NORTHWEST_RAY_MAP;
        case SOUTHEAST:    return SOUTHEAST_RAY_MAP;
        case SOUTHWEST:    return SOUTHWEST_RAY_MAP;
        default:           return EMPTY_RAY_MAP;
    }
}

template <Direction D>
constexpr const RayMap& get_ray_map() {
    if constexpr (D == NORTH)     return NORTH_RAY_MAP;
    if constexpr (D == SOUTH)     return SOUTH_RAY_MAP;
    if constexpr (D == EAST)      return EAST_RAY_MAP;
    if constexpr (D == WEST)      return WEST_RAY_MAP;
    if constexpr (D == NORTHEAST) return NORTHEAST_RAY_MAP;
    if constexpr (D == NORTHWEST) return NORTHWEST_RAY_MAP;
    if constexpr (D == SOUTHEAST) return SOUTHEAST_RAY_MAP;
    if constexpr (D == SOUTHWEST) return SOUTHWEST_RAY_MAP;
    else                          return EMPTY_RAY_MAP;
}

// Computes lines from square a to square b including square b
constexpr auto LINES = []() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SQUARES> lines{};
    for (Square a = 0; a < NUM_SQUARES; a++) {
        for (Square b = 0; b < NUM_SQUARES; b++) {
            Direction towards_b = get_direction(a, b);
            if (towards_b == NO_DIRECTION) {
                lines[a][b] = uint64_t{0};
                continue;
            }

            Bitboard ray_towards_b = get_ray_map(towards_b)[a];

            Direction towards_a = get_direction(b, a);
            Bitboard ray_towards_a = get_ray_map(towards_a)[b];

            // Intersect both rays, leaving only squares between the a and b
            lines[a][b] = (ray_towards_b & ray_towards_a) | get_mask(b); // Include square b
        }
    }

    return lines;
}();
