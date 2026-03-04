#include <iostream>

#include "board/board.hpp"
#include "eval/eval.hpp"
#include "eval/pawn_table.hpp"
#include "core/types.hpp"
#include "utils/notation.hpp"

static bool pawn_entry_equal(const PawnTableEntry& a, const PawnTableEntry& b) {
    return (
        a.hash == b.hash
        && a.early_pawn_score == b.early_pawn_score
        && a.late_pawn_score == b.late_pawn_score
    );
}

// Store an entry and probe it back, verify all fields match.
static bool test_pawn_table_store_probe() {
    PT.clear();

    PawnTableEntry entry;
    entry.hash = 0xDEADBEEF12345678ULL;
    entry.early_pawn_score = {37, -12};
    entry.late_pawn_score = {82, -24};

    PT.add_entry(entry);
    PawnTableEntry& probed = PT.get_entry(entry.hash);

    if (!PT.is_valid_entry(entry.hash, probed)) {
        std::clog << "[FAILURE] 'pawn_table_store_probe' - Entry not valid after store\n";
        return false;
    }

    if (!pawn_entry_equal(entry, probed)) {
        std::clog << "[FAILURE] 'pawn_table_store_probe' - Field mismatch\n";
        return false;
    }

    return true;
}

// Probing an empty or mismatched hash should return invalid.
static bool test_pawn_table_invalid_probe() {
    PT.clear();

    ZobristHash hash = 0x1234567890ABCDEFULL;
    PawnTableEntry& empty = PT.get_entry(hash);
    if (PT.is_valid_entry(hash, empty)) {
        std::clog << "[FAILURE] 'pawn_table_invalid_probe' - Empty entry should be invalid\n";
        return false;
    }

    ZobristHash stored_hash = 0xAAAABBBBCCCC1234ULL;
    ZobristHash wrong_hash = stored_hash ^ (1ULL << 63); // Same slot, different full hash.

    PawnTableEntry stored;
    stored.hash = stored_hash;
    stored.early_pawn_score = {5, -5};
    stored.late_pawn_score = {9, -9};
    PT.add_entry(stored);

    PawnTableEntry& wrong_probed = PT.get_entry(wrong_hash);
    if (PT.is_valid_entry(wrong_hash, wrong_probed)) {
        std::clog << "[FAILURE] 'pawn_table_invalid_probe' - Mismatched hash should be invalid\n";
        return false;
    }

    return true;
}

// clear() should invalidate previously stored entries.
static bool test_pawn_table_clear() {
    PT.clear();

    PawnTableEntry entry;
    entry.hash = 0xABCDEF0123456789ULL;
    entry.early_pawn_score = {11, 22};
    entry.late_pawn_score = {33, 44};

    PT.add_entry(entry);
    PT.clear();

    PawnTableEntry& probed = PT.get_entry(entry.hash);
    if (PT.is_valid_entry(entry.hash, probed)) {
        std::clog << "[FAILURE] 'pawn_table_clear' - Entry should be invalid after clear()\n";
        return false;
    }

    return true;
}

// Two hashes mapping to the same slot should overwrite.
static bool test_pawn_table_collision_overwrite() {
    PT.clear();

    ZobristHash hash_a = 0x0000000000001234ULL;
    ZobristHash hash_b = hash_a ^ (1ULL << 63); // Same low bits, different high bit.

    PawnTableEntry& slot_a = PT.get_entry(hash_a);
    PawnTableEntry& slot_b = PT.get_entry(hash_b);
    if (&slot_a != &slot_b) {
        std::clog << "[FAILURE] 'pawn_table_collision_overwrite' - Expected hashes to collide in same slot\n";
        return false;
    }

    PawnTableEntry entry_a;
    entry_a.hash = hash_a;
    entry_a.early_pawn_score = {1, 2};
    entry_a.late_pawn_score = {3, 4};

    PawnTableEntry entry_b;
    entry_b.hash = hash_b;
    entry_b.early_pawn_score = {7, 8};
    entry_b.late_pawn_score = {9, 10};

    PT.add_entry(entry_a);
    PawnTableEntry& probed_a_before = PT.get_entry(hash_a);
    if (!PT.is_valid_entry(hash_a, probed_a_before)) {
        std::clog << "[FAILURE] 'pawn_table_collision_overwrite' - First entry should be valid before overwrite\n";
        return false;
    }

    PT.add_entry(entry_b);

    PawnTableEntry& probed_a_after = PT.get_entry(hash_a);
    if (PT.is_valid_entry(hash_a, probed_a_after)) {
        std::clog << "[FAILURE] 'pawn_table_collision_overwrite' - Old colliding entry should be invalid after overwrite\n";
        return false;
    }

    PawnTableEntry& probed_b_after = PT.get_entry(hash_b);
    if (!PT.is_valid_entry(hash_b, probed_b_after)) {
        std::clog << "[FAILURE] 'pawn_table_collision_overwrite' - New colliding entry should be valid\n";
        return false;
    }

    if (!pawn_entry_equal(entry_b, probed_b_after)) {
        std::clog << "[FAILURE] 'pawn_table_collision_overwrite' - Overwritten slot fields mismatch\n";
        return false;
    }

    return true;
}

// Writing the same hash again should replace its stored data.
static bool test_pawn_table_replace_same_hash() {
    PT.clear();

    ZobristHash hash = 0x5555AAAA1234FEDCULL;

    PawnTableEntry entry_1;
    entry_1.hash = hash;
    entry_1.early_pawn_score = {10, 20};
    entry_1.late_pawn_score = {30, 40};

    PawnTableEntry entry_2;
    entry_2.hash = hash;
    entry_2.early_pawn_score = {-1, -2};
    entry_2.late_pawn_score = {-3, -4};

    PT.add_entry(entry_1);
    PT.add_entry(entry_2);

    PawnTableEntry& probed = PT.get_entry(hash);
    if (!PT.is_valid_entry(hash, probed)) {
        std::clog << "[FAILURE] 'pawn_table_replace_same_hash' - Entry should be valid after replacement\n";
        return false;
    }

    if (!pawn_entry_equal(entry_2, probed)) {
        std::clog << "[FAILURE] 'pawn_table_replace_same_hash' - Replaced entry fields mismatch\n";
        return false;
    }

    return true;
}

// get_pawn_score should cache by pawn hash:
// non-pawn moves keep same pawn hash and reuse same pawn evaluation,
// pawn moves change pawn hash and produce a new valid table entry.
static bool test_pawn_table_eval_cache(Board& b) {
    PT.clear();
    b.load_from_fen(START_POS_FEN);

    ZobristHash start_pawn_hash = b.pawn_hash;
    PawnTableEntry start_entry = get_pawn_score(b);
    PawnTableEntry& start_probed = PT.get_entry(start_pawn_hash);
    if (!PT.is_valid_entry(start_pawn_hash, start_probed) || !pawn_entry_equal(start_entry, start_probed)) {
        std::clog << "[FAILURE] 'pawn_table_eval_cache' - Initial pawn entry missing or mismatched\n";
        return false;
    }

    b.make_move(encode_move_from_uci(b, "g1f3")); // Non-pawn move.
    if (b.pawn_hash != start_pawn_hash) {
        std::clog << "[FAILURE] 'pawn_table_eval_cache' - Pawn hash changed after non-pawn move\n";
        return false;
    }

    PawnTableEntry after_non_pawn = get_pawn_score(b);
    if (!pawn_entry_equal(start_entry, after_non_pawn)) {
        std::clog << "[FAILURE] 'pawn_table_eval_cache' - Pawn score changed despite identical pawn structure\n";
        return false;
    }

    b.make_move(encode_move_from_uci(b, "d7d5")); // Pawn move.
    if (b.pawn_hash == start_pawn_hash) {
        std::clog << "[FAILURE] 'pawn_table_eval_cache' - Pawn hash unchanged after pawn move\n";
        return false;
    }

    ZobristHash new_pawn_hash = b.pawn_hash;
    PawnTableEntry new_entry = get_pawn_score(b);
    PawnTableEntry& new_probed = PT.get_entry(new_pawn_hash);
    if (!PT.is_valid_entry(new_pawn_hash, new_probed) || !pawn_entry_equal(new_entry, new_probed)) {
        std::clog << "[FAILURE] 'pawn_table_eval_cache' - New pawn entry missing or mismatched\n";
        return false;
    }

    return true;
}

bool test_pawn_table(Board& b) {
    if (!test_pawn_table_store_probe()) return false;
    if (!test_pawn_table_invalid_probe()) return false;
    if (!test_pawn_table_clear()) return false;
    if (!test_pawn_table_collision_overwrite()) return false;
    if (!test_pawn_table_replace_same_hash()) return false;
    if (!test_pawn_table_eval_cache(b)) return false;
    PT.clear(); // Clean up global table state after test.
    return true;
}
