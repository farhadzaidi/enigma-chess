#pragma once

#include <array>

#include "types.hpp"
#include "zobrist.hpp"


constexpr uint64_t PAWN_TABLE_SIZE = uint64_t{1} << 16;

struct PawnTableEntry {
    ZobristHash hash;
    std::array<PositionScore, NUM_COLORS> early_pawn_score;
    std::array<PositionScore, NUM_COLORS> late_pawn_score;

    constexpr PawnTableEntry() :hash(0), early_pawn_score{}, late_pawn_score{} {}
};

struct PawnTable {
    std::array<PawnTableEntry, PAWN_TABLE_SIZE> table;

    PawnTable() {
        clear();
    }

    void clear() {
        table.fill(PawnTableEntry{});
    }

    PawnTableEntry& get_entry(ZobristHash hash) {
        uint64_t index = get_index(hash);
        return table[index];
    }

    void add_entry(const PawnTableEntry& entry) {
        uint64_t index = get_index(entry.hash);
        table[index] = entry;
    }

    // We verify that the stored position hash matches the current one to ensure
    // the entry corresponds to the same position. This prevents hash collisions where
    // two different positions share the same lower bits and map to the same table index.
    bool is_valid_entry(ZobristHash hash, const PawnTableEntry& entry) {
        return hash == entry.hash;
    }

private:

    inline uint64_t get_index(ZobristHash hash) {
        return hash & (PAWN_TABLE_SIZE - 1);
    }
};

inline PawnTable PT;
