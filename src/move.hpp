#pragma once

#include <array>

#include "types.hpp"
#include "constants.hpp"

// Each move is represented by a 16 bit unsigned integer
// Bits 1-6 indicate the origin square (0-63)
// Bits 7-12 indicate the destination square (0-63)
// Bits 13 indicates whether the move type (quiet or capture)
// Bits 14-16 indicate the move flag (special move)

struct Move {
    uint16_t move;

    constexpr Move() : move(0) {}

    // Encodes move as an unsigned 16 bit int
    constexpr Move(Square from, Square to, MoveType type, MoveFlag flag)
        : move((from) | (to << 6) | (type << 12) | (flag << 13)) {}

    // Member functions
    constexpr Square from() const { return move & 63; }
    constexpr Square to() const { return (move >> 6) & 63; }
    constexpr MoveType type() const { return (move >> 12) & 1; }
    constexpr MoveFlag flag() const { return (move >> 13) & 7; }
    constexpr bool is_promotion() const {
        return flag() == MF_PROMO_BISHOP
            || flag() == MF_PROMO_KNIGHT
            || flag() == MF_PROMO_ROOK
            || flag() == MF_PROMO_QUEEN;
    }

    // Comparison operators
    constexpr bool operator==(const Move& other) const { return move == other.move; }
    constexpr bool operator!=(const Move& other) const { return move != other.move; }
};

// Null move sentinel value
constexpr Move NULL_MOVE = Move();

inline Piece get_promoted_piece(MoveFlag flag) {
    switch (flag) {
        case MF_PROMO_BISHOP: return BISHOP;
        case MF_PROMO_KNIGHT: return KNIGHT;
        case MF_PROMO_ROOK:   return ROOK;
        case MF_PROMO_QUEEN:  return QUEEN;
        default: return NO_PIECE;
    }
}

// --- MoveList ---

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

    bool is_empty() const {
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
