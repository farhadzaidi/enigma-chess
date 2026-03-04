#pragma once

#include <span>

#include "core/bitboard.hpp"
#include "precompute/magics.hpp"

using AttackMap = std::array<Bitboard, NUM_SQUARES>;

// NON-SLIDING PIECES

// Straightforward attack map generation: from each square, we just try going
// in every direction that the piece can go in and union the result of all
// directions
// Shift functions ensure that there is no wrap-around from a file to h file and
// vice versa. Furthemore, bitshift behavior naturally handles going off the board.

constexpr AttackMap KNIGHT_ATTACK_MAP = []() {
    AttackMap map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard mask = get_mask(sq);
        map[sq] =
            shift<NORTHEAST>(shift<NORTH>(mask)) |
            shift<NORTHEAST>(shift<EAST> (mask)) |
            shift<NORTHWEST>(shift<NORTH>(mask)) |
            shift<NORTHWEST>(shift<WEST> (mask)) |
            shift<SOUTHEAST>(shift<SOUTH>(mask)) |
            shift<SOUTHEAST>(shift<EAST> (mask)) |
            shift<SOUTHWEST>(shift<SOUTH>(mask)) |
            shift<SOUTHWEST>(shift<WEST> (mask));
    }
    return map;
}();

constexpr AttackMap KING_ATTACK_MAP = []() {
    AttackMap map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard mask = get_mask(sq);
        map[sq] = shift<NORTH>    (mask) |
                  shift<SOUTH>    (mask) |
                  shift<EAST>     (mask) |
                  shift<WEST>     (mask) |
                  shift<NORTHEAST>(mask) |
                  shift<NORTHWEST>(mask) |
                  shift<SOUTHEAST>(mask) |
                  shift<SOUTHWEST>(mask);
    }
    return map;
}();


// Array of attack maps used to check if a square is attacked by pawns
// Indexed by attacking color (e.g. PAWN_ATTACK_MAP[BLACK] checks if
// that square is attacked by black pawns)
constexpr auto PAWN_ATTACK_MAPS = []() {
    AttackMap white_map{}; // White attacking pawns
    AttackMap black_map{}; // Black attacking pawns
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard mask = get_mask(sq);
        white_map[sq] = shift<SOUTHEAST>(mask) | shift<SOUTHWEST>(mask);
        black_map[sq] = shift<NORTHEAST>(mask) | shift<NORTHWEST>(mask);
    }

    return std::array<AttackMap, NUM_COLORS>{white_map, black_map};
}();

// SLIDING PIECES

// Helper function that creates a mask from a given square and shifts that mask in
// a given direction until it encounters some blocker or goes off the board
// Returns a mask with nonblocked squares on the board set to 1
template <Direction D>
static constexpr Bitboard walk(Square sq, Bitboard blockers = 0) {
    Bitboard attack = 0;
    Bitboard mask = shift<D>(get_mask(sq));
    while (mask && ((mask & blockers) == 0)) {
        attack |= mask;
        mask = shift<D>(mask);
    }
    return attack;
}

// Map of all blocker squares for each square that the bishop is on
// So each entry contains a mask of all blocker squares for the bishop on that square
// Doesn't include edges since a piece on the edge isn't blocking another square
constexpr BlockerMap BISHOP_BLOCKER_MAP = []() {
    BlockerMap map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        map[sq] =
            walk<NORTHEAST>(sq, RANK_MASKS[RANK_8] | FILE_MASKS[H_FILE]) |
            walk<NORTHWEST>(sq, RANK_MASKS[RANK_8] | FILE_MASKS[A_FILE]) |
            walk<SOUTHEAST>(sq, RANK_MASKS[RANK_1] | FILE_MASKS[H_FILE]) |
            walk<SOUTHWEST>(sq, RANK_MASKS[RANK_1] | FILE_MASKS[A_FILE]);
    }
    return map;
}();

// Same thing as above but for rook
constexpr BlockerMap ROOK_BLOCKER_MAP = []() {
    BlockerMap map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        map[sq] =
            walk<NORTH>(sq, RANK_MASKS[RANK_8]) |
            walk<SOUTH>(sq, RANK_MASKS[RANK_1]) |
            walk<EAST> (sq, FILE_MASKS[H_FILE]) |
            walk<WEST> (sq, FILE_MASKS[A_FILE]);
    }
    return map;
}();

// Helper function used to compute sizes for rook and bishop attack tables
static constexpr size_t compute_attack_table_size(const BlockerMap& blocker_map) {
    size_t size = 0;

    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard blocker_mask = blocker_map[sq];
        // There are 2^N blocker configurations for this square where N is the
        // number of possible blocker squares i.e. popcount(blocker_mask)
        size += std::size_t{1} << std::popcount(blocker_mask);
    }

    return size;
}
static constexpr size_t BISHOP_ATTACK_TABLE_SIZE = compute_attack_table_size(BISHOP_BLOCKER_MAP);
static constexpr size_t ROOK_ATTACK_TABLE_SIZE = compute_attack_table_size(ROOK_BLOCKER_MAP);

// Helper function to compute offset for indexing into attack tables for each square
// Very similar logic to compute_attack_table_sizes but here we're saving cumulative
// sizes as we loop through all the squares
static constexpr std::array<size_t, NUM_SQUARES> compute_offset(const BlockerMap& blocker_map) {
    size_t size = 0;
    std::array<size_t, NUM_SQUARES> offset;

    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        offset[sq] = size;
        Bitboard blocker_mask = blocker_map[sq];
        size += 1 << std::popcount(blocker_mask);
    }

    return offset;
};
constexpr auto BISHOP_OFFSET = compute_offset(BISHOP_BLOCKER_MAP);
constexpr auto ROOK_OFFSET = compute_offset(ROOK_BLOCKER_MAP);

static inline const auto _BISHOP_ATTACK_TABLE = []() {
    std::array<Bitboard, BISHOP_ATTACK_TABLE_SIZE> table{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard blocker_mask = BISHOP_BLOCKER_MAP[sq];

        // Neat bit manipulation trick to enumerate all subsets of the blocker mask
        // i.e. get all possible blocker configurations
        for (Bitboard subset = blocker_mask;; subset = (subset - 1) & blocker_mask) {
            // Compute the attack mask by walking in each direction until we encounter
            // a blocker. Since we want to consider captures, we stop AFTER the blocker
            // which is why we pass the blocker mask shifted one step forward.
            Bitboard attack_mask =
                walk<NORTHEAST>(sq, shift<NORTHEAST>(subset)) |
                walk<NORTHWEST>(sq, shift<NORTHWEST>(subset)) |
                walk<SOUTHEAST>(sq, shift<SOUTHEAST>(subset)) |
                walk<SOUTHWEST>(sq, shift<SOUTHWEST>(subset));

            // Get index by either using PEXT or magic number
            size_t index = get_attack_table_index(subset, blocker_mask, BISHOP_MAGIC[sq]);

            // Index into the attack table using the offset and cache the attack mask
            table[BISHOP_OFFSET[sq] + index] = attack_mask;

            // Terminating condition
            if (subset == 0) {
                break;
            }
        };
    }

    return table;
}();

static inline const auto _ROOK_ATTACK_TABLE = []() {
    // Same logic as bishop attack table but using rook constants
    std::array<Bitboard, ROOK_ATTACK_TABLE_SIZE> table{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard blocker_mask = ROOK_BLOCKER_MAP[sq];

        for (Bitboard subset = blocker_mask;; subset = (subset - 1) & blocker_mask) {
            Bitboard attack_mask =
                walk<NORTH>(sq, shift<NORTH>(subset)) |
                walk<SOUTH>(sq, shift<SOUTH>(subset)) |
                walk<EAST> (sq, shift<EAST> (subset)) |
                walk<WEST> (sq, shift<WEST> (subset));

            size_t index = get_attack_table_index(subset, blocker_mask, ROOK_MAGIC[sq]);
            table[ROOK_OFFSET[sq] + index] = attack_mask;

            if (subset == 0) {
                break;
            }
        }
    }

    return table;
}();

// Expose attack tables as std::span since they have different types due to different sizes
inline const std::span<const Bitboard> BISHOP_ATTACK_TABLE{_BISHOP_ATTACK_TABLE};
inline const std::span<const Bitboard> ROOK_ATTACK_TABLE{_ROOK_ATTACK_TABLE};

