#include <iostream>

#include "board/board.hpp"
#include "eval/eval.hpp"
#include "eval/pawn_table.hpp"
#include "types.hpp"
#include "utils/notation.hpp"
#include "tests/helpers.hpp"

namespace {

bool pawn_entry_equal(const PawnTableEntry& a, const PawnTableEntry& b) {
    return (
        a.hash == b.hash
        && a.early_pawn_score == b.early_pawn_score
        && a.late_pawn_score == b.late_pawn_score
    );
}

// Store an entry and probe it back, verify all fields match.
bool test_pawn_table_store_probe() {
    g_shared.pawn_table.clear();

    PawnTableEntry entry;
    entry.hash = 0xDEADBEEF12345678ULL;
    entry.early_pawn_score = {37, -12};
    entry.late_pawn_score = {82, -24};

    g_shared.pawn_table.add_entry(entry);
    PawnTableEntry& probed = g_shared.pawn_table.get_entry(entry.hash);

    ASSERT(g_shared.pawn_table.is_valid_entry(entry.hash, probed), "g_pawn_table_store_probe", "Entry not valid after store");

    ASSERT(pawn_entry_equal(entry, probed), "g_pawn_table_store_probe", "Field mismatch");

    return true;
}

// Probing an empty or mismatched hash should return invalid.
bool test_pawn_table_invalid_probe() {
    g_shared.pawn_table.clear();

    ZobristHash hash = 0x1234567890ABCDEFULL;
    PawnTableEntry& empty = g_shared.pawn_table.get_entry(hash);
    ASSERT(!g_shared.pawn_table.is_valid_entry(hash, empty), "g_pawn_table_invalid_probe", "Empty entry should be invalid");

    ZobristHash stored_hash = 0xAAAABBBBCCCC1234ULL;
    ZobristHash wrong_hash = stored_hash ^ (1ULL << 63); // Same slot, different full hash.

    PawnTableEntry stored;
    stored.hash = stored_hash;
    stored.early_pawn_score = {5, -5};
    stored.late_pawn_score = {9, -9};
    g_shared.pawn_table.add_entry(stored);

    PawnTableEntry& wrong_probed = g_shared.pawn_table.get_entry(wrong_hash);
    ASSERT(!g_shared.pawn_table.is_valid_entry(wrong_hash, wrong_probed), "g_pawn_table_invalid_probe", "Mismatched hash should be invalid");

    return true;
}

// clear() should invalidate previously stored entries.
bool test_pawn_table_clear() {
    g_shared.pawn_table.clear();

    PawnTableEntry entry;
    entry.hash = 0xABCDEF0123456789ULL;
    entry.early_pawn_score = {11, 22};
    entry.late_pawn_score = {33, 44};

    g_shared.pawn_table.add_entry(entry);
    g_shared.pawn_table.clear();

    PawnTableEntry& probed = g_shared.pawn_table.get_entry(entry.hash);
    ASSERT(!g_shared.pawn_table.is_valid_entry(entry.hash, probed), "g_pawn_table_clear", "Entry should be invalid after clear()");

    return true;
}

// Two hashes mapping to the same slot should overwrite.
bool test_pawn_table_collision_overwrite() {
    g_shared.pawn_table.clear();

    ZobristHash hash_a = 0x0000000000001234ULL;
    ZobristHash hash_b = hash_a ^ (1ULL << 63); // Same low bits, different high bit.

    PawnTableEntry& slot_a = g_shared.pawn_table.get_entry(hash_a);
    PawnTableEntry& slot_b = g_shared.pawn_table.get_entry(hash_b);
    ASSERT(&slot_a == &slot_b, "g_pawn_table_collision_overwrite", "Expected hashes to collide in same slot");

    PawnTableEntry entry_a;
    entry_a.hash = hash_a;
    entry_a.early_pawn_score = {1, 2};
    entry_a.late_pawn_score = {3, 4};

    PawnTableEntry entry_b;
    entry_b.hash = hash_b;
    entry_b.early_pawn_score = {7, 8};
    entry_b.late_pawn_score = {9, 10};

    g_shared.pawn_table.add_entry(entry_a);
    PawnTableEntry& probed_a_before = g_shared.pawn_table.get_entry(hash_a);
    ASSERT(g_shared.pawn_table.is_valid_entry(hash_a, probed_a_before), "g_pawn_table_collision_overwrite", "First entry should be valid before overwrite");

    g_shared.pawn_table.add_entry(entry_b);

    PawnTableEntry& probed_a_after = g_shared.pawn_table.get_entry(hash_a);
    ASSERT(!g_shared.pawn_table.is_valid_entry(hash_a, probed_a_after), "g_pawn_table_collision_overwrite", "Old colliding entry should be invalid after overwrite");

    PawnTableEntry& probed_b_after = g_shared.pawn_table.get_entry(hash_b);
    ASSERT(g_shared.pawn_table.is_valid_entry(hash_b, probed_b_after), "g_pawn_table_collision_overwrite", "New colliding entry should be valid");

    ASSERT(pawn_entry_equal(entry_b, probed_b_after), "g_pawn_table_collision_overwrite", "Overwritten slot fields mismatch");

    return true;
}

// Writing the same hash again should replace its stored data.
bool test_pawn_table_replace_same_hash() {
    g_shared.pawn_table.clear();

    ZobristHash hash = 0x5555AAAA1234FEDCULL;

    PawnTableEntry entry_1;
    entry_1.hash = hash;
    entry_1.early_pawn_score = {10, 20};
    entry_1.late_pawn_score = {30, 40};

    PawnTableEntry entry_2;
    entry_2.hash = hash;
    entry_2.early_pawn_score = {-1, -2};
    entry_2.late_pawn_score = {-3, -4};

    g_shared.pawn_table.add_entry(entry_1);
    g_shared.pawn_table.add_entry(entry_2);

    PawnTableEntry& probed = g_shared.pawn_table.get_entry(hash);
    ASSERT(g_shared.pawn_table.is_valid_entry(hash, probed), "g_pawn_table_replace_same_hash", "Entry should be valid after replacement");

    ASSERT(pawn_entry_equal(entry_2, probed), "g_pawn_table_replace_same_hash", "Replaced entry fields mismatch");

    return true;
}

// get_pawn_score should cache by pawn hash:
// non-pawn moves keep same pawn hash and reuse same pawn evaluation,
// pawn moves change pawn hash and produce a new valid table entry.
bool test_pawn_table_eval_cache(Board& b) {
    g_shared.pawn_table.clear();
    b.load_from_fen(START_POS_FEN);

    ZobristHash start_pawn_hash = b.pawn_hash;
    PawnTableEntry start_entry = get_pawn_score(b);
    PawnTableEntry& start_probed = g_shared.pawn_table.get_entry(start_pawn_hash);
    ASSERT(g_shared.pawn_table.is_valid_entry(start_pawn_hash, start_probed) && pawn_entry_equal(start_entry, start_probed), "g_pawn_table_eval_cache", "Initial pawn entry missing or mismatched");

    b.make_move(encode_move_from_uci(b, "g1f3")); // Non-pawn move.
    ASSERT_EQ(b.pawn_hash, start_pawn_hash, "g_pawn_table_eval_cache", "Pawn hash changed after non-pawn move");

    PawnTableEntry after_non_pawn = get_pawn_score(b);
    ASSERT(pawn_entry_equal(start_entry, after_non_pawn), "g_pawn_table_eval_cache", "Pawn score changed despite identical pawn structure");

    b.make_move(encode_move_from_uci(b, "d7d5")); // Pawn move.
    ASSERT(b.pawn_hash != start_pawn_hash, "g_pawn_table_eval_cache", "Pawn hash unchanged after pawn move");

    ZobristHash new_pawn_hash = b.pawn_hash;
    PawnTableEntry new_entry = get_pawn_score(b);
    PawnTableEntry& new_probed = g_shared.pawn_table.get_entry(new_pawn_hash);
    ASSERT(g_shared.pawn_table.is_valid_entry(new_pawn_hash, new_probed) && pawn_entry_equal(new_entry, new_probed), "g_pawn_table_eval_cache", "New pawn entry missing or mismatched");

    return true;
}

} // namespace

bool test_pawn_table(Board& b) {
    if (!test_pawn_table_store_probe()) return false;
    if (!test_pawn_table_invalid_probe()) return false;
    if (!test_pawn_table_clear()) return false;
    if (!test_pawn_table_collision_overwrite()) return false;
    if (!test_pawn_table_replace_same_hash()) return false;
    if (!test_pawn_table_eval_cache(b)) return false;
    g_shared.pawn_table.clear(); // Clean up global table state after test.
    return true;
}
