#pragma once

#include "precompute/attacks.hpp"

// Sliding attack mask lookup using precomputed magic bitboard tables.
// Takes an explicit occupancy bitboard so it can be used with modified occupancy (e.g. SEE).
template <Piece P>
inline Bitboard generate_sliding_attack_mask(Square from, Bitboard occupied) {
    constexpr auto& attack_table = P == BISHOP ? BISHOP_ATTACK_TABLE : ROOK_ATTACK_TABLE;
    constexpr auto& blocker_map  = P == BISHOP ? BISHOP_BLOCKER_MAP : ROOK_BLOCKER_MAP;
    constexpr auto& magic        = P == BISHOP ? BISHOP_MAGIC : ROOK_MAGIC;
    constexpr auto& offset       = P == BISHOP ? BISHOP_OFFSET : ROOK_OFFSET;

    Bitboard blocker_mask = blocker_map[from];
    Bitboard blockers = occupied & blocker_mask;
    size_t index = get_attack_table_index(blockers, blocker_mask, magic[from]);
    return attack_table[offset[from] + index];
}

// Compile-time piece dispatch
template <Piece P>
inline Bitboard get_piece_attacks(Square from, Bitboard occupied) {
    if constexpr (P == KNIGHT) return KNIGHT_ATTACK_MAP[from];
    else if constexpr (P == KING)   return KING_ATTACK_MAP[from];
    else if constexpr (P == BISHOP) return generate_sliding_attack_mask<BISHOP>(from, occupied);
    else if constexpr (P == ROOK)   return generate_sliding_attack_mask<ROOK>(from, occupied);
    else if constexpr (P == QUEEN)  return generate_sliding_attack_mask<BISHOP>(from, occupied) |
                                           generate_sliding_attack_mask<ROOK>(from, occupied);
    else return EMPTY_BITBOARD;
}

// Runtime piece dispatch
inline Bitboard get_piece_attacks(Piece piece, Square from, Bitboard occupied) {
    switch (piece) {
        case KNIGHT: return get_piece_attacks<KNIGHT>(from, occupied);
        case KING:   return get_piece_attacks<KING>(from, occupied);
        case BISHOP: return get_piece_attacks<BISHOP>(from, occupied);
        case ROOK:   return get_piece_attacks<ROOK>(from, occupied);
        case QUEEN:  return get_piece_attacks<QUEEN>(from, occupied);
        default:     return EMPTY_BITBOARD;
    }
}
