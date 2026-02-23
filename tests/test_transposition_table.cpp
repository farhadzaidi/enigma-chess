#include <iostream>

#include "types.hpp"
#include "move.hpp"
#include "transposition_table.hpp"

// Store an entry and probe it back, verify all fields match
static bool test_tt_store_probe() {
    TT.clear();

    ZobristHash hash = 0xDEADBEEF12345678ULL;
    Move move(E2, E4, QUIET, NORMAL);
    TTEntry entry(hash, move, 10, 150, EXACT);

    TT.add_entry(entry);
    TTEntry& probed = TT.get_entry(hash);

    if (!TT.is_valid_entry(hash, probed)) {
        std::clog << "[FAILURE] 'tt_store_probe' - Entry not valid after store\n";
        return false;
    }

    if (probed.hash != hash || probed.depth != 10 || probed.score != 150 || probed.node != EXACT) {
        std::clog << "[FAILURE] 'tt_store_probe' - Field mismatch\n";
        std::clog << "Hash: " << probed.hash << " (expected " << hash << ")\n";
        std::clog << "Depth: " << (int)probed.depth << " Score: " << probed.score << " Node: " << (int)probed.node << "\n";
        return false;
    }

    if (probed.best_move != move) {
        std::clog << "[FAILURE] 'tt_store_probe' - Move mismatch\n";
        return false;
    }

    return true;
}

// Probing an empty or mismatched hash should return invalid
static bool test_tt_invalid_probe() {
    TT.clear();

    ZobristHash hash = 0x1234567890ABCDEFULL;
    TTEntry& probed = TT.get_entry(hash);

    if (TT.is_valid_entry(hash, probed)) {
        std::clog << "[FAILURE] 'tt_invalid_probe' - Empty entry should not be valid\n";
        return false;
    }

    // Store an entry, then probe with a different hash that maps to a different index
    ZobristHash stored_hash = 0xAAAABBBBCCCCDDDDULL;
    TT.add_entry(TTEntry(stored_hash, NULL_MOVE, 5, 100, EXACT));

    ZobristHash wrong_hash = 0x1111222233334444ULL;
    TTEntry& wrong_probed = TT.get_entry(wrong_hash);

    if (TT.is_valid_entry(wrong_hash, wrong_probed)) {
        std::clog << "[FAILURE] 'tt_invalid_probe' - Mismatched hash should not be valid\n";
        return false;
    }

    return true;
}

// clear() should reset previously stored entries
static bool test_tt_clear() {
    TT.clear();

    ZobristHash hash = 0xABCDEF0123456789ULL;
    TT.add_entry(TTEntry(hash, Move(E2, E4, QUIET, NORMAL), 6, 42, EXACT));
    TT.clear();

    TTEntry& probed = TT.get_entry(hash);
    if (TT.is_valid_entry(hash, probed)) {
        std::clog << "[FAILURE] 'tt_clear' - Entry should be invalid after clear()\n";
        return false;
    }

    return true;
}

// Two hashes mapping to the same slot should overwrite, and hash verification should prevent false hits
static bool test_tt_collision_overwrite() {
    TT.clear();

    ZobristHash hash_a = 0x0000000000001234ULL;
    ZobristHash hash_b = hash_a ^ (1ULL << 63); // Same low bits, different high bit

    TTEntry& slot_a = TT.get_entry(hash_a);
    TTEntry& slot_b = TT.get_entry(hash_b);
    if (&slot_a != &slot_b) {
        std::clog << "[FAILURE] 'tt_collision_overwrite' - Expected hashes to collide in same TT slot\n";
        return false;
    }

    TTEntry entry_a(hash_a, Move(E2, E4, QUIET, NORMAL), 4, 80, EXACT);
    TTEntry entry_b(hash_b, Move(G1, F3, QUIET, NORMAL), 7, 140, FAIL_HIGH);

    TT.add_entry(entry_a);
    TTEntry& probed_a_before = TT.get_entry(hash_a);
    if (!TT.is_valid_entry(hash_a, probed_a_before)) {
        std::clog << "[FAILURE] 'tt_collision_overwrite' - First entry should be valid before overwrite\n";
        return false;
    }

    TT.add_entry(entry_b);

    TTEntry& probed_a_after = TT.get_entry(hash_a);
    if (TT.is_valid_entry(hash_a, probed_a_after)) {
        std::clog << "[FAILURE] 'tt_collision_overwrite' - Old colliding entry should be invalid after overwrite\n";
        return false;
    }

    TTEntry& probed_b_after = TT.get_entry(hash_b);
    if (!TT.is_valid_entry(hash_b, probed_b_after)) {
        std::clog << "[FAILURE] 'tt_collision_overwrite' - New colliding entry should be valid\n";
        return false;
    }

    if (probed_b_after.best_move != entry_b.best_move || probed_b_after.depth != entry_b.depth
        || probed_b_after.score != entry_b.score || probed_b_after.node != entry_b.node) {
        std::clog << "[FAILURE] 'tt_collision_overwrite' - Overwritten slot fields mismatch\n";
        return false;
    }

    return true;
}

// Writing the same hash again should replace its stored data
static bool test_tt_replace_same_hash() {
    TT.clear();

    ZobristHash hash = 0x5555AAAA1234FEDCULL;
    TTEntry entry_1(hash, Move(B1, C3, QUIET, NORMAL), 3, 50, FAIL_LOW);
    TTEntry entry_2(hash, Move(B1, A3, QUIET, NORMAL), 8, 220, EXACT);

    TT.add_entry(entry_1);
    TT.add_entry(entry_2);

    TTEntry& probed = TT.get_entry(hash);
    if (!TT.is_valid_entry(hash, probed)) {
        std::clog << "[FAILURE] 'tt_replace_same_hash' - Entry should be valid after replacement\n";
        return false;
    }

    if (probed.best_move != entry_2.best_move || probed.depth != entry_2.depth
        || probed.score != entry_2.score || probed.node != entry_2.node) {
        std::clog << "[FAILURE] 'tt_replace_same_hash' - Replaced entry fields mismatch\n";
        return false;
    }

    return true;
}

bool test_transposition_table() {
    if (!test_tt_store_probe()) return false;
    if (!test_tt_invalid_probe()) return false;
    if (!test_tt_clear()) return false;
    if (!test_tt_collision_overwrite()) return false;
    if (!test_tt_replace_same_hash()) return false;
    TT.clear(); // Clean up after ourselves
    return true;
}
