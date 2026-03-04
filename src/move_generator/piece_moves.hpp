#pragma once

#include "core/types.hpp"
#include "core/move.hpp"
#include "board/board.hpp"
#include "precompute/rays.hpp"
#include "move_generator/attacks.hpp"
#include "core/bitboard.hpp"
#include "move_generator/check_info.hpp"

template <Side S, Direction D>
inline bool is_attacked_by_slider(const Board& b, Square sq) {
    constexpr Side enemy_side = opposite_side(S);
    constexpr auto& ray_map = get_ray_map<D>();
    Bitboard ray_mask = ray_map[sq] & b.occupied;
    if (ray_mask) {
        Square first = pop_next<D>(ray_mask);
        Bitboard first_mask = get_mask(first);
        if ((first_mask & b.sides[enemy_side]) && is_relevant_sliding_piece<D>(b.piece_map[first])) {
            return true;
        }
    }

    return false;
}

template <Side S>
inline bool is_attacked_by_slider(const Board& b, Square sq) {
    return (
        is_attacked_by_slider<S, NORTH>    (b, sq) ||
        is_attacked_by_slider<S, SOUTH>    (b, sq) ||
        is_attacked_by_slider<S, EAST>     (b, sq) ||
        is_attacked_by_slider<S, WEST>     (b, sq) ||
        is_attacked_by_slider<S, NORTHEAST>(b, sq) ||
        is_attacked_by_slider<S, NORTHWEST>(b, sq) ||
        is_attacked_by_slider<S, SOUTHEAST>(b, sq) ||
        is_attacked_by_slider<S, SOUTHWEST>(b, sq)
    );
}

template <Side S, Piece P, MoveGenMode M>
inline void generate_piece_moves(Board& b, MoveList& moves, CheckInfo& check_info) {
    constexpr Side enemy_side = opposite_side(S);
    Bitboard pieces = b.pieces[S][P];
    Bitboard friendly_pieces = b.sides[S];
    Bitboard enemy_pieces = b.sides[enemy_side];
    Bitboard empty = ~b.occupied;

    while (pieces) {
        Square from = pop_lsb(pieces);

        Bitboard attack_mask = get_piece_attacks<P>(from, b.occupied);
        if constexpr (P == KING) {
            attack_mask &= ~check_info.unsafe;
        }
        attack_mask &= ~friendly_pieces;

        if constexpr (P != KING) {
            attack_mask &= check_info.must_cover;
        }

        if (check_info.pinned & get_mask(from)) {
            attack_mask &= check_info.pins[from];
        }

        if constexpr (M == MoveGenMode::QuietOnly || M == MoveGenMode::All) {
            Bitboard quiet_moves = attack_mask & empty;
            while (quiet_moves) {
                Square to = pop_lsb(quiet_moves);
                moves.add(Move(from, to, MoveType::Quiet, MoveFlag::Normal));
            }
        }

        if constexpr (M == MoveGenMode::TacticalOnly || M == MoveGenMode::All) {
            Bitboard captures = attack_mask & enemy_pieces;
            while (captures) {
                Square to = pop_lsb(captures);

                if constexpr (P == KING) {
                    // If the move is a capture by the king, then we need to recompute
                    // enemy sliding attacks to see if an x-ray opened up.
                    Bitboard from_mask = get_mask(from);
                    b.occupied ^= from_mask;
                    bool is_attacked = is_attacked_by_slider<S>(b, to);
                    b.occupied ^= from_mask;
                    if (is_attacked) continue;
                }

                moves.add(Move(from, to, MoveType::Capture, MoveFlag::Normal));
            }
        }
    }
}
