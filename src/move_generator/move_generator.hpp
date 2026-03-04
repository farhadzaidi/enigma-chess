#pragma once

#include "core/types.hpp"
#include "core/move.hpp"
#include "board/board.hpp"
#include "move_generator/check_info.hpp"
#include "move_generator/piece_moves.hpp"
#include "move_generator/pawn_moves.hpp"
#include "move_generator/castling.hpp"

template <Color C, MoveGenMode M>
inline void generate_moves_impl(Board& b, MoveList& moves, CheckInfo& checkInfo) {
    // Double check — only king moves are legal
    if (std::popcount(checkInfo.checkers) == 2) {
        generate_piece_moves<C, KING, M>(b, moves, checkInfo);
        return;
    }

    if constexpr (M == MoveGenMode::QuietOnly || M == MoveGenMode::All) {
        generate_castling_moves<C>(b, moves, checkInfo);
    }

    generate_pawn_moves<C, M>(b, moves, checkInfo);

    generate_piece_moves<C, BISHOP, M>(b, moves, checkInfo);
    generate_piece_moves<C, KNIGHT, M>(b, moves, checkInfo);
    generate_piece_moves<C, ROOK,   M>(b, moves, checkInfo);
    generate_piece_moves<C, QUEEN,  M>(b, moves, checkInfo);
    generate_piece_moves<C, KING,   M>(b, moves, checkInfo);
}

template <MoveGenMode M>
inline MoveList generate_moves(Board &b) {
    MoveList moves;
    CheckInfo checkInfo;

    if (b.to_move == WHITE) {
        checkInfo.compute_check_info<WHITE>(b);
        generate_moves_impl<WHITE, M>(b, moves, checkInfo);
    } else {
        checkInfo.compute_check_info<BLACK>(b);
        generate_moves_impl<BLACK, M>(b, moves, checkInfo);
    }

    return moves;
}
