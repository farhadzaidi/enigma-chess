#pragma once

#include <array>

#include "types.hpp"
#include "board.hpp"
#include "move.hpp"

// Forward declaration
struct CheckInfo;

struct MoveList {
    std::array<Move, MAX_MOVES> moves;
    int size = 0;

    void add(Move move) {
        moves[size] = move;
        size++;
    }

    Move pop() {
        if (size == 0) return NULL_MOVE;

        Move to_return = moves[size - 1];
        size--;
        return to_return;
    }

    bool is_empty() {
        return size == 0;
    }

    // Iterator support
    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + size; }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + size; }

    // Enable indexing using brackets
    Move& operator[](int index) {
        return moves[index];
    }

    const Move& operator[](int index) const {
        return moves[index];
    }
};

// Expose this function since it's used in board.cpp as well
template <Piece P>
inline Bitboard generate_sliding_attack_mask(const Board& b, Square from) {
    // Assign constants based on sliding piece type
    constexpr auto& attack_table = P == BISHOP ? BISHOP_ATTACK_TABLE : ROOK_ATTACK_TABLE; 
    constexpr auto& blocker_map  = P == BISHOP ? BISHOP_BLOCKER_MAP : ROOK_BLOCKER_MAP;
    constexpr auto& magic        = P == BISHOP ? BISHOP_MAGIC : ROOK_MAGIC;
    constexpr auto& offset       = P == BISHOP ? BISHOP_OFFSET : ROOK_OFFSET;
        
    // Look up sliding piece attacks from attack table based on blocker pattern
    Bitboard blocker_mask = blocker_map[from];
    Bitboard blockers = b.occupied & blocker_mask;
    size_t index = get_attack_table_index(blockers, blocker_mask, magic[from]);
    return attack_table[offset[from] + index];
}

// Generates moves into provided MoveList using precomputed CheckInfo
// Use this to avoid recomputing CheckInfo or reallocating MoveList across multiple calls
template <Color C, MoveGenMode M>
void generate_moves_impl(Board& b, MoveList& moves, CheckInfo& checkInfo);

// Convenience wrapper that computes CheckInfo and returns a new MoveList
template <MoveGenMode M>
MoveList generate_moves(Board& b);