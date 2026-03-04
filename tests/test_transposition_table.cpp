#include <array>
#include <iostream>

#include "core/move.hpp"
#include "core/transposition_table.hpp"
#include "core/types.hpp"

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
    TT.clear();
    TT.generation = 7;

    ZobristHash hash = 0xDEADBEEF12345678ULL;
    Move move(E2, E4, MoveType::Quiet, MoveFlag::Normal);
    TTEntry entry(hash, move, 10, 150, TTNode::Exact);

    TT.add_entry(entry);
    TTEntry* probed = TT.get_entry(hash);
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
    TT.clear();

    ZobristHash empty_hash = 0x1234567890ABCDEFULL;
    if (TT.get_entry(empty_hash)) {
        std::clog << "[FAILURE] 'tt_invalid_probe' - Empty probe should return nullptr\n";
        return false;
    }

    ZobristHash stored_hash = colliding_hash(1);
    ZobristHash wrong_hash = colliding_hash(2);
    TT.add_entry(TTEntry(stored_hash, NULL_MOVE, 5, 100, TTNode::Exact));

    if (TT.get_entry(wrong_hash)) {
        std::clog << "[FAILURE] 'tt_invalid_probe' - Mismatched hash should not be found\n";
        return false;
    }

    return true;
}

// clear() should invalidate entries and reset generation.
static bool test_tt_clear() {
    TT.clear();
    TT.generation = 3;

    ZobristHash hash = 0xABCDEF0123456789ULL;
    TT.add_entry(TTEntry(hash, Move(E2, E4, MoveType::Quiet, MoveFlag::Normal), 6, 42, TTNode::Exact));
    TT.clear();

    if (TT.get_entry(hash)) {
        std::clog << "[FAILURE] 'tt_clear' - Entry should not exist after clear()\n";
        return false;
    }

    if (TT.generation != 0) {
        std::clog << "[FAILURE] 'tt_clear' - Generation should reset to 0 after clear()\n";
        return false;
    }

    return true;
}

// Up to bucket size colliding hashes should coexist in the same bucket.
static bool test_tt_bucket_collisions_fit() {
    TT.clear();
    TT.generation = 4;

    std::array<TTEntry, TRANSPOSITION_TABLE_BUCKET_SIZE> entries = {
        TTEntry(colliding_hash(1), Move(E2, E4, MoveType::Quiet, MoveFlag::Normal), 4, 10, TTNode::Exact),
        TTEntry(colliding_hash(2), Move(D2, D4, MoveType::Quiet, MoveFlag::Normal), 5, 20, TTNode::FailHigh),
        TTEntry(colliding_hash(3), Move(G1, F3, MoveType::Quiet, MoveFlag::Normal), 6, 30, TTNode::FailLow),
        TTEntry(colliding_hash(4), Move(C2, C4, MoveType::Quiet, MoveFlag::Normal), 7, 40, TTNode::Exact),
    };

    for (const auto& entry : entries) {
        TT.add_entry(entry);
    }

    for (const auto& entry : entries) {
        TTEntry* probed = TT.get_entry(entry.hash);
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
    TT.clear();
    ZobristHash hash = 0x5555AAAA1234FEDCULL;

    TT.generation = 1;
    TT.add_entry(TTEntry(hash, Move(B1, C3, MoveType::Quiet, MoveFlag::Normal), 3, 50, TTNode::FailLow));

    TT.generation = 5;
    TT.add_entry(TTEntry(hash, Move(B1, A3, MoveType::Quiet, MoveFlag::Normal), 8, 220, TTNode::Exact));

    TTEntry* probed = TT.get_entry(hash);
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
    for (const auto& bucket_entry : TT.table[index]) {
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
    TT.clear();
    TT.generation = 0;

    std::array<TTEntry, TRANSPOSITION_TABLE_BUCKET_SIZE> entries = {
        TTEntry(colliding_hash(11), Move(A2, A3, MoveType::Quiet, MoveFlag::Normal), 8, 10, TTNode::Exact),
        TTEntry(colliding_hash(12), Move(B2, B3, MoveType::Quiet, MoveFlag::Normal), 4, 11, TTNode::Exact),
        TTEntry(colliding_hash(13), Move(C2, C3, MoveType::Quiet, MoveFlag::Normal), 12, 12, TTNode::Exact),
        TTEntry(colliding_hash(14), Move(D2, D3, MoveType::Quiet, MoveFlag::Normal), 10, 13, TTNode::Exact),
    };

    for (const auto& entry : entries) {
        TT.add_entry(entry);
    }

    ZobristHash should_be_replaced = entries[1].hash; // Lowest depth = 4
    TT.add_entry(TTEntry(colliding_hash(15), Move(E2, E3, MoveType::Quiet, MoveFlag::Normal), 6, 99, TTNode::FailHigh));

    if (TT.get_entry(should_be_replaced)) {
        std::clog << "[FAILURE] 'tt_bucket_replacement_by_depth' - Lowest depth entry should be replaced\n";
        return false;
    }

    TTEntry* replacement = TT.get_entry(colliding_hash(15));
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
    TT.clear();

    std::array<ZobristHash, TRANSPOSITION_TABLE_BUCKET_SIZE> hashes = {
        colliding_hash(21),
        colliding_hash(22),
        colliding_hash(23),
        colliding_hash(24),
    };

    for (size_t i = 0; i < hashes.size(); i++) {
        TT.generation = static_cast<int>(i);
        TT.add_entry(TTEntry(
            hashes[i],
            Move(static_cast<Square>(A2 + i), static_cast<Square>(A3 + i), MoveType::Quiet, MoveFlag::Normal),
            20,
            100,
            TTNode::Exact
        ));
    }

    // Entry at hashes[0] is oldest and should be replaced first when values differ by age only.
    TT.generation = 8;
    ZobristHash replacement_hash = colliding_hash(25);
    TT.add_entry(TTEntry(replacement_hash, Move(H2, H3, MoveType::Quiet, MoveFlag::Normal), 20, 200, TTNode::FailLow));

    if (TT.get_entry(hashes[0])) {
        std::clog << "[FAILURE] 'tt_bucket_replacement_by_age' - Oldest entry should be replaced\n";
        return false;
    }

    for (size_t i = 1; i < hashes.size(); i++) {
        if (!TT.get_entry(hashes[i])) {
            std::clog << "[FAILURE] 'tt_bucket_replacement_by_age' - Newer entry was replaced unexpectedly\n";
            return false;
        }
    }

    TTEntry* replacement = TT.get_entry(replacement_hash);
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
    TT.clear(); // Clean up global table state after test.
    return true;
}
