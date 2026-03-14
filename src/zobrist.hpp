#pragma once

#include <array>

#include "board.hpp"
#include "types.hpp"

constexpr int CASTLING_RIGHTS_COMBINATIONS = 16;
constexpr int EN_PASSANT_TARGET_FILES = 8;

using ZobristPieces = std::array<std::array<std::array<ZobristHash, NUM_SQUARES>, NUM_PIECES>, NUM_SIDES>;
using ZobristCastlingRights = std::array<ZobristHash, CASTLING_RIGHTS_COMBINATIONS>;
using ZobristEnPassantTargets = std::array<ZobristHash, EN_PASSANT_TARGET_FILES>;

/** Random hashes for each (side, piece, square) combination */
extern const ZobristPieces ZOBRIST_PIECES;
/** Random hashes for each of the 16 castling-rights bitmask values */
extern const ZobristCastlingRights ZOBRIST_CASTLING_RIGHTS;
/** Random hashes for each en-passant target file */
extern const ZobristEnPassantTargets ZOBRIST_EN_PASSANT_TARGETS;
/** Random hash XOR-ed in when it is black's turn to move */
extern const ZobristHash ZOBRIST_SIDE_TO_MOVE;
