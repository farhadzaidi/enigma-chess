#pragma once

#include <array>
#include <cstdint>

#include "board.hpp"
#include "types.hpp"

/** Cached pawn-structure evaluation for a single pawn hash. */
struct PawnTableEntry {
    PawnTableEntry();

    ZobristHash hash = 0;
    std::array<PositionScore, NUM_SIDES> early_pawn_score{};
    std::array<PositionScore, NUM_SIDES> late_pawn_score{};
};

/** Fixed-size hash table that caches pawn-structure scores to avoid redundant evaluation. */
class PawnTable {
public:
    PawnTable();

    /** Reset every entry in the table. */
    void clear();

    /** Probe the table or compute and store a new pawn evaluation for the position. */
    PawnTableEntry get_pawn_score(const Board& b);

    /** Return a reference to the entry at the slot for the given hash. */
    PawnTableEntry& get_entry(ZobristHash hash);

    /** Store an entry in the table (always-replace scheme). */
    void add_entry(const PawnTableEntry& entry);

    /** Check whether the stored entry actually matches the requested hash. */
    bool is_valid_entry(ZobristHash hash, const PawnTableEntry& entry) const;

private:
    static constexpr uint64_t PAWN_TABLE_SIZE = uint64_t{1} << 16;

    std::array<PawnTableEntry, PAWN_TABLE_SIZE> table_;

    /** Map a Zobrist hash to a table index via bitmask. */
    uint64_t get_index(ZobristHash hash) const;
};

/** Global pawn hash table instance, shared across the engine. */
inline PawnTable g_pawn_table;
