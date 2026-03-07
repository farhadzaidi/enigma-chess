#pragma once

#include "core/types.hpp"
#include "core/constants.hpp"
#include "core/move.hpp"
#include "board/board.hpp"
#include "core/bitboard.hpp"
#include "move_generator/check_info.hpp"
#include "move_generator/piece_moves.hpp"

namespace {

// functions
template <Side S, Direction D, MoveType MT, bool IS_PROMOTION = false, bool IS_EN_PASSANT = false>
inline void encode_pawn_moves(
    Board& b,
    MoveList& moves,
    CheckInfo& check_info,
    Bitboard move_mask
) {
    while (move_mask) {
        Square to = pop_lsb(move_mask);
        Square from = to - D;

        Bitboard from_mask = get_mask(from);
        Bitboard to_mask = get_mask(to);
        if (check_info.pinned & from_mask) {
            to_mask &= check_info.pins[from];
            if (!to_mask) continue;
        }

        if constexpr (IS_PROMOTION) {
            moves.add(Move(from, to, MT, MF_PROMO_QUEEN));
            moves.add(Move(from, to, MT, MF_PROMO_ROOK));
            moves.add(Move(from, to, MT, MF_PROMO_BISHOP));
            moves.add(Move(from, to, MT, MF_PROMO_KNIGHT));
        } else {
            if constexpr (IS_EN_PASSANT) {
                // Handle en passant edge cases

                constexpr Direction BACK = S == WHITE ? SOUTH : NORTH;
                Bitboard capture_mask = shift<BACK>(to_mask);

                if (check_info.checkers) {
                    // In the event of a single check, the moving EP pawn can either
                    // capture the checking pawn
                    bool captures_checker = (capture_mask & check_info.checkers) != 0;

                    // Or block the ray check (or stay pinned)
                    bool blocks_line = (to_mask & check_info.must_cover) != 0;

                    if (!captures_checker && !blocks_line) return;
                }

                // We also need to account for the case en passant opens up an
                // x-ray check

                b.occupied ^= from_mask;
                b.occupied ^= to_mask;
                b.occupied ^= capture_mask;

                bool is_attacked = is_attacked_by_slider<S>(b, b.king_squares[S]);

                b.occupied ^= capture_mask;
                b.occupied ^= to_mask;
                b.occupied ^= from_mask;

                if (is_attacked) return;
                moves.add(Move(from, to, MT, MF_EN_PASSANT));
            } else {
                moves.add(Move(from, to, MT, MF_NORMAL));
            }
        }
    }
}

} // namespace


template<Side S, MoveGenMode M>
inline void generate_pawn_moves(Board& b, MoveList& moves, CheckInfo& check_info) {
    // Compile-time constants derived from template
    constexpr Direction FWD              = S == WHITE ? NORTH : SOUTH;
    constexpr Direction FWD_FWD          = S == WHITE ? NORTH_NORTH : SOUTH_SOUTH;
    constexpr Direction FWD_RIGHT        = S == WHITE ? NORTHEAST: SOUTHWEST;
    constexpr Direction FWD_LEFT         = S == WHITE ? NORTHWEST: SOUTHEAST;
    constexpr Direction BACK             = S == WHITE ? SOUTH : NORTH;
    constexpr Bitboard  PROMO_MASK       = S == WHITE ? RANK_MASKS[RANK_7] : RANK_MASKS[RANK_2];
    constexpr Bitboard  DOUBLE_PUSH_MASK = S == WHITE ? RANK_MASKS[RANK_4] : RANK_MASKS[RANK_5];

    Bitboard pawns = b.pieces[S][PAWN];
    Bitboard promo_pawns = pawns & PROMO_MASK;
    Bitboard non_promo_pawns = pawns & ~PROMO_MASK;
    Bitboard empty = ~b.occupied;

    if constexpr (M == MoveGenMode::QuietOnly || M == MoveGenMode::All) {
        Bitboard single_push = shift<FWD>(non_promo_pawns) & empty;
        Bitboard double_push = shift<FWD>(single_push) & empty & DOUBLE_PUSH_MASK & check_info.must_cover;

        // Mask single push with must cover (we didn't do it earlier to generate double_push)
        single_push &= check_info.must_cover;

        encode_pawn_moves<S, FWD, MT_QUIET>(b, moves, check_info, single_push);
        encode_pawn_moves<S, FWD_FWD, MT_QUIET>(b, moves, check_info, double_push);
    }

    if constexpr (M == MoveGenMode::TacticalOnly || M == MoveGenMode::All) {
        Bitboard enemy_pieces = b.sides[opposite_side(S)];

        Bitboard right_capture_promo    = shift<FWD_RIGHT>(promo_pawns) & enemy_pieces & check_info.must_cover;
        Bitboard left_capture_promo     = shift<FWD_LEFT>(promo_pawns) & enemy_pieces & check_info.must_cover;
        Bitboard push_promo             = shift<FWD>(promo_pawns) & empty & check_info.must_cover;

        Bitboard right_capture          = shift<FWD_RIGHT>(non_promo_pawns) & enemy_pieces & check_info.must_cover;
        Bitboard left_capture           = shift<FWD_LEFT>(non_promo_pawns) & enemy_pieces & check_info.must_cover;

        Bitboard right_en_passant = 0;
        Bitboard left_en_passant = 0;
        if (b.en_passant_target != NO_SQUARE) {
            Bitboard en_passant_target_mask = get_mask(b.en_passant_target);
            right_en_passant = shift<FWD_RIGHT>(non_promo_pawns) & en_passant_target_mask;
            left_en_passant  = shift<FWD_LEFT>(non_promo_pawns) & en_passant_target_mask;
        }

        encode_pawn_moves<S, FWD_RIGHT, MT_CAPTURE, true>(b, moves, check_info, right_capture_promo);
        encode_pawn_moves<S, FWD_LEFT,  MT_CAPTURE, true>(b, moves, check_info, left_capture_promo);
        encode_pawn_moves<S, FWD,       MT_QUIET,   true>(b, moves, check_info, push_promo);

        encode_pawn_moves<S, FWD_RIGHT, MT_CAPTURE>(b, moves, check_info, right_capture);
        encode_pawn_moves<S, FWD_LEFT,  MT_CAPTURE>(b, moves, check_info, left_capture);

        encode_pawn_moves<S, FWD_RIGHT, MT_CAPTURE, false, true>(b, moves, check_info, right_en_passant);
        encode_pawn_moves<S, FWD_LEFT,  MT_CAPTURE, false, true>(b, moves, check_info, left_en_passant);
    }
}
