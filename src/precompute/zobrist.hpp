#pragma once

#include <array>

#include "core/types.hpp"
#include "core/random.hpp"

constexpr int CASTLING_RIGHTS_COMBINATIONS = 16;
constexpr int EN_PASSANT_TARGET_FILES = 8;

using ZobristPieces = std::array<std::array<std::array<ZobristHash, NUM_SQUARES>, NUM_PIECES>, NUM_COLORS>;
using ZobristCastlingRights = std::array<ZobristHash, CASTLING_RIGHTS_COMBINATIONS>;
using ZobristEnPassantTargets = std::array<ZobristHash, EN_PASSANT_TARGET_FILES>;

inline const ZobristPieces ZOBRIST_PIECES = []() {
    ZobristPieces ZOBRIST_PIECES;
    for (int color = 0; color < NUM_COLORS; color++) {
        for (int piece = 0; piece < NUM_PIECES; piece++) {
            for (int sq = 0; sq < NUM_SQUARES; sq++) {
                ZOBRIST_PIECES[color][piece][sq] = prandom_u64();
            }
        }
    }

    return ZOBRIST_PIECES;
}();

inline const ZobristCastlingRights ZOBRIST_CASTLING_RIGHTS = []() {
    ZobristCastlingRights ZOBRIST_CASTLING_RIGHTS;
    for (int cr = 0; cr < CASTLING_RIGHTS_COMBINATIONS; cr++) {
        ZOBRIST_CASTLING_RIGHTS[cr] = prandom_u64();
    }

    return ZOBRIST_CASTLING_RIGHTS;
}();

inline const ZobristEnPassantTargets ZOBRIST_EN_PASSANT_TARGETS = []() {
    ZobristEnPassantTargets ZOBRIST_EN_PASSANT_TARGETS;
    for (int file = 0; file < EN_PASSANT_TARGET_FILES; file++) {
        ZOBRIST_EN_PASSANT_TARGETS[file] = prandom_u64();
    }

    return ZOBRIST_EN_PASSANT_TARGETS;
}();

inline const ZobristHash ZOBRIST_SIDE_TO_MOVE = prandom_u64();
