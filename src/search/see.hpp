#pragma once

#include <array>
#include <algorithm>

#include "core/types.hpp"
#include "board/board.hpp"

namespace {

struct Attacker {
    Piece piece;
    Square square;
};

// constants
constexpr int MAX_CAPTURES = 32;

// functions
inline Attacker get_least_valuable_attacker(Board& b, Side side, Bitboard attackers) {
    for (Piece piece = PAWN; piece < NUM_PIECES; piece++) {
        Bitboard from_mask = attackers & b.pieces[side][piece];
        if (from_mask) return {piece, get_lsb(from_mask)};
    }

    return {NO_PIECE, NO_SQUARE};
}

} // namespace


constexpr std::array<int, NUM_PIECES> SEE_PIECE_VALUES = {100, 300, 325, 500, 900, 0};

inline int see(Board& b, Move move) {
    // Note: Promotions are intentionally not handled. The material investment for a
    // promotion capture is the pawn, not the promoted piece. Treating the promoted
    // piece as a queen would overestimate the loss when it gets recaptured (e.g. a
    // pawn promoting and capturing a rook, then getting recaptured, is +rook -pawn,
    // not +rook -queen).

    // Note: Pinned pieces are not filtered from the attackers bitboard. A pinned
    // piece may be counted as an attacker/defender even though it can't legally
    // move. Not worth the complexity given the rarity and that SEE is a heuristic.

    std::array<int, MAX_CAPTURES> exchange_scores;
    Square target_sq = move.to();
    Side attacking_side = b.to_move;
    Bitboard occupied = b.occupied;
    Bitboard attackers = EMPTY_BITBOARD;
    Piece last_attacker_piece = NO_PIECE;
    int num_exchanges = 0;

    // Force initial capture
    exchange_scores[0] = move.flag() == MF_EN_PASSANT
        ? SEE_PIECE_VALUES[PAWN]
        : SEE_PIECE_VALUES[b.piece_map[target_sq]];
    last_attacker_piece = b.piece_map[move.from()];

    occupied ^= get_mask(move.from());
    if (move.flag() == MF_EN_PASSANT) {
        Square cap_sq = en_passant_capture_square(target_sq, b.to_move);
        occupied ^= get_mask(cap_sq);
        occupied |= get_mask(target_sq);
    }
    attacking_side = opposite_side(attacking_side);
    num_exchanges++;

    // Recapture from both sides until one side can't anymore
    while ((attackers = b.attackers_to(target_sq, occupied)) != EMPTY_BITBOARD) {
        Attacker attacker = get_least_valuable_attacker(b, attacking_side, attackers);

        if (attacker.piece == NO_PIECE) break;

        // Can't capture with the king if the opponent still has attackers
        if (attacker.piece == KING && (attackers & b.sides[opposite_side(attacking_side)])) break;

        int captured_value = SEE_PIECE_VALUES[last_attacker_piece];
        exchange_scores[num_exchanges] = captured_value - exchange_scores[num_exchanges - 1];

        // Simulate capture and toggle side
        last_attacker_piece = attacker.piece;
        occupied ^= get_mask(attacker.square);
        attacking_side = opposite_side(attacking_side);
        num_exchanges++;
    }

    // Backward pass: at each step, decide whether continuing the exchange is better
    // than stopping. Propagate the best choice back through the sequence.
    while (num_exchanges > 1) {
        num_exchanges--;

        int score_if_continue = exchange_scores[num_exchanges];
        int score_if_stop = -exchange_scores[num_exchanges - 1];

        exchange_scores[num_exchanges - 1] = -std::max(score_if_continue, score_if_stop);
    }

    return exchange_scores[0];
}
