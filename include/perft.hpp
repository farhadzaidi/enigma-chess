#pragma once

#include <iostream>

#include "board.hpp"
#include "utils.hpp"
#include "move.hpp"
#include "move_generator.hpp"
#include "check_info.hpp"

template<bool Root>
inline uint64_t perft(Board &b, SearchDepth depth) {
    uint64_t nodes = 0;
    uint64_t total_nodes = 0;
    MoveList moves = generate_moves<ALL>(b);

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

template <Color C>
inline uint64_t _perft_phased(Board &b, SearchDepth depth) {
    uint64_t nodes = 0;


    MoveList quiet_moves; 
    MoveList captures;
    
    CheckInfo checkInfo;
    checkInfo.compute_check_info<C>(b);
    
    generate_moves_impl<C, QUIET_ONLY>(b, quiet_moves, checkInfo);
    generate_moves_impl<C, CAPTURES_AND_PROMOTIONS>(b, captures, checkInfo);

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

inline uint64_t perft_phased(Board&b, SearchDepth depth) {
    return b.to_move == WHITE
        ? _perft_phased<WHITE>(b, depth)
        : _perft_phased<BLACK>(b, depth);
}