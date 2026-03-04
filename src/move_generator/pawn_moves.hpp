#pragma once

#include "core/types.hpp"
#include "core/constants.hpp"
#include "core/move.hpp"
#include "board/board.hpp"
#include "core/bitboard.hpp"
#include "move_generator/check_info.hpp"
#include "move_generator/piece_moves.hpp"

template <Color C, Direction D, MoveType MT, bool IS_PROMOTION = false, bool IS_EN_PASSANT = false>
inline void encode_pawn_moves(
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
            moves.add(Move(from, to, MT, MoveFlag::PromoQueen));
            moves.add(Move(from, to, MT, MoveFlag::PromoRook));
            moves.add(Move(from, to, MT, MoveFlag::PromoBishop));
            moves.add(Move(from, to, MT, MoveFlag::PromoKnight));
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

                bool is_attacked = is_attacked_by_slider<C>(b, b.king_squares[C]);

                b.occupied ^= capture_mask;
                b.occupied ^= to_mask;
                b.occupied ^= from_mask;

                if (is_attacked) return;
                moves.add(Move(from, to, MT, MoveFlag::EnPassant));
            } else {
                moves.add(Move(from, to, MT, MoveFlag::Normal));
            }
        }
    }
}

template<Color C, MoveGenMode M>
inline void generate_pawn_moves(Board& b, MoveList& moves, CheckInfo& checkInfo) {
    // Compile-time constants derived from template
    constexpr Direction FWD              = C == WHITE ? NORTH : SOUTH;
    constexpr Direction FWD_FWD          = C == WHITE ? NORTH_NORTH : SOUTH_SOUTH;
    constexpr Direction FWD_RIGHT        = C == WHITE ? NORTHEAST: SOUTHWEST;
    constexpr Direction FWD_LEFT         = C == WHITE ? NORTHWEST: SOUTHEAST;
    constexpr Direction BACK             = C == WHITE ? SOUTH : NORTH;
    constexpr Bitboard  PROMO_MASK       = C == WHITE ? RANK_MASKS[RANK_7] : RANK_MASKS[RANK_2];
    constexpr Bitboard  DOUBLE_PUSH_MASK = C == WHITE ? RANK_MASKS[RANK_4] : RANK_MASKS[RANK_5];

    Bitboard pawns = b.pieces[C][PAWN];
    Bitboard promo_pawns = pawns & PROMO_MASK;
    Bitboard non_promo_pawns = pawns & ~PROMO_MASK;
    Bitboard empty = ~b.occupied;

    if constexpr (M == MoveGenMode::QuietOnly || M == MoveGenMode::All) {
        Bitboard single_push = shift<FWD>(non_promo_pawns) & empty;
        Bitboard double_push = shift<FWD>(single_push) & empty & DOUBLE_PUSH_MASK & checkInfo.must_cover;

        // Mask single push with must cover (we didn't do it earlier to generate double_push)
        single_push &= checkInfo.must_cover;

        encode_pawn_moves<C, FWD, MoveType::Quiet>(b, moves, checkInfo, single_push);
        encode_pawn_moves<C, FWD_FWD, MoveType::Quiet>(b, moves, checkInfo, double_push);
    }

    if constexpr (M == MoveGenMode::TacticalOnly || M == MoveGenMode::All) {
        Bitboard enemy_pieces = b.colors[opposite_color(C)];

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

        encode_pawn_moves<C, FWD_RIGHT, MoveType::Capture, true>(b, moves, checkInfo, right_capture_promo);
        encode_pawn_moves<C, FWD_LEFT,  MoveType::Capture, true>(b, moves, checkInfo, left_capture_promo);
        encode_pawn_moves<C, FWD,       MoveType::Quiet,   true>(b, moves, checkInfo, push_promo);

        encode_pawn_moves<C, FWD_RIGHT, MoveType::Capture>(b, moves, checkInfo, right_capture);
        encode_pawn_moves<C, FWD_LEFT,  MoveType::Capture>(b, moves, checkInfo, left_capture);

        encode_pawn_moves<C, FWD_RIGHT, MoveType::Capture, false, true>(b, moves, checkInfo, right_en_passant);
        encode_pawn_moves<C, FWD_LEFT,  MoveType::Capture, false, true>(b, moves, checkInfo, left_en_passant);
    }
}
