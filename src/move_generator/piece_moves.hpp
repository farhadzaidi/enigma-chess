#pragma once

#include "core/types.hpp"
#include "core/move.hpp"
#include "board/board.hpp"
#include "precompute/rays.hpp"
#include "move_generator/attacks.hpp"
#include "core/bitboard.hpp"
#include "move_generator/check_info.hpp"

template <Color C, Direction D>
inline bool is_attacked_by_slider(const Board &b, Square sq) {
    constexpr Color Them = opposite_color(C);
    constexpr auto& ray_map = get_ray_map<D>();
    Bitboard ray_mask = ray_map[sq] & b.occupied;
    if (ray_mask) {
        Square first = pop_next<D>(ray_mask);
        Bitboard first_mask = get_mask(first);
        if ((first_mask & b.colors[Them]) && is_relevant_sliding_piece<D>(b.piece_map[first])) {
            return true;
        }
    }

    return false;
}

template <Color C>
inline bool is_attacked_by_slider(const Board &b, Square sq) {
    return (
        is_attacked_by_slider<C, NORTH>    (b, sq) ||
        is_attacked_by_slider<C, SOUTH>    (b, sq) ||
        is_attacked_by_slider<C, EAST>     (b, sq) ||
        is_attacked_by_slider<C, WEST>     (b, sq) ||
        is_attacked_by_slider<C, NORTHEAST>(b, sq) ||
        is_attacked_by_slider<C, NORTHWEST>(b, sq) ||
        is_attacked_by_slider<C, SOUTHEAST>(b, sq) ||
        is_attacked_by_slider<C, SOUTHWEST>(b, sq)
    );
}

template <Color C, Piece P, MoveGenMode M>
inline void generate_piece_moves(Board& b, MoveList& moves, CheckInfo& checkInfo) {
    constexpr Color Them = opposite_color(C);
    Bitboard piece_bb = b.pieces[C][P];
    Bitboard us = b.colors[C];
    Bitboard them = b.colors[Them];
    Bitboard empty = ~b.occupied;

    while (piece_bb) {
        Square from = pop_lsb(piece_bb);

        Bitboard attack_mask = get_piece_attacks<P>(from, b.occupied);
        if constexpr (P == KING) {
            attack_mask &= ~checkInfo.unsafe;
        }
        attack_mask &= ~us;

        if constexpr (P != KING) {
            attack_mask &= checkInfo.must_cover;
        }

        if (checkInfo.pinned & get_mask(from)) {
            attack_mask &= checkInfo.pins[from];
        }

        if constexpr (M == MoveGenMode::QuietOnly || M == MoveGenMode::All) {
            Bitboard quiet_moves = attack_mask & empty;
            while (quiet_moves) {
                Square to = pop_lsb(quiet_moves);
                moves.add(Move(from, to, MoveType::Quiet, MoveFlag::Normal));
            }
        }

        if constexpr (M == MoveGenMode::TacticalOnly || M == MoveGenMode::All) {
            Bitboard captures = attack_mask & them;
            while (captures) {
                Square to = pop_lsb(captures);

                if constexpr (P == KING) {
                    // If the move is a capture by the king, then we need to recompute
                    // enemy sliding attacks to see if an x-ray opened up.
                    Bitboard from_mask = get_mask(from);
                    b.occupied ^= from_mask;
                    bool is_attacked = is_attacked_by_slider<C>(b, to);
                    b.occupied ^= from_mask;
                    if (is_attacked) continue;
                }

                moves.add(Move(from, to, MoveType::Capture, MoveFlag::Normal));
            }
        }
    }
}
