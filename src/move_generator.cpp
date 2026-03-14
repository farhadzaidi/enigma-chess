#include "move_generator.hpp"

#include <bit>
#include <cstdlib>
#include <span>
#include <vector>

#include "data/magics.hpp"

namespace {

// --- Shared Helpers ---

/** Walk from a square along direction D until hitting a blocker or the board edge */
template <Direction D>
Bitboard walk(Square sq, Bitboard blockers = 0) {
    Bitboard attack = 0;
    Bitboard mask = shift<D>(get_mask(sq));
    while (mask && ((mask & blockers) == 0)) {
        attack |= mask;
        mask = shift<D>(mask);
    }
    return attack;
}

// --- Rays / Lines ---

using RayMap = std::array<Bitboard, NUM_SQUARES>;
using LineMap = std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SQUARES>;

/** Precompute the unblocked ray bitboard for every square in direction D */
template <Direction D>
RayMap build_rays() {
    RayMap ray_map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        ray_map[sq] = walk<D>(sq);
    }
    return ray_map;
}

/** Return the cardinal/diagonal direction from square a toward square b, or NO_DIRECTION if not collinear */
Direction get_direction(Square a, Square b) {
    if (a == b) return NO_DIRECTION;

    int a_rank = get_rank(a);
    int a_file = get_file(a);
    int b_rank = get_rank(b);
    int b_file = get_file(b);

    int dx = std::abs(a_file - b_file);
    int dy = std::abs(a_rank - b_rank);
    bool are_collinear = dx == 0 || dy == 0 || dx == dy;
    if (!are_collinear) return NO_DIRECTION;

    Direction vertical = a_rank != b_rank ? (a_rank < b_rank ? NORTH : SOUTH) : NO_DIRECTION;
    Direction horizontal = a_file != b_file ? (a_file < b_file ? EAST : WEST) : NO_DIRECTION;
    return static_cast<Direction>(vertical + horizontal);
}

const RayMap NORTH_RAY_MAP = build_rays<NORTH>();
const RayMap SOUTH_RAY_MAP = build_rays<SOUTH>();
const RayMap EAST_RAY_MAP = build_rays<EAST>();
const RayMap WEST_RAY_MAP = build_rays<WEST>();
const RayMap NORTHEAST_RAY_MAP = build_rays<NORTHEAST>();
const RayMap NORTHWEST_RAY_MAP = build_rays<NORTHWEST>();
const RayMap SOUTHEAST_RAY_MAP = build_rays<SOUTHEAST>();
const RayMap SOUTHWEST_RAY_MAP = build_rays<SOUTHWEST>();
const RayMap EMPTY_RAY_MAP{};

template <Direction D>
const RayMap& get_ray_map() {
    if constexpr (D == NORTH) return NORTH_RAY_MAP;
    if constexpr (D == SOUTH) return SOUTH_RAY_MAP;
    if constexpr (D == EAST) return EAST_RAY_MAP;
    if constexpr (D == WEST) return WEST_RAY_MAP;
    if constexpr (D == NORTHEAST) return NORTHEAST_RAY_MAP;
    if constexpr (D == NORTHWEST) return NORTHWEST_RAY_MAP;
    if constexpr (D == SOUTHEAST) return SOUTHEAST_RAY_MAP;
    if constexpr (D == SOUTHWEST) return SOUTHWEST_RAY_MAP;
    else return EMPTY_RAY_MAP;
}

/** Precompute the segment between any two collinear squares (used for pin/check masks) */
const LineMap LINES = []() {
    auto get_ray_map_by_direction = [](Direction direction) -> const RayMap& {
        switch (direction) {
            case NORTH: return NORTH_RAY_MAP;
            case SOUTH: return SOUTH_RAY_MAP;
            case EAST: return EAST_RAY_MAP;
            case WEST: return WEST_RAY_MAP;
            case NORTHEAST: return NORTHEAST_RAY_MAP;
            case NORTHWEST: return NORTHWEST_RAY_MAP;
            case SOUTHEAST: return SOUTHEAST_RAY_MAP;
            case SOUTHWEST: return SOUTHWEST_RAY_MAP;
            default: return EMPTY_RAY_MAP;
        }
    };

    LineMap lines{};
    for (Square a = 0; a < NUM_SQUARES; a++) {
        for (Square b = 0; b < NUM_SQUARES; b++) {
            Direction towards_b = get_direction(a, b);
            if (towards_b == NO_DIRECTION) {
                lines[a][b] = EMPTY_BITBOARD;
                continue;
            }

            Bitboard ray_towards_b = get_ray_map_by_direction(towards_b)[a];
            Direction towards_a = get_direction(b, a);
            Bitboard ray_towards_a = get_ray_map_by_direction(towards_a)[b];
            lines[a][b] = (ray_towards_b & ray_towards_a) | get_mask(b);
        }
    }

    return lines;
}();

// --- Sliding Attack Tables ---

/** Map an occupancy subset to an attack table index via PEXT or magic multiplication */
uint64_t get_attack_table_index(Bitboard subset, Bitboard blocker_mask, uint64_t magic_number) {
#if defined(__BMI2__)
    // Hardware bit extract gives a perfect hash without magic numbers
    if (__builtin_cpu_supports("bmi2")) {
        return _pext_u64(subset, blocker_mask);
    }
#endif

    // Fallback: magic number multiplication maps the relevant bits to the top N bits
    return (subset * magic_number) >> (64 - std::popcount(blocker_mask));
}

/** Compute the total number of entries needed for a sliding piece attack table */
size_t compute_attack_table_size(const std::array<Bitboard, NUM_SQUARES>& blocker_map) {
    size_t size = 0;

    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard blocker_mask = blocker_map[sq];
        size += std::size_t{1} << std::popcount(blocker_mask);
    }

    return size;
}

/** Compute per-square offsets into the flat attack table */
std::array<size_t, NUM_SQUARES> compute_offset(const std::array<Bitboard, NUM_SQUARES>& blocker_map) {
    size_t size = 0;
    std::array<size_t, NUM_SQUARES> offset{};

    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        offset[sq] = size;
        Bitboard blocker_mask = blocker_map[sq];
        size += std::size_t{1} << std::popcount(blocker_mask);
    }

    return offset;
}

const std::array<Bitboard, NUM_SQUARES> BISHOP_BLOCKER_MAP = []() {
    std::array<Bitboard, NUM_SQUARES> map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        map[sq] =
            walk<NORTHEAST>(sq, RANK_MASKS[RANK_8] | FILE_MASKS[H_FILE]) |
            walk<NORTHWEST>(sq, RANK_MASKS[RANK_8] | FILE_MASKS[A_FILE]) |
            walk<SOUTHEAST>(sq, RANK_MASKS[RANK_1] | FILE_MASKS[H_FILE]) |
            walk<SOUTHWEST>(sq, RANK_MASKS[RANK_1] | FILE_MASKS[A_FILE]);
    }
    return map;
}();

const std::array<Bitboard, NUM_SQUARES> ROOK_BLOCKER_MAP = []() {
    std::array<Bitboard, NUM_SQUARES> map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        map[sq] =
            walk<NORTH>(sq, RANK_MASKS[RANK_8]) |
            walk<SOUTH>(sq, RANK_MASKS[RANK_1]) |
            walk<EAST>(sq, FILE_MASKS[H_FILE]) |
            walk<WEST>(sq, FILE_MASKS[A_FILE]);
    }
    return map;
}();

const std::array<size_t, NUM_SQUARES> BISHOP_OFFSET = compute_offset(BISHOP_BLOCKER_MAP);
const std::array<size_t, NUM_SQUARES> ROOK_OFFSET = compute_offset(ROOK_BLOCKER_MAP);

const size_t BISHOP_ATTACK_TABLE_SIZE = compute_attack_table_size(BISHOP_BLOCKER_MAP);
const size_t ROOK_ATTACK_TABLE_SIZE = compute_attack_table_size(ROOK_BLOCKER_MAP);

const auto _BISHOP_ATTACK_TABLE = []() {
    std::vector<Bitboard> table(BISHOP_ATTACK_TABLE_SIZE);
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard blocker_mask = BISHOP_BLOCKER_MAP[sq];

        for (Bitboard subset = blocker_mask;; subset = (subset - 1) & blocker_mask) {
            Bitboard attack_mask =
                walk<NORTHEAST>(sq, shift<NORTHEAST>(subset)) |
                walk<NORTHWEST>(sq, shift<NORTHWEST>(subset)) |
                walk<SOUTHEAST>(sq, shift<SOUTHEAST>(subset)) |
                walk<SOUTHWEST>(sq, shift<SOUTHWEST>(subset));

            size_t index = get_attack_table_index(subset, blocker_mask, BISHOP_MAGIC[sq]);
            table[BISHOP_OFFSET[sq] + index] = attack_mask;

            if (subset == 0) {
                break;
            }
        }
    }

    return table;
}();

const auto _ROOK_ATTACK_TABLE = []() {
    std::vector<Bitboard> table(ROOK_ATTACK_TABLE_SIZE);
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard blocker_mask = ROOK_BLOCKER_MAP[sq];

        for (Bitboard subset = blocker_mask;; subset = (subset - 1) & blocker_mask) {
            Bitboard attack_mask =
                walk<NORTH>(sq, shift<NORTH>(subset)) |
                walk<SOUTH>(sq, shift<SOUTH>(subset)) |
                walk<EAST>(sq, shift<EAST>(subset)) |
                walk<WEST>(sq, shift<WEST>(subset));

            size_t index = get_attack_table_index(subset, blocker_mask, ROOK_MAGIC[sq]);
            table[ROOK_OFFSET[sq] + index] = attack_mask;

            if (subset == 0) {
                break;
            }
        }
    }

    return table;
}();

// --- Non-Sliding Attack Maps ---

using AttackMap = std::array<Bitboard, NUM_SQUARES>;
using PawnAttackMaps = std::array<AttackMap, NUM_SIDES>;

const std::span<const Bitboard> BISHOP_ATTACK_TABLE{_BISHOP_ATTACK_TABLE};
const std::span<const Bitboard> ROOK_ATTACK_TABLE{_ROOK_ATTACK_TABLE };

const AttackMap KNIGHT_ATTACK_MAP = []() {
    AttackMap map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard mask = get_mask(sq);
        map[sq] =
            shift<NORTHEAST>(shift<NORTH>(mask)) |
            shift<NORTHEAST>(shift<EAST>(mask)) |
            shift<NORTHWEST>(shift<NORTH>(mask)) |
            shift<NORTHWEST>(shift<WEST>(mask)) |
            shift<SOUTHEAST>(shift<SOUTH>(mask)) |
            shift<SOUTHEAST>(shift<EAST>(mask)) |
            shift<SOUTHWEST>(shift<SOUTH>(mask)) |
            shift<SOUTHWEST>(shift<WEST>(mask));
    }
    return map;
}();

const AttackMap KING_ATTACK_MAP = []() {
    AttackMap map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard mask = get_mask(sq);
        map[sq] =
            shift<NORTH>(mask) |
            shift<SOUTH>(mask) |
            shift<EAST>(mask) |
            shift<WEST>(mask) |
            shift<NORTHEAST>(mask) |
            shift<NORTHWEST>(mask) |
            shift<SOUTHEAST>(mask) |
            shift<SOUTHWEST>(mask);
    }
    return map;
}();

const PawnAttackMaps PAWN_ATTACK_MAPS = []() {
    AttackMap white_map{};
    AttackMap black_map{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard mask = get_mask(sq);
        white_map[sq] = shift<SOUTHEAST>(mask) | shift<SOUTHWEST>(mask);
        black_map[sq] = shift<NORTHEAST>(mask) | shift<NORTHWEST>(mask);
    }

    return PawnAttackMaps{white_map, black_map};
}();

// --- Piece Attack Helpers ---

/** Look up the precomputed sliding attack bitboard for piece P using magic bitboard indexing */
template <Piece P>
Bitboard generate_sliding_attack_mask(Square from, Bitboard occupied) {
    const auto& blocker_map = P == BISHOP ? BISHOP_BLOCKER_MAP : ROOK_BLOCKER_MAP;
    const auto& magic = P == BISHOP ? BISHOP_MAGIC : ROOK_MAGIC;
    const auto& offset = P == BISHOP ? BISHOP_OFFSET : ROOK_OFFSET;
    const auto& attack_table = P == BISHOP ? BISHOP_ATTACK_TABLE : ROOK_ATTACK_TABLE;

    Bitboard blocker_mask = blocker_map[from];
    Bitboard blockers = occupied & blocker_mask;  // only bits that can actually block matter
    size_t index = get_attack_table_index(blockers, blocker_mask, magic[from]);
    return attack_table[offset[from] + index];  // per-square offset + hashed blocker config
}

} // namespace

// --- Public Attack API ---

Bitboard get_pawn_attacks(Side side, Square square) {
    return PAWN_ATTACK_MAPS[side][square];
}

template <Piece P>
Bitboard get_piece_attacks(Square from, Bitboard occupied) {
    if constexpr (P == KNIGHT) return KNIGHT_ATTACK_MAP[from];
    else if constexpr (P == KING) return KING_ATTACK_MAP[from];
    else if constexpr (P == BISHOP) return generate_sliding_attack_mask<BISHOP>(from, occupied);
    else if constexpr (P == ROOK) return generate_sliding_attack_mask<ROOK>(from, occupied);
    else if constexpr (P == QUEEN) return generate_sliding_attack_mask<BISHOP>(from, occupied) |
                                          generate_sliding_attack_mask<ROOK>(from, occupied);
    else return EMPTY_BITBOARD;
}

Bitboard get_piece_attacks(Piece piece, Square from, Bitboard occupied) {
    switch (piece) {
        case KNIGHT: return ::get_piece_attacks<KNIGHT>(from, occupied);
        case KING: return ::get_piece_attacks<KING>(from, occupied);
        case BISHOP: return ::get_piece_attacks<BISHOP>(from, occupied);
        case ROOK: return ::get_piece_attacks<ROOK>(from, occupied);
        case QUEEN: return ::get_piece_attacks<QUEEN>(from, occupied);
        default: return EMPTY_BITBOARD;
    }
}

// --- Check Info ---

/** True if the piece is a sliding piece (bishop, rook, or queen) */
bool is_slider(Piece piece) {
    return piece == BISHOP || piece == ROOK || piece == QUEEN;
}

/** True if the piece can slide along direction D (rook/queen for orthogonal, bishop/queen for diagonal) */
template <Direction D>
bool is_relevant_sliding_piece(Piece piece) {
    if constexpr (D == NORTH || D == SOUTH || D == EAST || D == WEST) {
        return piece == ROOK || piece == QUEEN;
    } else if constexpr (D == NORTHEAST || D == NORTHWEST || D == SOUTHEAST || D == SOUTHWEST) {
        return piece == BISHOP || piece == QUEEN;
    } else {
        return false;
    }
}

template <Side S>
void MoveGenerator::compute_check_info() {
    constexpr Side friendly_side = S;
    constexpr Side enemy_side = opposite_side(S);
    Square king_sq = board_.king_squares()[friendly_side];
    auto& enemy_pieces = board_.pieces()[enemy_side];

    // Sliding checks and pins along all 8 ray directions
    compute_sliding_checks_and_pins<S, NORTH>(king_sq);
    compute_sliding_checks_and_pins<S, SOUTH>(king_sq);
    compute_sliding_checks_and_pins<S, EAST>(king_sq);
    compute_sliding_checks_and_pins<S, WEST>(king_sq);
    compute_sliding_checks_and_pins<S, NORTHEAST>(king_sq);
    compute_sliding_checks_and_pins<S, NORTHWEST>(king_sq);
    compute_sliding_checks_and_pins<S, SOUTHEAST>(king_sq);
    compute_sliding_checks_and_pins<S, SOUTHWEST>(king_sq);

    // Non-sliding checks: pawns, knights, king
    check_info_.checkers |= get_pawn_attacks(enemy_side, king_sq) & enemy_pieces[PAWN];
    check_info_.checkers |= ::get_piece_attacks<KNIGHT>(king_sq, board_.occupied()) & enemy_pieces[KNIGHT];
    check_info_.checkers |= ::get_piece_attacks<KING>(king_sq, board_.occupied()) & enemy_pieces[KING];

    // Squares the king must not move to
    check_info_.unsafe =
        compute_attack_mask<S, PAWN>() |
        compute_attack_mask<S, BISHOP>() |
        compute_attack_mask<S, KNIGHT>() |
        compute_attack_mask<S, ROOK>() |
        compute_attack_mask<S, QUEEN>() |
        compute_attack_mask<S, KING>();

    // Single check: non-king pieces must block or capture the checker
    if (std::popcount(check_info_.checkers) == 1) {
        Square checker_sq = get_lsb(check_info_.checkers);
        Piece checker_piece = board_.piece_map()[checker_sq];
        // Sliders can be blocked along their line; non-sliders must be captured directly
        check_info_.must_cover = is_slider(checker_piece) ? LINES[king_sq][checker_sq] : check_info_.checkers;
    }
}

template <Side S, Direction D>
void MoveGenerator::compute_sliding_checks_and_pins(Square king_sq) {
    const auto& ray_map = get_ray_map<D>();
    constexpr Side friendly_side = S;
    constexpr Side enemy_side = opposite_side(S);
    Bitboard enemy_pieces = board_.sides()[enemy_side];

    Bitboard ray_mask = ray_map[king_sq] & board_.occupied();
    if (ray_mask) {
        Square first = pop_next<D>(ray_mask);
        Bitboard first_mask = get_mask(first);

        if (first_mask & board_.sides()[friendly_side]) {
            // Friendly piece is between king and a potential pinner
            Square second = pop_next<D>(ray_mask);
            Bitboard second_mask = get_mask(second);

            if ((second_mask & enemy_pieces) && is_relevant_sliding_piece<D>(board_.piece_map()[second])) {
                check_info_.pinned |= first_mask;
                check_info_.pins[first] = LINES[king_sq][second];
            }
        } else if ((first_mask & enemy_pieces) && is_relevant_sliding_piece<D>(board_.piece_map()[first])) {
            // Enemy slider directly attacks the king along this ray
            check_info_.checkers |= first_mask;
        }
    }
}

template <Side S, Piece P>
Bitboard MoveGenerator::compute_attack_mask() {
    constexpr Side friendly_side = S;
    constexpr Side enemy_side = opposite_side(S);
    Bitboard pieces = board_.pieces()[enemy_side][P];

    if constexpr (P == PAWN) {
        return shift<S == WHITE ? SOUTHWEST : NORTHEAST>(board_.pieces()[enemy_side][PAWN]) |
               shift<S == WHITE ? SOUTHEAST : NORTHWEST>(board_.pieces()[enemy_side][PAWN]);
    } else {
        // Remove our king from occupied so x-ray attacks through it are included
        Bitboard occupied = board_.occupied() ^ board_.pieces()[friendly_side][KING];

        Bitboard attack_mask = 0ULL;
        while (pieces) {
            Square from = pop_lsb(pieces);
            attack_mask |= ::get_piece_attacks<P>(from, occupied);
        }

        return attack_mask;
    }
}

template <Side S, Direction D>
bool MoveGenerator::is_attacked_by_slider(Square square, Bitboard occupied) {
    constexpr Side enemy_side = opposite_side(S);
    const auto& ray_map = get_ray_map<D>();
    Bitboard ray_mask = ray_map[square] & occupied;
    if (ray_mask) {
        Square first = pop_next<D>(ray_mask);
        Bitboard first_mask = get_mask(first);
        if ((first_mask & board_.sides()[enemy_side]) && is_relevant_sliding_piece<D>(board_.piece_map()[first])) {
            return true;
        }
    }

    return false;
}

template <Side S>
bool MoveGenerator::is_attacked_by_slider(Square square, Bitboard occupied) {
    return (
        is_attacked_by_slider<S, NORTH>(square, occupied) ||
        is_attacked_by_slider<S, SOUTH>(square, occupied) ||
        is_attacked_by_slider<S, EAST>(square, occupied) ||
        is_attacked_by_slider<S, WEST>(square, occupied) ||
        is_attacked_by_slider<S, NORTHEAST>(square, occupied) ||
        is_attacked_by_slider<S, NORTHWEST>(square, occupied) ||
        is_attacked_by_slider<S, SOUTHEAST>(square, occupied) ||
        is_attacked_by_slider<S, SOUTHWEST>(square, occupied)
    );
}

// --- Non-Pawn Moves ---

template <Side S, Piece P, MoveGenMode M>
void MoveGenerator::generate_piece_moves(MoveList& moves) {
    constexpr Side enemy_side = opposite_side(S);
    Bitboard pieces = board_.pieces()[S][P];
    Bitboard friendly_pieces = board_.sides()[S];
    Bitboard enemy_pieces = board_.sides()[enemy_side];
    Bitboard empty = ~board_.occupied();

    while (pieces) {
        Square from = pop_lsb(pieces);

        Bitboard attack_mask = ::get_piece_attacks<P>(from, board_.occupied());
        if constexpr (P == KING) {
            attack_mask &= ~check_info_.unsafe;
        }
        attack_mask &= ~friendly_pieces;

        if constexpr (P != KING) {
            attack_mask &= check_info_.must_cover;
        }

        if (check_info_.pinned & get_mask(from)) {
            attack_mask &= check_info_.pins[from];
        }

        if constexpr (M == MGM_QUIET_ONLY || M == MGM_ALL) {
            Bitboard quiet_moves = attack_mask & empty;
            while (quiet_moves) {
                Square to = pop_lsb(quiet_moves);
                moves.add(Move(from, to, MT_QUIET, MF_NORMAL));
            }
        }

        if constexpr (M == MGM_TACTICAL_ONLY || M == MGM_ALL) {
            Bitboard captures = attack_mask & enemy_pieces;
            while (captures) {
                Square to = pop_lsb(captures);

                if constexpr (P == KING) {
                    // Verify the king doesn't walk into a sliding attack after leaving its square
                    Bitboard from_mask = get_mask(from);
                    Bitboard occupied = board_.occupied() ^ from_mask;
                    if (is_attacked_by_slider<S>(to, occupied)) continue;
                }

                moves.add(Move(from, to, MT_CAPTURE, MF_NORMAL));
            }
        }
    }
}

// --- Pawn Moves ---

template <Side S, Direction D, MoveType MT, bool IS_PROMOTION, bool IS_EN_PASSANT>
void MoveGenerator::encode_pawn_moves(MoveList& moves, Bitboard move_mask) {
    while (move_mask) {
        Square to = pop_lsb(move_mask);
        Square from = to - D;

        Bitboard from_mask = get_mask(from);
        Bitboard to_mask = get_mask(to);
        if (check_info_.pinned & from_mask) {
            to_mask &= check_info_.pins[from];
            if (!to_mask) continue;
        }

        if constexpr (IS_PROMOTION) {
            moves.add(Move(from, to, MT, MF_PROMO_QUEEN));
            moves.add(Move(from, to, MT, MF_PROMO_ROOK));
            moves.add(Move(from, to, MT, MF_PROMO_BISHOP));
            moves.add(Move(from, to, MT, MF_PROMO_KNIGHT));
        } else {
            if constexpr (IS_EN_PASSANT) {
                constexpr Direction BACK = S == WHITE ? SOUTH : NORTH;
                Bitboard capture_mask = shift<BACK>(to_mask);

                // Must resolve check: either capture the checker or land on a blocking square
                if (check_info_.checkers) {
                    bool captures_checker = (capture_mask & check_info_.checkers) != 0;
                    bool blocks_line = (to_mask & check_info_.must_cover) != 0;

                    if (!captures_checker && !blocks_line) return;
                }

                // EP removes two pawns from one rank; verify this doesn't expose the king to a slider
                Bitboard occupied = board_.occupied() ^ from_mask ^ to_mask ^ capture_mask;
                if (is_attacked_by_slider<S>(board_.king_square(S), occupied)) return;
                moves.add(Move(from, to, MT, MF_EN_PASSANT));
            } else {
                moves.add(Move(from, to, MT, MF_NORMAL));
            }
        }
    }
}

template <Side S, MoveGenMode M>
void MoveGenerator::generate_pawn_moves(MoveList& moves) {
    constexpr Direction FWD = S == WHITE ? NORTH : SOUTH;
    constexpr Direction FWD_FWD = S == WHITE ? NORTH_NORTH : SOUTH_SOUTH;
    constexpr Direction FWD_RIGHT = S == WHITE ? NORTHEAST : SOUTHWEST;
    constexpr Direction FWD_LEFT = S == WHITE ? NORTHWEST : SOUTHEAST;
    constexpr Bitboard PROMO_MASK = S == WHITE ? rank_mask(RANK_7) : rank_mask(RANK_2);
    constexpr Bitboard DOUBLE_PUSH_MASK = S == WHITE ? rank_mask(RANK_4) : rank_mask(RANK_5);

    Bitboard pawns = board_.pieces()[S][PAWN];
    Bitboard promo_pawns = pawns & PROMO_MASK;
    Bitboard non_promo_pawns = pawns & ~PROMO_MASK;
    Bitboard empty = ~board_.occupied();

    // --- Quiet pawn pushes ---
    if constexpr (M == MGM_QUIET_ONLY || M == MGM_ALL) {
        Bitboard single_push = shift<FWD>(non_promo_pawns) & empty;
        Bitboard double_push = shift<FWD>(single_push) & empty & DOUBLE_PUSH_MASK & check_info_.must_cover;

        single_push &= check_info_.must_cover;

        encode_pawn_moves<S, FWD, MT_QUIET>(moves, single_push);
        encode_pawn_moves<S, FWD_FWD, MT_QUIET>(moves, double_push);
    }

    // --- Tactical pawn moves: captures, promotions, en passant ---
    if constexpr (M == MGM_TACTICAL_ONLY || M == MGM_ALL) {
        Bitboard enemy_pieces = board_.sides()[opposite_side(S)];

        Bitboard right_capture_promo = shift<FWD_RIGHT>(promo_pawns) & enemy_pieces & check_info_.must_cover;
        Bitboard left_capture_promo  = shift<FWD_LEFT>(promo_pawns) & enemy_pieces & check_info_.must_cover;
        Bitboard push_promo          = shift<FWD>(promo_pawns) & empty & check_info_.must_cover;

        Bitboard right_capture = shift<FWD_RIGHT>(non_promo_pawns) & enemy_pieces & check_info_.must_cover;
        Bitboard left_capture  = shift<FWD_LEFT>(non_promo_pawns) & enemy_pieces & check_info_.must_cover;

        Bitboard right_en_passant = 0;
        Bitboard left_en_passant = 0;
        if (board_.en_passant_target() != NO_SQUARE) {
            Bitboard en_passant_target_mask = get_mask(board_.en_passant_target());
            right_en_passant = shift<FWD_RIGHT>(non_promo_pawns) & en_passant_target_mask;
            left_en_passant  = shift<FWD_LEFT>(non_promo_pawns) & en_passant_target_mask;
        }

        encode_pawn_moves<S, FWD_RIGHT, MT_CAPTURE, true>(moves, right_capture_promo);
        encode_pawn_moves<S, FWD_LEFT, MT_CAPTURE, true>(moves, left_capture_promo);
        encode_pawn_moves<S, FWD, MT_QUIET, true>(moves, push_promo);

        encode_pawn_moves<S, FWD_RIGHT, MT_CAPTURE>(moves, right_capture);
        encode_pawn_moves<S, FWD_LEFT, MT_CAPTURE>(moves, left_capture);

        encode_pawn_moves<S, FWD_RIGHT, MT_CAPTURE, false, true>(moves, right_en_passant);
        encode_pawn_moves<S, FWD_LEFT, MT_CAPTURE, false, true>(moves, left_en_passant);
    }
}

// --- Castling ---

template <Side S>
void MoveGenerator::generate_castling_moves(MoveList& moves) {
    constexpr auto SHORT_CASTLING_RIGHTS = S == WHITE ? WHITE_SHORT : BLACK_SHORT;
    constexpr auto LONG_CASTLING_RIGHTS = S == WHITE ? WHITE_LONG : BLACK_LONG;
    constexpr auto KING_SQUARE = S == WHITE ? E1 : E8;
    constexpr auto F_SQUARE = S == WHITE ? F1 : F8;
    constexpr auto G_SQUARE = S == WHITE ? G1 : G8;
    constexpr auto B_SQUARE = S == WHITE ? B1 : B8;
    constexpr auto D_SQUARE = S == WHITE ? D1 : D8;
    constexpr auto C_SQUARE = S == WHITE ? C1 : C8;
    constexpr auto SHORT_TO = S == WHITE ? G1 : G8;
    constexpr auto LONG_TO = S == WHITE ? C1 : C8;
    constexpr auto SHORT_CASTLE_PATH = get_mask(F_SQUARE) | get_mask(G_SQUARE);
    constexpr auto LONG_CASTLE_PATH = get_mask(B_SQUARE) | get_mask(C_SQUARE) | get_mask(D_SQUARE);

    if (std::popcount(check_info_.checkers) != 0) return;

    Bitboard king_short_castle_path = get_mask(F_SQUARE) | get_mask(G_SQUARE);
    Bitboard king_long_castle_path = get_mask(D_SQUARE) | get_mask(C_SQUARE);

    if (
        (board_.castling_rights() & SHORT_CASTLING_RIGHTS)
        && ((board_.occupied() & SHORT_CASTLE_PATH) == 0)
        && ((king_short_castle_path & check_info_.unsafe) == 0)
    ) {
        moves.add(Move(KING_SQUARE, SHORT_TO, MT_QUIET, MF_CASTLE));
    }

    if (
        (board_.castling_rights() & LONG_CASTLING_RIGHTS)
        && ((board_.occupied() & LONG_CASTLE_PATH) == 0)
        && ((king_long_castle_path & check_info_.unsafe) == 0)
    ) {
        moves.add(Move(KING_SQUARE, LONG_TO, MT_QUIET, MF_CASTLE));
    }
}

// --- Move Generation ---

template <Side S, MoveGenMode M>
void MoveGenerator::generate_moves_impl(MoveList& moves) {
    // Double check: only king moves are legal
    if (std::popcount(check_info_.checkers) == 2) {
        generate_piece_moves<S, KING, M>(moves);
        return;
    }

    if constexpr (M == MGM_QUIET_ONLY || M == MGM_ALL) {
        generate_castling_moves<S>(moves);
    }

    generate_pawn_moves<S, M>(moves);
    generate_piece_moves<S, BISHOP, M>(moves);
    generate_piece_moves<S, KNIGHT, M>(moves);
    generate_piece_moves<S, ROOK, M>(moves);
    generate_piece_moves<S, QUEEN, M>(moves);
    generate_piece_moves<S, KING, M>(moves);
}

MoveGenerator::MoveGenerator(Board& board) : board_(board) {
    if (board_.to_move() == WHITE) compute_check_info<WHITE>();
    else compute_check_info<BLACK>();
}

MoveList MoveGenerator::generate_quiets() {
    MoveList moves;
    if (board_.to_move() == WHITE) generate_moves_impl<WHITE, MGM_QUIET_ONLY>(moves);
    else generate_moves_impl<BLACK, MGM_QUIET_ONLY>(moves);
    return moves;
}

MoveList MoveGenerator::generate_tacticals() {
    MoveList moves;
    if (board_.to_move() == WHITE) generate_moves_impl<WHITE, MGM_TACTICAL_ONLY>(moves);
    else generate_moves_impl<BLACK, MGM_TACTICAL_ONLY>(moves);
    return moves;
}

MoveList MoveGenerator::generate_all() {
    MoveList moves;
    if (board_.to_move() == WHITE) generate_moves_impl<WHITE, MGM_ALL>(moves);
    else generate_moves_impl<BLACK, MGM_ALL>(moves);
    return moves;
}

// --- Template Declarations ---

template Bitboard get_piece_attacks<KNIGHT>(Square from, Bitboard occupied);
template Bitboard get_piece_attacks<KING>(Square from, Bitboard occupied);
template Bitboard get_piece_attacks<BISHOP>(Square from, Bitboard occupied);
template Bitboard get_piece_attacks<ROOK>(Square from, Bitboard occupied);
template Bitboard get_piece_attacks<QUEEN>(Square from, Bitboard occupied);
