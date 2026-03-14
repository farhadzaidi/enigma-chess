#pragma once

#include <array>

#include "types.hpp"

constexpr int MAX_MOVES = 256;

// Each move is represented by a 16 bit unsigned integer
// Bits 1-6 indicate the origin square (0-63)
// Bits 7-12 indicate the destination square (0-63)
// Bits 13 indicates whether the move type (quiet or capture)
// Bits 14-16 indicate the move flag (special move)

class Move {
public:
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
    constexpr Piece promoted_piece() const {
        switch (flag()) {
            case MF_PROMO_BISHOP: return BISHOP;
            case MF_PROMO_KNIGHT: return KNIGHT;
            case MF_PROMO_ROOK: return ROOK;
            case MF_PROMO_QUEEN: return QUEEN;
            default: return NO_PIECE;
        }
    }

    // Comparison operators
    constexpr bool operator==(const Move& other) const { return move == other.move; }
    constexpr bool operator!=(const Move& other) const { return move != other.move; }
};

// Null move sentinel value
constexpr Move NULL_MOVE = Move();

// --- MoveList ---

/** Stack-allocated list of moves with a fixed upper bound */
class MoveList {
public:
    /** Append a move to the end of the list */
    void add(Move move) {
        moves_[size_] = move;
        size_++;
    }

    /** Remove and return the last move, or NULL_MOVE if empty */
    Move pop() {
        if (size_ == 0) return NULL_MOVE;

        Move to_return = moves_[size_ - 1];
        size_--;
        return to_return;
    }

    bool is_empty() const {
        return size_ == 0;
    }

    int size() const {
        return size_;
    }

    // Iterator support
    Move* begin() { return moves_.data(); }
    Move* end() { return moves_.data() + size_; }
    const Move* begin() const { return moves_.data(); }
    const Move* end() const { return moves_.data() + size_; }

    // Enable indexing using brackets
    Move& operator[](int index) {
        return moves_[index];
    }

    const Move& operator[](int index) const {
        return moves_[index];
    }

private:
    std::array<Move, MAX_MOVES> moves_;
    int size_ = 0;
};
