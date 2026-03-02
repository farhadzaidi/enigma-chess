#pragma once

#include <array>

#include "types.hpp"
#include "zobrist.hpp"

constexpr uint64_t TRANSPOSITION_TABLE_SIZE = uint64_t{1} << 20;

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