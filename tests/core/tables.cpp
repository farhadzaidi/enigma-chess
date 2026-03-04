#include <array>
#include <iostream>

#include "core/move.hpp"
#include "core/transposition_table.hpp"
#include "core/types.hpp"
#include "core/globals.hpp"

static constexpr uint64_t TT_INDEX_MASK = TRANSPOSITION_TABLE_SIZE - 1;
static constexpr uint64_t COLLIDING_SLOT = 0x12345ULL;

static ZobristHash colliding_hash(uint64_t variant) {
    return (COLLIDING_SLOT & TT_INDEX_MASK) | (variant << 20);
}

static bool entry_matches(
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
static bool test_tt_store_probe() {
    g_transposition_table.clear();
    g_transposition_table.generation = 7;

    ZobristHash hash = 0xDEADBEEF12345678ULL;
    Move move(E2, E4, MoveType::Quiet, MoveFlag::Normal);
    TTEntry entry(hash, move, 10, 150, TTNode::Exact);

    g_transposition_table.add_entry(entry);
    TTEntry* probed = g_transposition_table.get_entry(hash);
    if (!probed) {
        std::clog << "[FAILURE] 'tt_store_probe' - Expected stored entry to be found\n";
        return false;
    }

    if (!entry_matches(*probed, hash, move, 10, 150, TTNode::Exact, 7)) {
        std::clog << "[FAILURE] 'tt_store_probe' - Stored entry fields mismatch\n";
        return false;
    }

    return true;
}

// Probing an empty or mismatched hash should return nullptr.
static bool test_tt_invalid_probe() {
    g_transposition_table.clear();

    ZobristHash empty_hash = 0x1234567890ABCDEFULL;
    if (g_transposition_table.get_entry(empty_hash)) {
        std::clog << "[FAILURE] 'tt_invalid_probe' - Empty probe should return nullptr\n";
        return false;
    }

    ZobristHash stored_hash = colliding_hash(1);
    ZobristHash wrong_hash = colliding_hash(2);
    g_transposition_table.add_entry(TTEntry(stored_hash, NULL_MOVE, 5, 100, TTNode::Exact));

    if (g_transposition_table.get_entry(wrong_hash)) {
        std::clog << "[FAILURE] 'tt_invalid_probe' - Mismatched hash should not be found\n";
        return false;
    }

    return true;
}

// clear() should invalidate entries and reset generation.
static bool test_tt_clear() {
    g_transposition_table.clear();
    g_transposition_table.generation = 3;

    ZobristHash hash = 0xABCDEF0123456789ULL;
    g_transposition_table.add_entry(TTEntry(hash, Move(E2, E4, MoveType::Quiet, MoveFlag::Normal), 6, 42, TTNode::Exact));
    g_transposition_table.clear();

    if (g_transposition_table.get_entry(hash)) {
        std::clog << "[FAILURE] 'tt_clear' - Entry should not exist after clear()\n";
        return false;
    }

    if (g_transposition_table.generation != 0) {
        std::clog << "[FAILURE] 'tt_clear' - Generation should reset to 0 after clear()\n";
        return false;
    }

    return true;
}

// Up to bucket size colliding hashes should coexist in the same bucket.
static bool test_tt_bucket_collisions_fit() {
    g_transposition_table.clear();
    g_transposition_table.generation = 4;

    std::array<TTEntry, TRANSPOSITION_TABLE_BUCKET_SIZE> entries = {
        TTEntry(colliding_hash(1), Move(E2, E4, MoveType::Quiet, MoveFlag::Normal), 4, 10, TTNode::Exact),
        TTEntry(colliding_hash(2), Move(D2, D4, MoveType::Quiet, MoveFlag::Normal), 5, 20, TTNode::FailHigh),
        TTEntry(colliding_hash(3), Move(G1, F3, MoveType::Quiet, MoveFlag::Normal), 6, 30, TTNode::FailLow),
        TTEntry(colliding_hash(4), Move(C2, C4, MoveType::Quiet, MoveFlag::Normal), 7, 40, TTNode::Exact),
    };

    for (const auto& entry : entries) {
        g_transposition_table.add_entry(entry);
    }

    for (const auto& entry : entries) {
        TTEntry* probed = g_transposition_table.get_entry(entry.hash);
        if (!probed) {
            std::clog << "[FAILURE] 'tt_bucket_collisions_fit' - Missing colliding entry in bucket\n";
            return false;
        }

        if (!entry_matches(*probed, entry.hash, entry.best_move, entry.depth, entry.score, entry.node, 4)) {
            std::clog << "[FAILURE] 'tt_bucket_collisions_fit' - Colliding entry fields mismatch\n";
            return false;
        }
    }

    return true;
}

// Writing the same hash again should replace the existing record in-place.
static bool test_tt_replace_same_hash() {
    g_transposition_table.clear();
    ZobristHash hash = 0x5555AAAA1234FEDCULL;

    g_transposition_table.generation = 1;
    g_transposition_table.add_entry(TTEntry(hash, Move(B1, C3, MoveType::Quiet, MoveFlag::Normal), 3, 50, TTNode::FailLow));

    g_transposition_table.generation = 5;
    g_transposition_table.add_entry(TTEntry(hash, Move(B1, A3, MoveType::Quiet, MoveFlag::Normal), 8, 220, TTNode::Exact));

    TTEntry* probed = g_transposition_table.get_entry(hash);
    if (!probed) {
        std::clog << "[FAILURE] 'tt_replace_same_hash' - Replaced entry should exist\n";
        return false;
    }

    if (!entry_matches(*probed, hash, Move(B1, A3, MoveType::Quiet, MoveFlag::Normal), 8, 220, TTNode::Exact, 5)) {
        std::clog << "[FAILURE] 'tt_replace_same_hash' - Replaced entry fields mismatch\n";
        return false;
    }

    uint64_t index = hash & TT_INDEX_MASK;
    int matches = 0;
    for (const auto& bucket_entry : g_transposition_table.table[index]) {
        if (bucket_entry.hash == hash && bucket_entry.node != TTNode::None) {
            matches++;
        }
    }
    if (matches != 1) {
        std::clog << "[FAILURE] 'tt_replace_same_hash' - Bucket should contain exactly one copy of hash\n";
        return false;
    }

    return true;
}

// With a full bucket and same age, lowest depth should be replaced.
static bool test_tt_bucket_replacement_by_depth() {
    g_transposition_table.clear();
    g_transposition_table.generation = 0;

    std::array<TTEntry, TRANSPOSITION_TABLE_BUCKET_SIZE> entries = {
        TTEntry(colliding_hash(11), Move(A2, A3, MoveType::Quiet, MoveFlag::Normal), 8, 10, TTNode::Exact),
        TTEntry(colliding_hash(12), Move(B2, B3, MoveType::Quiet, MoveFlag::Normal), 4, 11, TTNode::Exact),
        TTEntry(colliding_hash(13), Move(C2, C3, MoveType::Quiet, MoveFlag::Normal), 12, 12, TTNode::Exact),
        TTEntry(colliding_hash(14), Move(D2, D3, MoveType::Quiet, MoveFlag::Normal), 10, 13, TTNode::Exact),
    };

    for (const auto& entry : entries) {
        g_transposition_table.add_entry(entry);
    }

    ZobristHash should_be_replaced = entries[1].hash; // Lowest depth = 4
    g_transposition_table.add_entry(TTEntry(colliding_hash(15), Move(E2, E3, MoveType::Quiet, MoveFlag::Normal), 6, 99, TTNode::FailHigh));

    if (g_transposition_table.get_entry(should_be_replaced)) {
        std::clog << "[FAILURE] 'tt_bucket_replacement_by_depth' - Lowest depth entry should be replaced\n";
        return false;
    }

    TTEntry* replacement = g_transposition_table.get_entry(colliding_hash(15));
    if (!replacement) {
        std::clog << "[FAILURE] 'tt_bucket_replacement_by_depth' - Replacement entry missing\n";
        return false;
    }

    if (!entry_matches(*replacement, colliding_hash(15), Move(E2, E3, MoveType::Quiet, MoveFlag::Normal), 6, 99, TTNode::FailHigh, 0)) {
        std::clog << "[FAILURE] 'tt_bucket_replacement_by_depth' - Replacement entry fields mismatch\n";
        return false;
    }

    return true;
}

// With equal depth, aging should make older entries less valuable and first to replace.
static bool test_tt_bucket_replacement_by_age() {
    g_transposition_table.clear();

    std::array<ZobristHash, TRANSPOSITION_TABLE_BUCKET_SIZE> hashes = {
        colliding_hash(21),
        colliding_hash(22),
        colliding_hash(23),
        colliding_hash(24),
    };

    for (size_t i = 0; i < hashes.size(); i++) {
        g_transposition_table.generation = static_cast<int>(i);
        g_transposition_table.add_entry(TTEntry(
            hashes[i],
            Move(static_cast<Square>(A2 + i), static_cast<Square>(A3 + i), MoveType::Quiet, MoveFlag::Normal),
            20,
            100,
            TTNode::Exact
        ));
    }

    // Entry at hashes[0] is oldest and should be replaced first when values differ by age only.
    g_transposition_table.generation = 8;
    ZobristHash replacement_hash = colliding_hash(25);
    g_transposition_table.add_entry(TTEntry(replacement_hash, Move(H2, H3, MoveType::Quiet, MoveFlag::Normal), 20, 200, TTNode::FailLow));

    if (g_transposition_table.get_entry(hashes[0])) {
        std::clog << "[FAILURE] 'tt_bucket_replacement_by_age' - Oldest entry should be replaced\n";
        return false;
    }

    for (size_t i = 1; i < hashes.size(); i++) {
        if (!g_transposition_table.get_entry(hashes[i])) {
            std::clog << "[FAILURE] 'tt_bucket_replacement_by_age' - Newer entry was replaced unexpectedly\n";
            return false;
        }
    }

    TTEntry* replacement = g_transposition_table.get_entry(replacement_hash);
    if (!replacement) {
        std::clog << "[FAILURE] 'tt_bucket_replacement_by_age' - Replacement entry missing\n";
        return false;
    }

    if (replacement->age != 8) {
        std::clog << "[FAILURE] 'tt_bucket_replacement_by_age' - Replacement entry has wrong stamped age\n";
        return false;
    }

    return true;
}

bool test_transposition_table() {
    if (!test_tt_store_probe()) return false;
    if (!test_tt_invalid_probe()) return false;
    if (!test_tt_clear()) return false;
    if (!test_tt_bucket_collisions_fit()) return false;
    if (!test_tt_replace_same_hash()) return false;
    if (!test_tt_bucket_replacement_by_depth()) return false;
    if (!test_tt_bucket_replacement_by_age()) return false;
    g_transposition_table.clear(); // Clean up global table state after test.
    return true;
}
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
    g_pawn_table.clear();

    PawnTableEntry entry;
    entry.hash = 0xDEADBEEF12345678ULL;
    entry.early_pawn_score = {37, -12};
    entry.late_pawn_score = {82, -24};

    g_pawn_table.add_entry(entry);
    PawnTableEntry& probed = g_pawn_table.get_entry(entry.hash);

    if (!g_pawn_table.is_valid_entry(entry.hash, probed)) {
        std::clog << "[FAILURE] 'g_pawn_table_store_probe' - Entry not valid after store\n";
        return false;
    }

    if (!pawn_entry_equal(entry, probed)) {
        std::clog << "[FAILURE] 'g_pawn_table_store_probe' - Field mismatch\n";
        return false;
    }

    return true;
}

// Probing an empty or mismatched hash should return invalid.
static bool test_pawn_table_invalid_probe() {
    g_pawn_table.clear();

    ZobristHash hash = 0x1234567890ABCDEFULL;
    PawnTableEntry& empty = g_pawn_table.get_entry(hash);
    if (g_pawn_table.is_valid_entry(hash, empty)) {
        std::clog << "[FAILURE] 'g_pawn_table_invalid_probe' - Empty entry should be invalid\n";
        return false;
    }

    ZobristHash stored_hash = 0xAAAABBBBCCCC1234ULL;
    ZobristHash wrong_hash = stored_hash ^ (1ULL << 63); // Same slot, different full hash.

    PawnTableEntry stored;
    stored.hash = stored_hash;
    stored.early_pawn_score = {5, -5};
    stored.late_pawn_score = {9, -9};
    g_pawn_table.add_entry(stored);

    PawnTableEntry& wrong_probed = g_pawn_table.get_entry(wrong_hash);
    if (g_pawn_table.is_valid_entry(wrong_hash, wrong_probed)) {
        std::clog << "[FAILURE] 'g_pawn_table_invalid_probe' - Mismatched hash should be invalid\n";
        return false;
    }

    return true;
}

// clear() should invalidate previously stored entries.
static bool test_pawn_table_clear() {
    g_pawn_table.clear();

    PawnTableEntry entry;
    entry.hash = 0xABCDEF0123456789ULL;
    entry.early_pawn_score = {11, 22};
    entry.late_pawn_score = {33, 44};

    g_pawn_table.add_entry(entry);
    g_pawn_table.clear();

    PawnTableEntry& probed = g_pawn_table.get_entry(entry.hash);
    if (g_pawn_table.is_valid_entry(entry.hash, probed)) {
        std::clog << "[FAILURE] 'g_pawn_table_clear' - Entry should be invalid after clear()\n";
        return false;
    }

    return true;
}

// Two hashes mapping to the same slot should overwrite.
static bool test_pawn_table_collision_overwrite() {
    g_pawn_table.clear();

    ZobristHash hash_a = 0x0000000000001234ULL;
    ZobristHash hash_b = hash_a ^ (1ULL << 63); // Same low bits, different high bit.

    PawnTableEntry& slot_a = g_pawn_table.get_entry(hash_a);
    PawnTableEntry& slot_b = g_pawn_table.get_entry(hash_b);
    if (&slot_a != &slot_b) {
        std::clog << "[FAILURE] 'g_pawn_table_collision_overwrite' - Expected hashes to collide in same slot\n";
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

    g_pawn_table.add_entry(entry_a);
    PawnTableEntry& probed_a_before = g_pawn_table.get_entry(hash_a);
    if (!g_pawn_table.is_valid_entry(hash_a, probed_a_before)) {
        std::clog << "[FAILURE] 'g_pawn_table_collision_overwrite' - First entry should be valid before overwrite\n";
        return false;
    }

    g_pawn_table.add_entry(entry_b);

    PawnTableEntry& probed_a_after = g_pawn_table.get_entry(hash_a);
    if (g_pawn_table.is_valid_entry(hash_a, probed_a_after)) {
        std::clog << "[FAILURE] 'g_pawn_table_collision_overwrite' - Old colliding entry should be invalid after overwrite\n";
        return false;
    }

    PawnTableEntry& probed_b_after = g_pawn_table.get_entry(hash_b);
    if (!g_pawn_table.is_valid_entry(hash_b, probed_b_after)) {
        std::clog << "[FAILURE] 'g_pawn_table_collision_overwrite' - New colliding entry should be valid\n";
        return false;
    }

    if (!pawn_entry_equal(entry_b, probed_b_after)) {
        std::clog << "[FAILURE] 'g_pawn_table_collision_overwrite' - Overwritten slot fields mismatch\n";
        return false;
    }

    return true;
}

// Writing the same hash again should replace its stored data.
static bool test_pawn_table_replace_same_hash() {
    g_pawn_table.clear();

    ZobristHash hash = 0x5555AAAA1234FEDCULL;

    PawnTableEntry entry_1;
    entry_1.hash = hash;
    entry_1.early_pawn_score = {10, 20};
    entry_1.late_pawn_score = {30, 40};

    PawnTableEntry entry_2;
    entry_2.hash = hash;
    entry_2.early_pawn_score = {-1, -2};
    entry_2.late_pawn_score = {-3, -4};

    g_pawn_table.add_entry(entry_1);
    g_pawn_table.add_entry(entry_2);

    PawnTableEntry& probed = g_pawn_table.get_entry(hash);
    if (!g_pawn_table.is_valid_entry(hash, probed)) {
        std::clog << "[FAILURE] 'g_pawn_table_replace_same_hash' - Entry should be valid after replacement\n";
        return false;
    }

    if (!pawn_entry_equal(entry_2, probed)) {
        std::clog << "[FAILURE] 'g_pawn_table_replace_same_hash' - Replaced entry fields mismatch\n";
        return false;
    }

    return true;
}

// get_pawn_score should cache by pawn hash:
// non-pawn moves keep same pawn hash and reuse same pawn evaluation,
// pawn moves change pawn hash and produce a new valid table entry.
static bool test_pawn_table_eval_cache(Board& b) {
    g_pawn_table.clear();
    b.load_from_fen(START_POS_FEN);

    ZobristHash start_pawn_hash = b.pawn_hash;
    PawnTableEntry start_entry = get_pawn_score(b);
    PawnTableEntry& start_probed = g_pawn_table.get_entry(start_pawn_hash);
    if (!g_pawn_table.is_valid_entry(start_pawn_hash, start_probed) || !pawn_entry_equal(start_entry, start_probed)) {
        std::clog << "[FAILURE] 'g_pawn_table_eval_cache' - Initial pawn entry missing or mismatched\n";
        return false;
    }

    b.make_move(encode_move_from_uci(b, "g1f3")); // Non-pawn move.
    if (b.pawn_hash != start_pawn_hash) {
        std::clog << "[FAILURE] 'g_pawn_table_eval_cache' - Pawn hash changed after non-pawn move\n";
        return false;
    }

    PawnTableEntry after_non_pawn = get_pawn_score(b);
    if (!pawn_entry_equal(start_entry, after_non_pawn)) {
        std::clog << "[FAILURE] 'g_pawn_table_eval_cache' - Pawn score changed despite identical pawn structure\n";
        return false;
    }

    b.make_move(encode_move_from_uci(b, "d7d5")); // Pawn move.
    if (b.pawn_hash == start_pawn_hash) {
        std::clog << "[FAILURE] 'g_pawn_table_eval_cache' - Pawn hash unchanged after pawn move\n";
        return false;
    }

    ZobristHash new_pawn_hash = b.pawn_hash;
    PawnTableEntry new_entry = get_pawn_score(b);
    PawnTableEntry& new_probed = g_pawn_table.get_entry(new_pawn_hash);
    if (!g_pawn_table.is_valid_entry(new_pawn_hash, new_probed) || !pawn_entry_equal(new_entry, new_probed)) {
        std::clog << "[FAILURE] 'g_pawn_table_eval_cache' - New pawn entry missing or mismatched\n";
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
    g_pawn_table.clear(); // Clean up global table state after test.
    return true;
}
