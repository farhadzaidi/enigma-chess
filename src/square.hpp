#pragma once

#include "types.hpp"

constexpr int BOARD_SIZE = 8;

constexpr Side opposite_side(Side side) { return side ^ 1; }
constexpr Square flip_square(Square square) { return square ^ 56; }
constexpr Square get_square(int rank, int file) { return rank * BOARD_SIZE + file; }
constexpr int get_rank(Square square) { return square / BOARD_SIZE; }
constexpr int get_file(Square square) { return square % BOARD_SIZE; }
