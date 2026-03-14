#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "move.hpp"
#include "types.hpp"

constexpr size_t DEFAULT_HASH_MB = 64;
constexpr size_t MIN_HASH_MB = 1;
constexpr size_t MAX_HASH_MB = 16384;

/** Single entry in the transposition table, packing move/depth/score/node into 64 bits */
class TTEntry {
public:
    static constexpr uint64_t EMPTY_DATA = 0;

    TTEntry() = default;
    /** Construct an entry, XOR-compressing the hash with packed data */
    TTEntry(ZobristHash hash, Move best_move, SearchDepth depth, PositionScore score, TTNode node);

    /** Return the best move stored in this entry */
    Move move() const;
    /** Return the search depth at which this entry was created */
    SearchDepth depth() const;
    /** Return the evaluation score */
    PositionScore score() const;
    /** Return the node type (exact, upper bound, or lower bound) */
    TTNode node() const;
    /** Return the generation age used for replacement policy */
    uint16_t age() const;
    /** Recover the original Zobrist hash by XOR-ing data back out */
    ZobristHash hash() const;
    bool is_empty() const;
    /** Update the generation age, re-XORing the hash to stay consistent */
    void set_age(uint16_t age);

private:
    ZobristHash hash_ = 0;
    uint64_t data_ = 0;
};

/** Global transposition table using bucketed hashing with age-based replacement */
class TranspositionTable {
public:
    TranspositionTable();

    /** Resize the table to approximately the given number of megabytes */
    void resize(size_t mb);
    /** Clear all entries and reset the generation counter */
    void clear();
    /** Look up an entry by hash, returning nullptr on miss */
    TTEntry* get_entry(ZobristHash hash);
    /** Store an entry, replacing the least valuable entry in the bucket if full */
    void add_entry(TTEntry entry);
    /** Advance the generation counter for age-based replacement */
    void increment_generation();

private:
    static constexpr size_t TRANSPOSITION_TABLE_BUCKET_SIZE = 4;
    static constexpr int AGE_PENALTY_WEIGHT = 4;

    using TTBucket = std::array<TTEntry, TRANSPOSITION_TABLE_BUCKET_SIZE>;

    std::vector<TTBucket> table;
    uint64_t num_buckets = 0;
    int generation = 0;

    /** Map a hash to a bucket index using power-of-two masking */
    uint64_t get_index(ZobristHash hash) const;
    /** Score an entry for replacement: higher depth is more valuable, stale age is penalized */
    int get_entry_value(const TTEntry& entry) const;
};

inline TranspositionTable g_tt;
