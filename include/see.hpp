#pragma once

#include <array>
#include <algorithm>

#include "types.hpp"
#include "board.hpp"

struct Attacker {
    Piece piece;
    Square square;
};

// Simple piece values for SEE
constexpr std::array<int, NUM_PIECES> SEE_PIECE_VALUES = {100, 300, 325, 500, 900, 0}; 

// Upper bound for the maximum number of captures
constexpr int MAX_CAPTURES = 32;

inline Attacker get_least_valuable_attacker(Board& b, Color side, Bitboard attackers) {
    for (Piece p = PAWN; p < NUM_PIECES; p++) {
        Bitboard from_mask = attackers & b.pieces[side][p];
        if (from_mask) return {p, get_lsb(from_mask)};
    }

    return {NO_PIECE, NO_SQUARE};
}

inline int see(Board& b, Move move) {
    // Note: Promotions are intentionally not handled. The material investment for a
    // promotion capture is the pawn, not the promoted piece. Treating the promoted
    // piece as a queen would overestimate the loss when it gets recaptured (e.g. a
    // pawn promoting and capturing a rook, then getting recaptured, is +rook -pawn,
    // not +rook -queen).
    
    // Note: Pinned pieces are not filtered from the attackers bitboard. A pinned
    // piece may be counted as an attacker/defender even though it can't legally
    // move. Not worth the complexity given the rarity and that SEE is a heuristic.

    std::array<int, MAX_CAPTURES> gain;
    Square target_sq = move.to();
    Color side = b.to_move;
    Bitboard occupied = b.occupied;
    Bitboard attackers = EMPTY_BITBOARD;
    Piece last_attacker = NO_PIECE;
    int depth = 0;

    // Force initial capture
    gain[0] = move.flag() == EN_PASSANT ? SEE_PIECE_VALUES[PAWN] : SEE_PIECE_VALUES[b.piece_map[target_sq]];
    last_attacker = b.piece_map[move.from()];

    occupied ^= get_mask(move.from());
    if (move.flag() == EN_PASSANT) {
        Square cap_sq = b.to_move == WHITE ? target_sq + SOUTH : target_sq + NORTH;
        occupied ^= get_mask(cap_sq);
        occupied |= get_mask(target_sq);
    }
    side ^= 1;
    depth++;

    // Recapture from both sides until one side can't anymore
    // Keep track of relative capture scores
    while ((attackers = b.attackers_to(target_sq, occupied)) != EMPTY_BITBOARD) {
        Attacker attacker = get_least_valuable_attacker(b, side, attackers);

        // No more attackers
        if (attacker.piece == NO_PIECE) break;

        // If our attacker is the king and they still have attackers left, then
        // we can't continue the exchange since the king would be in check
        if (attacker.piece == KING && (attackers & b.colors[side ^ 1])) break;
        
        int attacker_gain = SEE_PIECE_VALUES[last_attacker];
        gain[depth] = (attacker_gain - gain[depth - 1]);

        // Simulate capture and toggle side
        last_attacker = attacker.piece;
        occupied ^= get_mask(attacker.square);
        side ^= 1;
        depth++;
    }

    // Now we do a backward pass to correct scores based on if we should've
    // chosen not to capture at each step
    while (depth > 1) {
        // Decrement at the start since depth doesn't point to any entry
        // after the previous loop
        depth--;

        // This is our score if we choose to continue the exchange (capture)
        int continue_exchange = gain[depth];

        // This is our score if we choose to stop here
        // This is essentially whatever outcome the other side had,
        // negated to match our perspective
        int stop_here = -gain[depth - 1];

        // The previous entry (the other side's best outcome) depends
        // on what we choose to do here
        // We negate to match their perspective
        gain[depth - 1] = -std::max(continue_exchange, stop_here);
    }

    return gain[0];
}
