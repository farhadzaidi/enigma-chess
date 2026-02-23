#pragma once

#include "types.hpp"

// Each move is represented by a 16 bit unsigned integer
// Bits 1-6 indicate the origin square (0-63)
// Bits 7-12 indicate the destination square (0-63)
// Bits 13 indicates whether the move type (quiet or capture)
// Bits 14-16 indicate the move flag (special move)

struct Move {
    uint16_t move;
    // bool is_killer = false;

    constexpr Move() : move(0) {}

    // Encodes move as an unsigned 16 bit int
    constexpr Move(Square from, Square to, MoveType type, MoveFlag flag)
        : move((from) | (to << 6) | (type << 12) | (flag << 13)) {}
    
    // Member functions
    constexpr Square from() const { return move & 63; }
    constexpr Square to() const { return (move >> 6) & 63; }
    constexpr MoveType type() const { return (move >> 12) & 1; }
    constexpr MoveFlag flag() const { return (move >> 13) & 7; }
    constexpr bool is_promotion() const { return flag() >= 3; }

    // Comparison operators
    constexpr bool operator==(const Move& other) const { return move == other.move; }
    constexpr bool operator!=(const Move& other) const { return move != other.move; }
};

// Null move sentinel value
constexpr Move NULL_MOVE = Move();

// Killer move type definition
using KillerMove = std::array<Move, MAX_SEARCH_PLY>;
