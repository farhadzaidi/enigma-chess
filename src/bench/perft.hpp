#pragma once

#include <iostream>

#include "board/board.hpp"
#include "core/move.hpp"
#include "move_generator/move_generator.hpp"
#include "move_generator/check_info.hpp"
#include "utils/notation.hpp"

template<bool Root>
inline uint64_t perft(Board& b, SearchDepth depth) {
    uint64_t nodes = 0;
    uint64_t total_nodes = 0;
    MoveList moves = generate_moves<MoveGenMode::All>(b);

    // No need to make moves, just return the count
    if (depth == 1) {
        return moves.size;
    }

    for (Move move: moves) {
        b.make_move(move);
        nodes = perft<false>(b, depth - 1);
        total_nodes += nodes;
        b.unmake_move(move);

        if (Root) {
            std::clog << decode_move_to_uci(move) << ": " << nodes << "\n";
        }

    }

    if (Root) {
        std::clog << "\nNodes searched: " << total_nodes << "\n";
    }

    return total_nodes;
}

// Forward declaration
inline uint64_t perft_phased(Board& b, SearchDepth depth);

template <Side S>
inline uint64_t _perft_phased(Board& b, SearchDepth depth) {
    uint64_t nodes = 0;


    MoveList quiet_moves;
    MoveList captures;

    CheckInfo check_info;
    check_info.compute_check_info<S>(b);

    generate_moves_impl<S, MoveGenMode::QuietOnly>(b, quiet_moves, check_info);
    generate_moves_impl<S, MoveGenMode::TacticalOnly>(b, captures, check_info);

    if (depth == 1) {
        return quiet_moves.size + captures.size;
    }

    for (Move move: quiet_moves) {
        b.make_move(move);
        nodes += perft_phased(b, depth - 1);
        b.unmake_move(move);
    }

    for (Move move: captures) {
        b.make_move(move);
        nodes += perft_phased(b, depth - 1);
        b.unmake_move(move);
    }

    return nodes;
}

inline uint64_t perft_phased(Board& b, SearchDepth depth) {
    return b.to_move == WHITE
        ? _perft_phased<WHITE>(b, depth)
        : _perft_phased<BLACK>(b, depth);
}
