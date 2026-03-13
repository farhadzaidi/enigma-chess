#pragma once

#include <bit>

#include "types.hpp"
#include "move.hpp"
#include "board/board.hpp"
#include "bitboard.hpp"
#include "move_generator/check_info.hpp"

namespace {

// constants
constexpr Bitboard WHITE_LONG_CASTLE_PATH   = 0x000000000000000EULL;
constexpr Bitboard WHITE_SHORT_CASTLE_PATH  = 0x0000000000000060ULL;
constexpr Bitboard BLACK_LONG_CASTLE_PATH   = 0x0E00000000000000ULL;
constexpr Bitboard BLACK_SHORT_CASTLE_PATH  = 0x6000000000000000ULL;

} // namespace


template <Side S>
inline void generate_castling_moves(Board& b, MoveList& moves, CheckInfo& check_info) {
    constexpr auto SHORT_CASTLING_RIGHTS = S == WHITE ? WHITE_SHORT : BLACK_SHORT;
    constexpr auto LONG_CASTLING_RIGHTS  = S == WHITE ? WHITE_LONG : BLACK_LONG;
    constexpr auto SHORT_CASTLE_PATH     = S == WHITE ? WHITE_SHORT_CASTLE_PATH : BLACK_SHORT_CASTLE_PATH;
    constexpr auto LONG_CASTLE_PATH      = S == WHITE ? WHITE_LONG_CASTLE_PATH : BLACK_LONG_CASTLE_PATH;
    constexpr auto SHORT_TO              = S == WHITE ? G1 : G8;
    constexpr auto LONG_TO               = S == WHITE ? C1 : C8;
    constexpr auto KING_SQUARE           = S == WHITE ? E1 : E8;

    // Castle path squares (that king walks over)
    constexpr auto F_SQUARE = S == WHITE ? F1 : F8;
    constexpr auto G_SQUARE = S == WHITE ? G1 : G8;

    constexpr auto D_SQUARE = S == WHITE ? D1 : D8;
    constexpr auto C_SQUARE = S == WHITE ? C1 : C8;

    if (std::popcount(check_info.checkers) != 0) return;

    // Compute the path that the king walks over (not the full path in the case of a long castle)
    Bitboard king_short_castle_path = get_mask(F_SQUARE) | get_mask(G_SQUARE);
    Bitboard king_long_castle_path = get_mask(D_SQUARE) | get_mask(C_SQUARE);

    // Short castle
    if (
        (b.castling_rights & SHORT_CASTLING_RIGHTS) // Have castling rights for this side
        && ((b.occupied & SHORT_CASTLE_PATH) == 0)  // Path is clear
        && ((king_short_castle_path & check_info.unsafe) == 0) // King doesn't pass thru check
    ) {
        moves.add(Move(KING_SQUARE, SHORT_TO, MT_QUIET, MF_CASTLE));
    }

    // Long castle
    if (
        (b.castling_rights & LONG_CASTLING_RIGHTS)
        && ((b.occupied & LONG_CASTLE_PATH) == 0)
        && ((king_long_castle_path & check_info.unsafe) == 0)
    ) {
        moves.add(Move(KING_SQUARE, LONG_TO, MT_QUIET, MF_CASTLE));
    }
}
