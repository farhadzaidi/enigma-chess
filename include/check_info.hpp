#pragma once

#include <array>
#include <iostream>

#include "types.hpp"
#include "utils.hpp"
#include "board.hpp"
#include "precompute.hpp"

// Computed at the start of move generation at every node
// Useful for determining legal moves efficienctly
struct CheckInfo {
    Bitboard pinned = 0ULL;
    std::array<Bitboard, NUM_SQUARES> pins;
    Bitboard checkers = 0ULL;
    Bitboard must_cover = ~0ULL; // By default, there is no square that must be covered
    Bitboard unsafe = 0ULL;

    // Main function that calls helpers to compute CheckInfo
    template <Color C>
    inline void compute_check_info(Board& b) {
        constexpr Color us = C;
        constexpr Color them = C ^ 1;
        Square king_sq = b.king_squares[us];
        auto& enemy_pieces = b.pieces[them];

        // Checks from sliding pieces
        compute_sliding_checks_and_pins<C, NORTH>    (b, king_sq);
        compute_sliding_checks_and_pins<C, SOUTH>    (b, king_sq);
        compute_sliding_checks_and_pins<C, EAST>     (b, king_sq);
        compute_sliding_checks_and_pins<C, WEST>     (b, king_sq);
        compute_sliding_checks_and_pins<C, NORTHEAST>(b, king_sq);
        compute_sliding_checks_and_pins<C, NORTHWEST>(b, king_sq);
        compute_sliding_checks_and_pins<C, SOUTHEAST>(b, king_sq);
        compute_sliding_checks_and_pins<C, SOUTHWEST>(b, king_sq);

        // Checks from nonsliding pieces
        checkers |= PAWN_ATTACK_MAPS[them][king_sq] & enemy_pieces[PAWN];
        checkers |= KNIGHT_ATTACK_MAP[king_sq] & enemy_pieces[KNIGHT];
        checkers |= KING_ATTACK_MAP[king_sq] & enemy_pieces[KING];

        // Get all squares attacked by the enemy pieces
        unsafe =
            compute_attack_mask<C, PAWN>  (b) |
            compute_attack_mask<C, BISHOP>(b) |
            compute_attack_mask<C, KNIGHT>(b) |
            compute_attack_mask<C, ROOK>  (b) |
            compute_attack_mask<C, QUEEN> (b) |
            compute_attack_mask<C, KING>  (b);

        // If single check, we need to initialize must_cover based on whether the checker is
        // a sliding or nonsliding piece
        // Double check case is handled in move generation
        if (std::popcount(checkers) == 1) {
            Square checker_sq = get_lsb(checkers);
            Piece checker_piece = b.piece_map[checker_sq];

            // If checked by a sliding piece, then other pieces must either capture or block
            // If checked by a nonsliding piece, then other pieces must capture
            must_cover = is_slider(checker_piece)
                ? LINES[king_sq][checker_sq]
                : checkers;
        }
    }

private:
    // Helpers for computing CheckInfo attributes

    // Computes:
    // 1. Mask representing location of any pieces giving a check to the provided king square
    // 2. Mask representing pieces on the board that are pinned by sliding checkers
    // 3. Masks of pin lines for every pinned piece (line from pinned piece to checker, not including the pinned piece)
    template <Color C, Direction D>
    inline void compute_sliding_checks_and_pins(
        const Board &b,
        Square king_sq
    ) {
        constexpr auto& ray_map = get_ray_map<D>();
        constexpr Color us = C;
        constexpr Color them = C ^ 1;
        Bitboard enemy_pieces = b.colors[them];

        // Check if there is a piece in the ray
        Bitboard ray_mask = ray_map[king_sq] & b.occupied;
        if (ray_mask) {
            Square first = pop_next<D>(ray_mask);
            Bitboard first_mask = get_mask(first);

            if (first_mask & b.colors[us]) {
                // First piece is friendly, so we can get second to look for pins
                Square second = pop_next<D>(ray_mask);
                Bitboard second_mask = get_mask(second);

                if ((second_mask & enemy_pieces) && is_relevant_sliding_piece<D>(b.piece_map[second])) {
                    // Second piece is an enemy sliding piece (relevant), so first is pinned
                    pinned |= first_mask;
                    pins[first] = LINES[king_sq][second];
                }
            } else if ((first_mask & enemy_pieces) && is_relevant_sliding_piece<D>(b.piece_map[first])){
                // First piece is an enemy sliding piece (relevant), so second is a checker
                checkers |= first_mask;
            }
        }
    }

    // Computes attack masks for all enemy pieces
    template <Color C, Piece P>
    inline Bitboard compute_attack_mask(Board& b) {
        constexpr Color us = C;
        constexpr Color them = C ^ 1;
        Bitboard piece_bb = b.pieces[them][P];

        if constexpr (P == PAWN) {
            // Pawn attack mask will be computed by shifting enemy pawns
            constexpr Direction attack_right = C == WHITE ? SOUTHWEST : NORTHEAST;
            constexpr Direction attack_left  = C == WHITE ? SOUTHEAST : NORTHWEST;
            Bitboard enemy_pawns = b.pieces[them][PAWN];
            return shift<attack_right>(enemy_pawns) | shift<attack_left>(enemy_pawns);
        } else {
            // Remove our king from the occupied bitboard prior to computing attack
            // masks to enable x-rays. For example, if a rook on a8 is targeting the
            // king on a2, the king shouldn't be able to "escape" to a1.
            b.occupied ^= b.pieces[us][KING];

            Bitboard attack_mask = 0ULL;
            while (piece_bb) {
                Square from = pop_lsb(piece_bb);
                if constexpr (P == BISHOP || P == ROOK) {
                    attack_mask |= generate_sliding_attack_mask<P>(b, from);
                } else if constexpr (P == QUEEN) {
                    attack_mask |=
                        generate_sliding_attack_mask<BISHOP>(b, from) |
                        generate_sliding_attack_mask<ROOK>(b, from);
                } else if constexpr (P == KNIGHT) {
                    attack_mask |= KNIGHT_ATTACK_MAP[from];
                } else if constexpr (P == KING) {
                    attack_mask |= KING_ATTACK_MAP[from];
                }
            }

            // Place the king back before returning
            b.occupied ^= b.pieces[us][KING];
            return attack_mask;
        }
    }
};