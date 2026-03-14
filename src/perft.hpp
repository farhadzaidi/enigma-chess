#pragma once

#include <cstdint>
#include <iostream>

#include "types.hpp"
#include "board.hpp"
#include "move.hpp"
#include "move_generator.hpp"
#include "notation.hpp"

/** Phased perft: count leaf nodes using separate quiet/tactical generation */
inline uint64_t perft_phased(Board& b, SearchDepth depth) {
    uint64_t nodes = 0;

    MoveGenerator move_generator(b);
    MoveList quiet_moves = move_generator.generate_quiets();
    MoveList captures = move_generator.generate_tacticals();

    if (depth == 1) {
        return quiet_moves.size() + captures.size();
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

/** Standard perft node counter; prints per-move breakdowns from the root node */
template<bool Root>
inline uint64_t perft(Board& b, SearchDepth depth) {
    uint64_t nodes = 0;
    uint64_t total_nodes = 0;
    MoveGenerator move_generator(b);
    MoveList moves = move_generator.generate_all();

    // No need to make moves, just return the count
    if (depth == 1) {
        return moves.size();
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
