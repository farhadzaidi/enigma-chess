#pragma once

#include <array>
#include <bit>

#include "types.hpp"
#include "bitboard.hpp"
#include "board/board.hpp"
#include "precompute/rays.hpp"
#include "move_generator/attacks.hpp"

// Computed at the start of move generation at every node
// Useful for determining legal moves efficienctly
struct CheckInfo {
    Bitboard pinned = 0ULL;
    std::array<Bitboard, NUM_SQUARES> pins;
    Bitboard checkers = 0ULL;
    Bitboard must_cover = ~0ULL; // By default, there is no square that must be covered
    Bitboard unsafe = 0ULL;

    // Main function that calls helpers to compute CheckInfo
    template <Side S>
    inline void compute_check_info(Board& b) {
        constexpr Side friendly_side = S;
        constexpr Side enemy_side = opposite_side(S);
        Square king_sq = b.king_squares[friendly_side];
        auto& enemy_pieces = b.pieces[enemy_side];

        // Checks from sliding pieces
        compute_sliding_checks_and_pins<S, NORTH>    (b, king_sq);
        compute_sliding_checks_and_pins<S, SOUTH>    (b, king_sq);
        compute_sliding_checks_and_pins<S, EAST>     (b, king_sq);
        compute_sliding_checks_and_pins<S, WEST>     (b, king_sq);
        compute_sliding_checks_and_pins<S, NORTHEAST>(b, king_sq);
        compute_sliding_checks_and_pins<S, NORTHWEST>(b, king_sq);
        compute_sliding_checks_and_pins<S, SOUTHEAST>(b, king_sq);
        compute_sliding_checks_and_pins<S, SOUTHWEST>(b, king_sq);

        // Checks from nonsliding pieces
        checkers |= PAWN_ATTACK_MAPS[enemy_side][king_sq] & enemy_pieces[PAWN];
        checkers |= KNIGHT_ATTACK_MAP[king_sq] & enemy_pieces[KNIGHT];
        checkers |= KING_ATTACK_MAP[king_sq] & enemy_pieces[KING];

        // Get all squares attacked by the enemy pieces
        unsafe =
            compute_attack_mask<S, PAWN>  (b) |
            compute_attack_mask<S, BISHOP>(b) |
            compute_attack_mask<S, KNIGHT>(b) |
            compute_attack_mask<S, ROOK>  (b) |
            compute_attack_mask<S, QUEEN> (b) |
            compute_attack_mask<S, KING>  (b);

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
    template <Side S, Direction D>
    inline void compute_sliding_checks_and_pins(
        const Board& b,
        Square king_sq
    ) {
        constexpr auto& ray_map = get_ray_map<D>();
        constexpr Side friendly_side = S;
        constexpr Side enemy_side = opposite_side(S);
        Bitboard enemy_pieces = b.sides[enemy_side];

        // Check if there is a piece in the ray
        Bitboard ray_mask = ray_map[king_sq] & b.occupied;
        if (ray_mask) {
            Square first = pop_next<D>(ray_mask);
            Bitboard first_mask = get_mask(first);

            if (first_mask & b.sides[friendly_side]) {
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
    template <Side S, Piece P>
    inline Bitboard compute_attack_mask(Board& b) {
        constexpr Side friendly_side = S;
        constexpr Side enemy_side = opposite_side(S);
        Bitboard pieces = b.pieces[enemy_side][P];

        if constexpr (P == PAWN) {
            // Pawn attack mask will be computed by shifting enemy pawns
            constexpr Direction attack_right = S == WHITE ? SOUTHWEST : NORTHEAST;
            constexpr Direction attack_left  = S == WHITE ? SOUTHEAST : NORTHWEST;
            Bitboard enemy_pawns = b.pieces[enemy_side][PAWN];
            return shift<attack_right>(enemy_pawns) | shift<attack_left>(enemy_pawns);
        } else {
            // Remove our king from the occupied bitboard prior to computing attack
            // masks to enable x-rays. For example, if a rook on a8 is targeting the
            // king on a2, the king shouldn't be able to "escape" to a1.
            b.occupied ^= b.pieces[friendly_side][KING];

            Bitboard attack_mask = 0ULL;
            while (pieces) {
                Square from = pop_lsb(pieces);
                if constexpr (P == BISHOP || P == ROOK) {
                    attack_mask |= generate_sliding_attack_mask<P>(from, b.occupied);
                } else if constexpr (P == QUEEN) {
                    attack_mask |=
                        generate_sliding_attack_mask<BISHOP>(from, b.occupied) |
                        generate_sliding_attack_mask<ROOK>(from, b.occupied);
                } else if constexpr (P == KNIGHT) {
                    attack_mask |= KNIGHT_ATTACK_MAP[from];
                } else if constexpr (P == KING) {
                    attack_mask |= KING_ATTACK_MAP[from];
                }
            }

            // Place the king back before returning
            b.occupied ^= b.pieces[friendly_side][KING];
            return attack_mask;
        }
    }
};
