#pragma once

#include "core/types.hpp"
#include "core/move.hpp"
#include "board/board.hpp"
#include "move_generator/check_info.hpp"
#include "move_generator/piece_moves.hpp"
#include "move_generator/pawn_moves.hpp"
#include "move_generator/castling.hpp"

template <Side S, MoveGenMode M>
inline void generate_moves_impl(Board& b, MoveList& moves, CheckInfo& check_info) {
    // Double check — only king moves are legal
    if (std::popcount(check_info.checkers) == 2) {
        generate_piece_moves<S, KING, M>(b, moves, check_info);
        return;
    }

    if constexpr (M == MoveGenMode::QuietOnly || M == MoveGenMode::All) {
        generate_castling_moves<S>(b, moves, check_info);
    }

    generate_pawn_moves<S, M>(b, moves, check_info);

    generate_piece_moves<S, BISHOP, M>(b, moves, check_info);
    generate_piece_moves<S, KNIGHT, M>(b, moves, check_info);
    generate_piece_moves<S, ROOK,   M>(b, moves, check_info);
    generate_piece_moves<S, QUEEN,  M>(b, moves, check_info);
    generate_piece_moves<S, KING,   M>(b, moves, check_info);
}

template <MoveGenMode M>
inline MoveList generate_moves(Board& b) {
    MoveList moves;
    CheckInfo check_info;

    if (b.to_move == WHITE) {
        check_info.compute_check_info<WHITE>(b);
        generate_moves_impl<WHITE, M>(b, moves, check_info);
    } else {
        check_info.compute_check_info<BLACK>(b);
        generate_moves_impl<BLACK, M>(b, moves, check_info);
    }

    return moves;
}
