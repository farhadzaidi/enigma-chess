#pragma once

#include <array>

#include "types.hpp"
#include "zobrist.hpp"

constexpr uint64_t TRANSPOSITION_TABLE_SIZE = uint64_t{1} << 20;
constexpr size_t TRANSPOSITION_TABLE_BUCKET_SIZE = 4;

// --- TRANSPOSITION TABLE ---

struct TTEntry {
    ZobristHash hash;
    Move best_move;
    SearchDepth depth;
    PositionScore score;
    TTNode node;
    uint16_t age;

    constexpr TTEntry() :
        hash(0), best_move(NULL_MOVE), depth(0), score(DUMMY_SCORE), node(NO_TT_ENTRY), age(0) {}

    constexpr TTEntry(ZobristHash hash, Move best_move, SearchDepth depth, PositionScore score, TTNode node) :
        hash(hash), best_move(best_move), depth(depth), score(score), node(node), age(0) {}
};

using TTBucket = std::array<TTEntry, TRANSPOSITION_TABLE_BUCKET_SIZE>;

struct TranspositionTable {
    std::array<TTBucket, TRANSPOSITION_TABLE_SIZE> table;
    int generation = 0;

    TranspositionTable() {
        clear();
    }

    void clear() {
        table.fill(TTBucket{});
        generation = 0;
    }

    TTEntry* get_entry(ZobristHash hash) {
        uint64_t index = get_index(hash);
        TTBucket& bucket = table[index];

        for (auto& entry : bucket) {
            if (entry.hash == hash && entry.node != NO_TT_ENTRY) {
                return &entry;
            }
        }
        
        return nullptr;
    }

    void add_entry(TTEntry entry) {
        uint64_t index = get_index(entry.hash);
        TTBucket& bucket = table[index];

        // Stamp age
        entry.age = generation;

        // We keep track of the least valuable entry to replace
        TTEntry* least_valuable = &bucket[0];
        int least_value = get_entry_value(least_valuable);
        for (auto& bucket_entry : bucket) {
            // If both hashes match (same position) or we have a free slot we can just
            // insert/replace the entry and return out
            if (bucket_entry.hash == entry.hash || bucket_entry.node == NO_TT_ENTRY) {
                bucket_entry = entry;
                return;
            }

            // In case we have to replace an entry
            int value = get_entry_value(&bucket_entry);
            if (value < least_value) {
                least_valuable = &bucket_entry;
                least_value = value;
            }
        }

        // If we made it out of the loop then we couldn't find a spot
        // for this entry and must replace the least valuable one
        *least_valuable = entry;
    }

private:

    inline uint64_t get_index(ZobristHash hash) {
        return hash & (TRANSPOSITION_TABLE_SIZE - 1);
    }

    inline int get_entry_value(TTEntry* entry) {
        return entry->depth - 4 * (generation - entry->age);
    }
};

inline TranspositionTable TT;