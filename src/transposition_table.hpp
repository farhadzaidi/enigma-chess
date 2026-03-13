#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "types.hpp"
#include "constants.hpp"
#include "move.hpp"

// Hash size limits
constexpr size_t DEFAULT_HASH_MB = 64;
constexpr size_t MIN_HASH_MB = 1;
constexpr size_t MAX_HASH_MB = 16384;

namespace {

constexpr size_t TRANSPOSITION_TABLE_BUCKET_SIZE = 4;
constexpr int AGE_PENALTY_WEIGHT = 4;

} // namespace


struct TTEntry {
    static constexpr uint64_t EMPTY_DATA = 0;

    ZobristHash hash;
    uint64_t data;

    constexpr TTEntry() :
        hash(0), data(0) {}

    constexpr TTEntry(ZobristHash hash, Move best_move, SearchDepth depth, PositionScore score, TTNode node) {
        data = (
            static_cast<uint64_t>(best_move.move)                     |
            static_cast<uint64_t>(depth)                        << 16 |
            static_cast<uint64_t>(static_cast<uint16_t>(score)) << 24 |
            static_cast<uint64_t>(node)                         << 40
        );

        // Hash is xor'd with the packed payload so partially torn reads fail validation.
        this->hash = hash ^ data;
    }

    inline Move move() const {
        Move best_move;
        best_move.move = static_cast<uint16_t>(data & 0xFFFF);
        return best_move;
    }

    inline SearchDepth depth() const { return static_cast<SearchDepth>((data >> 16) & 0xFF); }
    inline PositionScore score() const { return static_cast<int16_t>((data >> 24) & 0xFFFF); }
    inline TTNode node() const { return static_cast<TTNode>((data >> 40) & 0xFF); }
    inline uint16_t age() const { return static_cast<uint16_t>((data >> 48) & 0xFFFF); }
    inline ZobristHash _hash() const { return hash ^ data; }

    inline bool is_empty() const { return data == EMPTY_DATA; }

    inline void set_age(uint16_t age) {
        hash = _hash();
        data &= ~(uint64_t{0xFFFF} << 48);
        data |= static_cast<uint64_t>(age) << 48;
        hash = _hash();
    }
};

struct TranspositionTable {
    using TTBucket = std::array<TTEntry, TRANSPOSITION_TABLE_BUCKET_SIZE>;

    std::vector<TTBucket> table;
    uint64_t num_buckets = 0;
    int generation = 0;

    TranspositionTable() {
        resize(DEFAULT_HASH_MB);
    }

    void resize(size_t mb) {
        mb = std::clamp(mb, MIN_HASH_MB, MAX_HASH_MB);
        uint64_t target_buckets = (mb * 1024ULL * 1024ULL) / sizeof(TTBucket);

        num_buckets = 1;
        while (num_buckets * 2 <= target_buckets) {
            num_buckets *= 2;
        }

        table.assign(num_buckets, TTBucket{});
        generation = 0;
    }

    void clear() {
        std::fill(table.begin(), table.end(), TTBucket{});
        generation = 0;
    }

    TTEntry* get_entry(ZobristHash hash) {
        TTBucket& bucket = table[get_index(hash)];

        for (auto& entry : bucket) {
            if (!entry.is_empty() && entry._hash() == hash) {
                return &entry;
            }
        }

        return nullptr;
    }

    void add_entry(TTEntry entry) {
        TTBucket& bucket = table[get_index(entry._hash())];

        entry.set_age(generation);

        TTEntry* least_valuable = &bucket[0];
        int least_value = get_entry_value(*least_valuable);
        for (auto& bucket_entry : bucket) {
            if (bucket_entry.is_empty() || bucket_entry._hash() == entry._hash()) {
                bucket_entry = entry;
                return;
            }

            int value = get_entry_value(bucket_entry);
            if (value < least_value) {
                least_valuable = &bucket_entry;
                least_value = value;
            }
        }

        *least_valuable = entry;
    }

private:
    inline uint64_t get_index(ZobristHash hash) {
        return hash & (num_buckets - 1);
    }

    inline int get_entry_value(const TTEntry& entry) {
        return entry.depth() - AGE_PENALTY_WEIGHT * (generation - entry.age());
    }
};
