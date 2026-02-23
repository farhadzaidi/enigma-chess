#include "move_generator.hpp"
#include "types.hpp"
#include "utils.hpp"
#include "precompute.hpp"
#include "board.hpp"
#include "check_info.hpp"

constexpr Bitboard WHITE_LONG_CASTLE_PATH   = 0x000000000000000EULL;
constexpr Bitboard WHITE_SHORT_CASTLE_PATH  = 0x0000000000000060ULL;
constexpr Bitboard BLACK_LONG_CASTLE_PATH   = 0x0E00000000000000ULL;
constexpr Bitboard BLACK_SHORT_CASTLE_PATH  = 0x6000000000000000ULL;

template <Direction D>
static inline bool is_attacked_by_slider(const Board &b, Square sq) {
    constexpr auto& ray_map = get_ray_map<D>();
    Bitboard ray_mask = ray_map[sq] & b.occupied;
    if (ray_mask) {
        Square first = pop_next<D>(ray_mask);
        Bitboard first_mask = get_mask(first);
        if ((first_mask & b.colors[b.to_move ^ 1]) && is_relevant_sliding_piece<D>(b.piece_map[first])) {
            return true;
        }
    }

    return false;
}

static inline bool is_attacked_by_slider(const Board &b, Square sq) {
    return (
        is_attacked_by_slider<NORTH>    (b, sq) ||
        is_attacked_by_slider<SOUTH>    (b, sq) ||
        is_attacked_by_slider<EAST>     (b, sq) ||
        is_attacked_by_slider<WEST>     (b, sq) ||
        is_attacked_by_slider<NORTHEAST>(b, sq) ||
        is_attacked_by_slider<NORTHWEST>(b, sq) ||
        is_attacked_by_slider<SOUTHEAST>(b, sq) ||
        is_attacked_by_slider<SOUTHWEST>(b, sq)
    );
}

template <Piece P, MoveGenMode M>
static inline void generate_piece_moves(Board& b, MoveList& moves, CheckInfo& checkInfo) {
    Bitboard piece_bb = b.pieces[b.to_move][P];
    Bitboard us = b.colors[b.to_move];
    Bitboard them = b.colors[b.to_move ^ 1];
    Bitboard empty = ~b.occupied;

    while (piece_bb) {
        Square from = pop_lsb(piece_bb);

        Bitboard attack_mask = 
            P == KING   ? KING_ATTACK_MAP[from] & ~checkInfo.unsafe     :
            P == KNIGHT ? KNIGHT_ATTACK_MAP[from]                       :
            P == BISHOP ? generate_sliding_attack_mask<BISHOP>(b, from) :
            P == ROOK   ? generate_sliding_attack_mask<ROOK>  (b, from) :
            P == QUEEN  ? generate_sliding_attack_mask<BISHOP>(b, from) |
                          generate_sliding_attack_mask<ROOK>  (b, from) :
                          0;

        attack_mask &= ~us;

        if constexpr (P != KING) {
            attack_mask &= checkInfo.must_cover;
        }

        if (checkInfo.pinned & get_mask(from)) {
            attack_mask &= checkInfo.pins[from];
        }

        if constexpr (M == QUIET_ONLY || M == ALL) {
            Bitboard quiet_moves = attack_mask & empty;
            while (quiet_moves) {
                Square to = pop_lsb(quiet_moves);
                moves.add(Move(from, to, QUIET, NORMAL));
            }
        }

        if constexpr (M == CAPTURES_AND_PROMOTIONS || M == ALL) {
            Bitboard captures = attack_mask & them;
            while (captures) {
                Square to = pop_lsb(captures);

                if constexpr (P == KING) {
                    // If the move is a capture by the king, then we need to recompute
                    // enemy sliding attacks to see if an x-ray opened up.
                    Bitboard from_mask = get_mask(from);
                    b.occupied ^= from_mask;
                    bool is_attacked = is_attacked_by_slider(b, to);
                    b.occupied ^= from_mask;
                    if (is_attacked) continue;
                }

                moves.add(Move(from, to, CAPTURE, NORMAL));
            }
        }
    }
}

template <Color C, Direction D, MoveType MT, bool IS_PROMOTION = false, bool IS_EN_PASSANT = false>
static inline void encode_pawn_moves(
    Board &b,
    MoveList& moves,
    CheckInfo& checkInfo,
    Bitboard move_mask
) {
    while (move_mask) {
        Square to = pop_lsb(move_mask);
        Square from = to - D;
        
        Bitboard from_mask = get_mask(from);
        Bitboard to_mask = get_mask(to);
        if (checkInfo.pinned & from_mask) {
            to_mask &= checkInfo.pins[from];
            if (!to_mask) continue;
        }

        if constexpr (IS_PROMOTION) {
            moves.add(Move(from, to, MT, PROMOTION_QUEEN));
            moves.add(Move(from, to, MT, PROMOTION_ROOK));
            moves.add(Move(from, to, MT, PROMOTION_BISHOP));
            moves.add(Move(from, to, MT, PROMOTION_KNIGHT));
        } else {
            if constexpr (IS_EN_PASSANT) {
                // Handle en passant edge cases

                constexpr Direction BACK = C == WHITE ? SOUTH : NORTH;
                Bitboard capture_mask = shift<BACK>(to_mask);

                if (checkInfo.checkers) {
                    // In the event of a single check, the moving EP pawn can either
                    // capture the checking pawn
                    bool captures_checker = (capture_mask & checkInfo.checkers) != 0;

                    // Or block the ray check (or stay pinned)
                    bool blocks_line = (to_mask & checkInfo.must_cover) != 0;

                    if (!captures_checker && !blocks_line) return;
                }

                // We also need to account for the case en passant opens up an
                // x-ray check

                b.occupied ^= from_mask;
                b.occupied ^= to_mask;
                b.occupied ^= capture_mask;

                bool is_attacked = is_attacked_by_slider(b, b.king_squares[C]);

                b.occupied ^= capture_mask;
                b.occupied ^= to_mask;
                b.occupied ^= from_mask;

                if (is_attacked) return;
                moves.add(Move(from, to, MT, EN_PASSANT));
            } else {
                moves.add(Move(from, to, MT, NORMAL));
            }
        }
    }
}

template<Color C, MoveGenMode M>
static inline void generate_pawn_moves(Board& b, MoveList& moves, CheckInfo& checkInfo) {
    // Compile-time constants derived from template
    constexpr Direction FWD              = C == WHITE ? NORTH : SOUTH;
    constexpr Direction FWD_FWD          = C == WHITE ? NORTH_NORTH : SOUTH_SOUTH;
    constexpr Direction FWD_RIGHT        = C == WHITE ? NORTHEAST: SOUTHWEST;
    constexpr Direction FWD_LEFT         = C == WHITE ? NORTHWEST: SOUTHEAST;
    constexpr Direction BACK             = C == WHITE ? SOUTH : NORTH;
    constexpr Bitboard  PROMO_MASK       = C == WHITE ? RANK_7_MASK : RANK_2_MASK;
    constexpr Bitboard  DOUBLE_PUSH_MASK = C == WHITE ? RANK_4_MASK : RANK_5_MASK;

    Bitboard pawns = b.pieces[C][PAWN];
    Bitboard promo_pawns = pawns & PROMO_MASK;
    Bitboard non_promo_pawns = pawns & ~PROMO_MASK;\
    Bitboard empty = ~b.occupied;

    if constexpr (M == QUIET_ONLY || M == ALL) {
        Bitboard single_push = shift<FWD>(non_promo_pawns) & empty; 
        Bitboard double_push = shift<FWD>(single_push) & empty & DOUBLE_PUSH_MASK & checkInfo.must_cover;

        // Mask single push with must cover (we didn't do it earlier to generate double_push)
        single_push &= checkInfo.must_cover;

        encode_pawn_moves<C, FWD, QUIET>(b, moves, checkInfo, single_push);
        encode_pawn_moves<C, FWD_FWD, QUIET>(b, moves, checkInfo, double_push);
    }

    if constexpr (M == CAPTURES_AND_PROMOTIONS || M == ALL) {
        Bitboard enemy_pieces = b.colors[C ^ 1];

        Bitboard right_capture_promo    = shift<FWD_RIGHT>(promo_pawns) & enemy_pieces & checkInfo.must_cover;
        Bitboard left_capture_promo     = shift<FWD_LEFT>(promo_pawns) & enemy_pieces & checkInfo.must_cover;
        Bitboard push_promo             = shift<FWD>(promo_pawns) & empty & checkInfo.must_cover;

        Bitboard right_capture          = shift<FWD_RIGHT>(non_promo_pawns) & enemy_pieces & checkInfo.must_cover;
        Bitboard left_capture           = shift<FWD_LEFT>(non_promo_pawns) & enemy_pieces & checkInfo.must_cover;

        Bitboard right_en_passant = 0;
        Bitboard left_en_passant = 0;
        if (b.en_passant_target != NO_SQUARE) {
            Bitboard en_passant_target_mask = get_mask(b.en_passant_target);
            right_en_passant = shift<FWD_RIGHT>(non_promo_pawns) & en_passant_target_mask;
            left_en_passant  = shift<FWD_LEFT>(non_promo_pawns) & en_passant_target_mask;
        }

        encode_pawn_moves<C, FWD_RIGHT, CAPTURE, true>(b, moves, checkInfo, right_capture_promo);
        encode_pawn_moves<C, FWD_LEFT,  CAPTURE, true>(b, moves, checkInfo, left_capture_promo);
        encode_pawn_moves<C, FWD,       QUIET,   true>(b, moves, checkInfo, push_promo);

        encode_pawn_moves<C, FWD_RIGHT, CAPTURE>(b, moves, checkInfo, right_capture);
        encode_pawn_moves<C, FWD_LEFT,  CAPTURE>(b, moves, checkInfo, left_capture);

        encode_pawn_moves<C, FWD_RIGHT, CAPTURE, false, true>(b, moves, checkInfo, right_en_passant);
        encode_pawn_moves<C, FWD_LEFT,  CAPTURE, false, true>(b, moves, checkInfo, left_en_passant);
    }
}

template <Color C>
static inline void generate_castling_moves(Board &b, MoveList& moves, CheckInfo& checkInfo) {
    constexpr auto SHORT_CASTLING_RIGHTS = C == WHITE ? WHITE_SHORT : BLACK_SHORT;
    constexpr auto LONG_CASTLING_RIGHTS  = C == WHITE ? WHITE_LONG : BLACK_LONG;
    constexpr auto SHORT_CASTLE_PATH     = C == WHITE ? WHITE_SHORT_CASTLE_PATH : BLACK_SHORT_CASTLE_PATH;
    constexpr auto LONG_CASTLE_PATH      = C == WHITE ? WHITE_LONG_CASTLE_PATH : BLACK_LONG_CASTLE_PATH;
    constexpr auto SHORT_TO              = C == WHITE ? G1 : G8;
    constexpr auto LONG_TO               = C == WHITE ? C1 : C8;
    constexpr auto KING_SQUARE           = C == WHITE ? E1 : E8;

    // Castle path squares (that king walks over)
    constexpr auto F_SQUARE = C == WHITE ? F1 : F8;
    constexpr auto G_SQUARE = C == WHITE ? G1 : G8;

    constexpr auto D_SQUARE = C == WHITE ? D1 : D8;
    constexpr auto C_SQUARE = C == WHITE ? C1 : C8;

    if (std::popcount(checkInfo.checkers) != 0) return;

    // Compute the path that the king walks over (not the full path in the case of a long castle)
    Bitboard king_short_castle_path = get_mask(F_SQUARE) | get_mask(G_SQUARE);
    Bitboard king_long_castle_path = get_mask(D_SQUARE) | get_mask(C_SQUARE);

    // Short castle
    if (
        (b.castling_rights & SHORT_CASTLING_RIGHTS) // Have castling rights for this side
        && ((b.occupied & SHORT_CASTLE_PATH) == 0)  // Path is clear
        && ((king_short_castle_path & checkInfo.unsafe) == 0) // King doesn't pass thru check
    ) {
        moves.add(Move(KING_SQUARE, SHORT_TO, QUIET, CASTLE));
    } 
    
    // Long castle
    if (
        (b.castling_rights & LONG_CASTLING_RIGHTS)
        && ((b.occupied & LONG_CASTLE_PATH) == 0)
        && ((king_long_castle_path & checkInfo.unsafe) == 0)
    ) {
        moves.add(Move(KING_SQUARE, LONG_TO, QUIET, CASTLE));
    }
}

template <Color C, MoveGenMode M>
void generate_moves_impl(Board& b, MoveList& moves, CheckInfo& checkInfo) {
    // Double check; only king moves are valid (noncastling)
    if (std::popcount(checkInfo.checkers) == 2) {
        generate_piece_moves<KING, M>(b, moves, checkInfo);
        return;
    }

    // Only generate castling moves when generating quiet moves
    if constexpr (M == QUIET_ONLY || M == ALL) {
        generate_castling_moves<C>(b, moves, checkInfo);
    }

    generate_pawn_moves<C, M>(b, moves, checkInfo);

    generate_piece_moves<BISHOP, M>(b, moves, checkInfo);
    generate_piece_moves<KNIGHT, M>(b, moves, checkInfo);
    generate_piece_moves<ROOK,   M>(b, moves, checkInfo);
    generate_piece_moves<QUEEN,  M>(b, moves, checkInfo);
    generate_piece_moves<KING,   M>(b, moves, checkInfo);
}

template <MoveGenMode M>
MoveList generate_moves(Board &b) {
    MoveList moves;
    CheckInfo checkInfo;

    if (b.to_move == WHITE) {
        checkInfo.compute_check_info<WHITE>(b);
        generate_moves_impl<WHITE, M>(b, moves, checkInfo);
    } else {
        checkInfo.compute_check_info<BLACK>(b);
        generate_moves_impl<BLACK, M>(b, moves, checkInfo);
    }

    return moves;
}

// Explicit template instantiations
template MoveList generate_moves<ALL>(Board&);
template MoveList generate_moves<QUIET_ONLY>(Board&);
template MoveList generate_moves<CAPTURES_AND_PROMOTIONS>(Board&);

template void generate_moves_impl<WHITE, ALL>(Board&, MoveList&, CheckInfo&);
template void generate_moves_impl<WHITE, QUIET_ONLY>(Board&, MoveList&, CheckInfo&);
template void generate_moves_impl<WHITE, CAPTURES_AND_PROMOTIONS>(Board&, MoveList&, CheckInfo&);
template void generate_moves_impl<BLACK, ALL>(Board&, MoveList&, CheckInfo&);
template void generate_moves_impl<BLACK, QUIET_ONLY>(Board&, MoveList&, CheckInfo&);
template void generate_moves_impl<BLACK, CAPTURES_AND_PROMOTIONS>(Board&, MoveList&, CheckInfo&);