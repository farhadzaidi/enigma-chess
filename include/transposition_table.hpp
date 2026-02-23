#pragma once

#include <array>

#include "types.hpp"
#include "random.hpp"

constexpr uint64_t TRANSPOSITION_TABLE_SIZE = uint64_t{1} << 20;

// --- ZOBRIST NUMBERS ---

const int CASTLING_RIGHTS_COMBINATIONS = 16;
const int EN_PASSANT_TARGET_FILES = 8;

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

// --- TRANSPOSITION TABLE ---

struct TTEntry {
    ZobristHash hash;
    Move best_move;
    SearchDepth depth;
    PositionScore score;
    TTNode node;

    constexpr TTEntry() :
        hash(0), best_move(NULL_MOVE), depth(0), score(DUMMY_SCORE), node(NO_TT_ENTRY) {}

    constexpr TTEntry(ZobristHash hash, Move best_move, SearchDepth depth, PositionScore score, TTNode node) :
        hash(hash), best_move(best_move), depth(depth), score(score), node(node) {}
};

struct TranspositionTable {
    std::array<TTEntry, TRANSPOSITION_TABLE_SIZE> table;

    TranspositionTable() {
        clear();
    }

    void clear() {
        table.fill(TTEntry{});
    }

    TTEntry& get_entry(ZobristHash hash) {
        uint64_t index = get_index(hash);
        return table[index];
    }

    void add_entry(const TTEntry& entry) {
        uint64_t index = get_index(entry.hash);
        table[index] = entry;
    }

    // We verify that the stored position hash matches the current one to ensure
    // the entry corresponds to the same position. This prevents hash collisions where
    // two different positions share the same lower bits and map to the same table index.
    bool is_valid_entry(ZobristHash hash, const TTEntry& entry) {
        return entry.node != NO_TT_ENTRY && hash == entry.hash;
    }

private:

    inline uint64_t get_index(ZobristHash hash) {
        return hash & (TRANSPOSITION_TABLE_SIZE - 1);
    }
};

inline TranspositionTable TT;