#pragma once

#include <array>

#include "types.hpp"
#include "random.hpp"

constexpr int CASTLING_RIGHTS_COMBINATIONS = 16;
constexpr int EN_PASSANT_TARGET_FILES = 8;

using ZobristPieces = std::array<std::array<std::array<ZobristHash, NUM_SQUARES>, NUM_PIECES>, NUM_COLORS>;
using ZobristCastlingRights = std::array<ZobristHash, CASTLING_RIGHTS_COMBINATIONS>;
using ZobristEnPassantTargets = std::array<ZobristHash, EN_PASSANT_TARGET_FILES>;

inline const ZobristPieces ZOBRIST_PIECES = []() {
    ZobristPieces ZOBRIST_PIECES;
    for (int i = 0; i < NUM_COLORS; i++) {
        for (int j = 0; j < NUM_PIECES; j++) {
            for (int k = 0; k < NUM_SQUARES; k++) {
                ZOBRIST_PIECES[i][j][k] = prandom_u64();
            }
        }
    }

    return ZOBRIST_PIECES;
}();

inline const ZobristCastlingRights ZOBRIST_CASTLING_RIGHTS = []() {
    ZobristCastlingRights ZOBRIST_CASTLING_RIGHTS;
    for (int i = 0; i < CASTLING_RIGHTS_COMBINATIONS; i++) {
        ZOBRIST_CASTLING_RIGHTS[i] = prandom_u64();
    }

    return ZOBRIST_CASTLING_RIGHTS;
}();

inline const ZobristEnPassantTargets ZOBRIST_EN_PASSANT_TARGETS = []() {
    ZobristEnPassantTargets ZOBRIST_EN_PASSANT_TARGETS;
    for (int i = 0; i < EN_PASSANT_TARGET_FILES; i++) {
        ZOBRIST_EN_PASSANT_TARGETS[i] = prandom_u64();
    }

    return ZOBRIST_EN_PASSANT_TARGETS;
}();

inline const ZobristHash ZOBRIST_SIDE_TO_MOVE = prandom_u64();
