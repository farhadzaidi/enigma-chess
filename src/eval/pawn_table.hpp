#pragma once

#include <array>

#include "types.hpp"
#include "constants.hpp"

namespace {

// constants

constexpr uint64_t PAWN_TABLE_SIZE = uint64_t{1} << 16;

} // namespace


struct PawnTableEntry {
    ZobristHash hash;
    std::array<PositionScore, NUM_SIDES> early_pawn_score;
    std::array<PositionScore, NUM_SIDES> late_pawn_score;

    constexpr PawnTableEntry() :hash(0), early_pawn_score{}, late_pawn_score{} {}
};

struct PawnTable {
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

    bool is_valid_entry(ZobristHash hash, const PawnTableEntry& entry) {
        return hash == entry.hash;
    }

    PawnTable() {
        clear();
    }

private:
    std::array<PawnTableEntry, PAWN_TABLE_SIZE> table;

    inline uint64_t get_index(ZobristHash hash) {
        return hash & (PAWN_TABLE_SIZE - 1);
    }
};
