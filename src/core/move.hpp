#pragma once

#include <array>

#include "core/types.hpp"
#include "core/constants.hpp"

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
        : move((from) | (to << 6)
            | (static_cast<uint16_t>(type) << 12)
            | (static_cast<uint16_t>(flag) << 13)) {}

    // Member functions
    constexpr Square from() const { return move & 63; }
    constexpr Square to() const { return (move >> 6) & 63; }
    constexpr MoveType type() const { return static_cast<MoveType>((move >> 12) & 1); }
    constexpr MoveFlag flag() const { return static_cast<MoveFlag>((move >> 13) & 7); }
    constexpr bool is_promotion() const {
        return flag() == MoveFlag::PromoBishop
            || flag() == MoveFlag::PromoKnight
            || flag() == MoveFlag::PromoRook
            || flag() == MoveFlag::PromoQueen;
    }

    // Comparison operators
    constexpr bool operator==(const Move& other) const { return move == other.move; }
    constexpr bool operator!=(const Move& other) const { return move != other.move; }
};

// Null move sentinel value
constexpr Move NULL_MOVE = Move();

inline Piece get_promoted_piece(MoveFlag flag) {
    switch (flag) {
        case MoveFlag::PromoBishop: return BISHOP;
        case MoveFlag::PromoKnight: return KNIGHT;
        case MoveFlag::PromoRook:   return ROOK;
        case MoveFlag::PromoQueen:  return QUEEN;
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
