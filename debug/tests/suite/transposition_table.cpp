#include <iostream>

#include "core/move.hpp"
#include "core/transposition_table.hpp"
#include "core/types.hpp"
#include "core/globals.hpp"
#include "tests/helpers.hpp"

namespace {

bool entry_matches(
    const TTEntry& entry,
    ZobristHash hash,
    Move best_move,
    SearchDepth depth,
    PositionScore score,
    TTNode node,
    uint16_t age
) {
    return (
        entry.hash == hash
        && entry.best_move == best_move
        && entry.depth == depth
        && entry.score == score
        && entry.node == node
        && entry.age == age
    );
}

// Store an entry and probe it back, verify all fields match and age is stamped.
bool test_tt_store_probe() {
    g_transposition_table.clear();
    g_transposition_table.generation = 7;

    ZobristHash hash = 0xDEADBEEF12345678ULL;
    Move move(E2, E4, MoveType::Quiet, MoveFlag::Normal);
    TTEntry entry(hash, move, 10, 150, TTNode::Exact);

    g_transposition_table.add_entry(entry);
    TTEntry* probed = g_transposition_table.get_entry(hash);
    ASSERT(probed, "tt_store_probe", "Expected stored entry to be found");

    ASSERT(entry_matches(*probed, hash, move, 10, 150, TTNode::Exact, 7), "tt_store_probe", "Stored entry fields mismatch");

    return true;
}

// Probing an empty or mismatched hash should return nullptr.
bool test_tt_invalid_probe() {
    g_transposition_table.clear();

    ZobristHash empty_hash = 0x1234567890ABCDEFULL;
    ASSERT(!g_transposition_table.get_entry(empty_hash), "tt_invalid_probe", "Empty probe should return nullptr");

    ZobristHash stored_hash = 0x9A9A9A9A00000001ULL;
    ZobristHash wrong_hash = 0x9A9A9A9A00000002ULL;
    g_transposition_table.add_entry(TTEntry(stored_hash, NULL_MOVE, 5, 100, TTNode::Exact));

    ASSERT(!g_transposition_table.get_entry(wrong_hash), "tt_invalid_probe", "Mismatched hash should not be found");

    return true;
}

// clear() should invalidate entries and reset generation.
bool test_tt_clear() {
    g_transposition_table.clear();
    g_transposition_table.generation = 3;

    ZobristHash hash = 0xABCDEF0123456789ULL;
    g_transposition_table.add_entry(TTEntry(hash, Move(E2, E4, MoveType::Quiet, MoveFlag::Normal), 6, 42, TTNode::Exact));
    g_transposition_table.clear();

    ASSERT(!g_transposition_table.get_entry(hash), "tt_clear", "Entry should not exist after clear()");

    ASSERT_EQ(g_transposition_table.generation, 0, "tt_clear", "Generation should reset to 0 after clear()");

    return true;
}

// Writing the same hash again should replace the existing record in-place.
bool test_tt_replace_same_hash() {
    g_transposition_table.clear();
    ZobristHash hash = 0x5555AAAA1234FEDCULL;

    g_transposition_table.generation = 1;
    g_transposition_table.add_entry(TTEntry(hash, Move(B1, C3, MoveType::Quiet, MoveFlag::Normal), 3, 50, TTNode::FailLow));

    g_transposition_table.generation = 5;
    g_transposition_table.add_entry(TTEntry(hash, Move(B1, A3, MoveType::Quiet, MoveFlag::Normal), 8, 220, TTNode::Exact));

    TTEntry* probed = g_transposition_table.get_entry(hash);
    ASSERT(probed, "tt_replace_same_hash", "Replaced entry should exist");

    ASSERT(entry_matches(*probed, hash, Move(B1, A3, MoveType::Quiet, MoveFlag::Normal), 8, 220, TTNode::Exact, 5), "tt_replace_same_hash", "Replaced entry fields mismatch");

    return true;
}

// When all 4 bucket slots are occupied by different hashes, inserting a 5th
// should evict the least valuable entry (lowest depth - age penalty).
bool test_tt_bucket_eviction() {
    g_transposition_table.clear();
    g_transposition_table.generation = 10;

    // All 5 hashes share the same low 20 bits so they map to the same bucket.
    constexpr uint64_t BASE = 0x12345ULL;
    ZobristHash hash_1 = (1ULL << 40) | BASE;
    ZobristHash hash_2 = (2ULL << 40) | BASE;
    ZobristHash hash_3 = (3ULL << 40) | BASE; // Lowest depth → least valuable → should be evicted
    ZobristHash hash_4 = (4ULL << 40) | BASE;
    ZobristHash hash_5 = (5ULL << 40) | BASE;

    g_transposition_table.add_entry(TTEntry(hash_1, NULL_MOVE, 10, 100, TTNode::Exact));
    g_transposition_table.add_entry(TTEntry(hash_2, NULL_MOVE, 8,  80,  TTNode::Exact));
    g_transposition_table.add_entry(TTEntry(hash_3, NULL_MOVE, 1,  10,  TTNode::Exact));
    g_transposition_table.add_entry(TTEntry(hash_4, NULL_MOVE, 5,  50,  TTNode::Exact));

    // All 4 should be present
    ASSERT(g_transposition_table.get_entry(hash_1), "tt_bucket_eviction", "hash_1 missing before eviction");
    ASSERT(g_transposition_table.get_entry(hash_2), "tt_bucket_eviction", "hash_2 missing before eviction");
    ASSERT(g_transposition_table.get_entry(hash_3), "tt_bucket_eviction", "hash_3 missing before eviction");
    ASSERT(g_transposition_table.get_entry(hash_4), "tt_bucket_eviction", "hash_4 missing before eviction");

    // Insert 5th entry — should evict hash_3 (depth 1 is least valuable)
    g_transposition_table.add_entry(TTEntry(hash_5, NULL_MOVE, 15, 200, TTNode::Exact));

    ASSERT(g_transposition_table.get_entry(hash_1),  "tt_bucket_eviction", "hash_1 should survive eviction");
    ASSERT(g_transposition_table.get_entry(hash_2),  "tt_bucket_eviction", "hash_2 should survive eviction");
    ASSERT(!g_transposition_table.get_entry(hash_3), "tt_bucket_eviction", "hash_3 should have been evicted");
    ASSERT(g_transposition_table.get_entry(hash_4),  "tt_bucket_eviction", "hash_4 should survive eviction");
    ASSERT(g_transposition_table.get_entry(hash_5),  "tt_bucket_eviction", "hash_5 should be present after insertion");

    return true;
}

// Stale entries (old age) should be evicted before fresh entries of the same depth.
bool test_tt_age_based_eviction() {
    g_transposition_table.clear();

    constexpr uint64_t BASE = 0x54321ULL;
    ZobristHash hash_stale  = (1ULL << 40) | BASE;
    ZobristHash hash_fresh  = (2ULL << 40) | BASE;
    ZobristHash hash_fill_1 = (3ULL << 40) | BASE;
    ZobristHash hash_fill_2 = (4ULL << 40) | BASE;
    ZobristHash hash_new    = (5ULL << 40) | BASE;

    // Insert stale entry at generation 0
    g_transposition_table.generation = 0;
    g_transposition_table.add_entry(TTEntry(hash_stale, NULL_MOVE, 5, 50, TTNode::Exact));

    // Insert fresh entry and fillers at generation 10
    g_transposition_table.generation = 10;
    g_transposition_table.add_entry(TTEntry(hash_fresh,  NULL_MOVE, 5, 60, TTNode::Exact));
    g_transposition_table.add_entry(TTEntry(hash_fill_1, NULL_MOVE, 5, 70, TTNode::Exact));
    g_transposition_table.add_entry(TTEntry(hash_fill_2, NULL_MOVE, 5, 80, TTNode::Exact));

    // Value: stale = 5 - 4*(10-0) = -35, fresh = 5 - 4*(10-10) = 5
    // Stale entry should be evicted
    g_transposition_table.add_entry(TTEntry(hash_new, NULL_MOVE, 5, 90, TTNode::Exact));

    ASSERT(!g_transposition_table.get_entry(hash_stale), "tt_age_based_eviction", "Stale entry should have been evicted");
    ASSERT(g_transposition_table.get_entry(hash_fresh),  "tt_age_based_eviction", "Fresh entry should survive");
    ASSERT(g_transposition_table.get_entry(hash_new),    "tt_age_based_eviction", "New entry should be present");

    return true;
}

} // namespace

bool test_transposition_table() {
    if (!test_tt_store_probe()) return false;
    if (!test_tt_invalid_probe()) return false;
    if (!test_tt_clear()) return false;
    if (!test_tt_replace_same_hash()) return false;
    if (!test_tt_bucket_eviction()) return false;
    if (!test_tt_age_based_eviction()) return false;
    g_transposition_table.clear(); // Clean up global table state after test.
    return true;
}
