#pragma once

#include <bit>
#include <vector>
#include <cstdint>
#include <iostream>
#include <span>

#include "types.hpp"
#include "utils.hpp"
#include "random.hpp"

using AttackMap         = std::array<Bitboard, NUM_SQUARES>;
using BlockerMap        = std::array<Bitboard, NUM_SQUARES>;

// --- Magic Maps ---
// These are magic numbers which are useful for looking up attack masks for sliding pieces.
// They are generated (via brute-force) using compute_magic_numbers()

constexpr std::array<uint64_t, NUM_SQUARES> BISHOP_MAGIC = {
    290491063393657344ULL,
    1134842633265152ULL,
    4649984774927155200ULL,
    2568742656016384ULL,
    72356936150419462ULL,
    2328664610067973153ULL,
    14411590344832593920ULL,
    37176689507442688ULL,
    37194863728672896ULL,
    10377146831273508944ULL,
    155389598090953472ULL,
    13245737861120ULL,
    2594077805088082049ULL,
    1271448731648ULL,
    1190077372182825008ULL,
    2258405507072512ULL,
    2885393523753992ULL,
    112590282809671776ULL,
    10957258477512441880ULL,
    596727020021354496ULL,
    1730508165441724440ULL,
    562955457005570ULL,
    2308380733808837120ULL,
    10381958227549489792ULL,
    1189531948111106048ULL,
    2595202034320802122ULL,
    9512730517574002761ULL,
    565149514104840ULL,
    9260108920326725640ULL,
    13836189040448114688ULL,
    90639343106130182ULL,
    10088350140195930369ULL,
    2287056530702880ULL,
    919200349556800ULL,
    20338010176884896ULL,
    18023228964473344ULL,
    565166156677250ULL,
    36327873308853256ULL,
    1157997541804083200ULL,
    77726676527775872ULL,
    2450522384730474505ULL,
    3026564153983631872ULL,
    1162584013462963200ULL,
    9223794532796139522ULL,
    342275945220015104ULL,
    11538226712392253472ULL,
    301747815057523724ULL,
    36593963244716288ULL,
    324541208105844736ULL,
    142940859021312ULL,
    1152928103321305608ULL,
    3299080667201ULL,
    3497678738859231232ULL,
    5101769117974800ULL,
    19144731130068994ULL,
    589372600647680ULL,
    9078702004636236ULL,
    2289187563275264ULL,
    1515461573119320384ULL,
    36038263145793536ULL,
    5764607560348996096ULL,
    9313448582336135684ULL,
    9621958204922792070ULL,
    5206163660333940992ULL,
};

constexpr std::array<uint64_t, NUM_SQUARES> ROOK_MAGIC = {
    1765411328882712592ULL,
    8088482524017336328ULL,
    4683796427680251968ULL,
    36037593187487744ULL,
    2449975807192863232ULL,
    216179383481140224ULL,
    36029896631255168ULL,
    8214566032254239236ULL,
    2612369401801868544ULL,
    70437465751616ULL,
    576742433975500864ULL,
    36310375343333632ULL,
    2306265256038760832ULL,
    4901043462784163848ULL,
    11532029830263702016ULL,
    171699736900862612ULL,
    9133093340315778ULL,
    90160498803200ULL,
    288301844944409856ULL,
    40542292519428353ULL,
    882846814343015424ULL,
    6953699112105542144ULL,
    4632238090883696976ULL,
    76000443071062145ULL,
    90072544450736192ULL,
    297238435473731584ULL,
    18695996874752ULL,
    1153204117750939680ULL,
    149537877659904ULL,
    567350147547264ULL,
    9150152946764048ULL,
    9223653584848126018ULL,
    9007474141069312ULL,
    295021028688527424ULL,
    7206040957169442816ULL,
    4611773981513484288ULL,
    6919415791750219776ULL,
    140754676621825ULL,
    1297046932488716808ULL,
    176507083293699ULL,
    9259401246262444032ULL,
    585555914099015688ULL,
    153123487115182112ULL,
    13835251569731338368ULL,
    4919619679524356128ULL,
    36592313916489730ULL,
    1170546622472ULL,
    18085181734912004ULL,
    306244922837713024ULL,
    9227876323907600448ULL,
    1689537327694336ULL,
    35527970062592ULL,
    1170949099403870848ULL,
    578996226184216704ULL,
    2305983759587606656ULL,
    1153066937174197760ULL,
    35760199713026ULL,
    2305992620658147329ULL,
    578888551295354946ULL,
    9811373298177560577ULL,
    5810206606768506882ULL,
    189714151617400834ULL,
    3378833860373028ULL,
    9224498503701561378ULL,
};

// We can precompute castling rights updates to make it much faster during make move.
// This lookup table keeps track of which castling rights are lost when a piece
// moves from or to that square. 
constexpr auto castling_rights_updates = []() {
    std::array<CastlingRights, NUM_SQUARES> castling_rights_updates = {NO_CASTLING_RIGHTS};
    castling_rights_updates[E1] = WHITE_SHORT | WHITE_LONG;
    castling_rights_updates[H1] = WHITE_SHORT;
    castling_rights_updates[A1] = WHITE_LONG;
    castling_rights_updates[E8] = BLACK_SHORT | BLACK_LONG;
    castling_rights_updates[H8] = BLACK_SHORT;
    castling_rights_updates[A8] = BLACK_LONG;
    return castling_rights_updates;
}();

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
            walk<NORTHEAST>(sq, RANK_8_MASK | H_FILE_MASK) |
            walk<NORTHWEST>(sq, RANK_8_MASK | A_FILE_MASK) |
            walk<SOUTHEAST>(sq, RANK_1_MASK | H_FILE_MASK) |
            walk<SOUTHWEST>(sq, RANK_1_MASK | A_FILE_MASK);
    }
    return map;
}();

// Same thing as above but for rook
constexpr BlockerMap ROOK_BLOCKER_MAP = []() {
    BlockerMap map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        map[sq] =
            walk<NORTH>(sq, RANK_8_MASK) |
            walk<SOUTH>(sq, RANK_1_MASK) |
            walk<EAST> (sq, H_FILE_MASK) |
            walk<WEST> (sq, A_FILE_MASK);
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
        int num_blockers = std::popcount(blocker_mask);

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

// This function is used to compute magic numbers which are useful for generating
// indices for rook and bishop attack tables.
// Note that the source code contains hardcoded values generated using this function,
// but this function is available incase these values ever need to be regenerated.
inline void compute_magic_numbers(const BlockerMap& blocker_map) {
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard blocker_mask = blocker_map[sq];
        int num_blockers = std::popcount(blocker_mask);
        std::size_t num_subsets = uint64_t{1} << num_blockers;

        // General algorithm for computing magic for each square goes like this:
        // 1. Generate a random number to try and assume it's valid
        // 2. Keep an array of used indices
        // 3. Generate the index for a given subset using the candidate magic number
        // 4. Check if the index has already been marked used 
            // If so, we have a collision --> restart with an empty used array and new candidate
            // Else, mark this index as used and keep going
        // 5. If a number generates all unique indices, then this is a valid magic number
        while (true) {
            uint64_t candidate = prandom_magic();
            std::vector<bool> used(num_subsets, false);
            bool is_valid_magic = true;

            for (Bitboard subset = blocker_mask;; subset = (subset - 1) & blocker_mask) {
                size_t index = (subset * candidate) >> (NUM_SQUARES - num_blockers);
                if (used[index]) {
                    is_valid_magic = false;
                    break;
                }
                used[index] = true;

                if (subset == 0) {
                    break;
                }
            }

            if (is_valid_magic) {
                std::clog << candidate << "\n";
                break;
            }
        }
    }
}

// Ray masks from each square to the end of the board (not including the square)
using RayMap = std::array<Bitboard, NUM_SQUARES>;

template <Direction D>
static constexpr RayMap compute_rays() {
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

    Rank a_rank = get_rank(a);
    File a_file = get_file(a);

    Rank b_rank = get_rank(b);
    File b_file = get_file(b);

    // Check collinearity
    int dx = abs_val(a_file - b_file);
    int dy = abs_val(a_rank - b_rank);
    bool are_colinear = (
        dx == 0 || // same file
        dy == 0 || // same rank
        abs_val(dx) == abs_val(dy) // same diagonal
    );
    if (!are_colinear) return NO_DIRECTION;

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

// Late Move Reduction table
// R(depth, move_index) ≈ floor(ln(depth + 1) * ln(move_index + 1) / tuning_constant)
constexpr int LMR_MAX_MOVES = 128;
constexpr int TUNING_CONSTANT = 2.0;

static inline const auto LMR_TABLE = []() {
    std::array<std::array<int, LMR_MAX_MOVES>, MAX_SEARCH_PLY> table{};
    for (int d = 0; d < MAX_SEARCH_PLY; d++) {
        for (int m = 0; m < LMR_MAX_MOVES; m++) {
            table[d][m] = std::log(d + 1) * std::log(m + 1) / TUNING_CONSTANT;
        }
    }
    return table;
}();

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
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_COLORS> passed_pawn_masks = {};
    
    // White pawns
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int rank = get_rank(sq);
        Bitboard ranks_in_front = EMPTY_BITBOARD;
        for (int r = rank + 1; r < BOARD_SIZE; r++) {
            ranks_in_front |= RANK_MASKS[r];
        }

        passed_pawn_masks[WHITE][sq] = ranks_in_front & ADJACENT_FILE_MASKS_INCLUSIVE[sq];
    }

    // Black pawns
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int rank = get_rank(sq);
        Bitboard ranks_in_front = EMPTY_BITBOARD; // From black's perspective
        for (int r = rank - 1; r >= RANK_1; r--) {
            ranks_in_front |= RANK_MASKS[r];
        }

        passed_pawn_masks[BLACK][sq] = ranks_in_front & ADJACENT_FILE_MASKS_INCLUSIVE[sq];
    }

    return passed_pawn_masks;
}();