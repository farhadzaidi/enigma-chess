#include "pawn_table.hpp"

#include <array>

#include "bitboard.hpp"

namespace {

// --- Pawn-structure evaluation constants ---

// Isolated pawn: no friendly pawn on either adjacent file.
constexpr PositionScore EARLY_ISOLATED_PAWN_PENALTY = -10;
constexpr PositionScore LATE_ISOLATED_PAWN_PENALTY = -12;

// Doubled (stacked) pawn: another friendly pawn shares the same file.
constexpr PositionScore EARLY_STACKED_PAWN_PENALTY = -8;
constexpr PositionScore LATE_STACKED_PAWN_PENALTY = -10;

// --- Lookup Tables ---

/** Passed pawn bonus by rank (middlegame), indexed [side][rank]. */
constexpr auto EARLY_PASSED_PAWN_BONUS = []() {
    std::array<PositionScore, BOARD_SIZE> bonus = { 0, 0, 3, 8, 15, 25, 40, 0 };
    std::array<std::array<PositionScore, BOARD_SIZE>, NUM_SIDES> passed_pawn_bonus = {};
    for (int rank = RANK_1; rank <= RANK_8; rank++) {
        passed_pawn_bonus[WHITE][rank] = bonus[rank];
        passed_pawn_bonus[BLACK][rank] = bonus[BOARD_SIZE - 1 - rank];
    }

    return passed_pawn_bonus;
}();

/** Passed pawn bonus by rank (endgame), indexed [side][rank]. */
constexpr auto LATE_PASSED_PAWN_BONUS = []() {
    std::array<PositionScore, BOARD_SIZE> bonus = { 0, 0, 5, 12, 25, 45, 75, 0 };
    std::array<std::array<PositionScore, BOARD_SIZE>, NUM_SIDES> passed_pawn_bonus = {};
    for (int rank = RANK_1; rank <= RANK_8; rank++) {
        passed_pawn_bonus[WHITE][rank] = bonus[rank];
        passed_pawn_bonus[BLACK][rank] = bonus[BOARD_SIZE - 1 - rank];
    }

    return passed_pawn_bonus;
}();

/** Bitboard mask of the two files adjacent to a square's file (excluding its own file). */
const std::array<Bitboard, NUM_SQUARES> ADJACENT_FILE_MASKS = []() {
    std::array<Bitboard, NUM_SQUARES> masks{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int file = get_file(sq);
        Bitboard left_file = file - 1 >= A_FILE ? FILE_MASKS[file - 1] : EMPTY_BITBOARD;
        Bitboard right_file = file + 1 <= H_FILE ? FILE_MASKS[file + 1] : EMPTY_BITBOARD;
        masks[sq] = left_file | right_file;
    }

    return masks;
}();

/** Same as ADJACENT_FILE_MASKS but also includes the square's own file. */
const std::array<Bitboard, NUM_SQUARES> ADJACENT_FILE_MASKS_INCLUSIVE = []() {
    std::array<Bitboard, NUM_SQUARES> masks{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int file = get_file(sq);
        Bitboard adjacent_files = EMPTY_BITBOARD;
        if (file > A_FILE) adjacent_files |= FILE_MASKS[file - 1];
        if (file < H_FILE) adjacent_files |= FILE_MASKS[file + 1];
        masks[sq] = adjacent_files | FILE_MASKS[file];
    }
    return masks;
}();

/** Mask of all squares an enemy pawn could occupy to block this pawn from being passed. */
const std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SIDES> PASSED_PAWN_MASKS = []() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SIDES> masks{};

    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int file = get_file(sq);
        int rank = get_rank(sq);
        Bitboard ranks_in_front = EMPTY_BITBOARD;
        for (int front_rank = rank + 1; front_rank < BOARD_SIZE; front_rank++) {
            ranks_in_front |= RANK_MASKS[front_rank];
        }
        Bitboard adjacent_files = EMPTY_BITBOARD;
        if (file > A_FILE) adjacent_files |= FILE_MASKS[file - 1];
        if (file < H_FILE) adjacent_files |= FILE_MASKS[file + 1];
        masks[WHITE][sq] = ranks_in_front & (adjacent_files | FILE_MASKS[file]);
    }

    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int file = get_file(sq);
        int rank = get_rank(sq);
        Bitboard ranks_in_front = EMPTY_BITBOARD;
        for (int front_rank = rank - 1; front_rank >= RANK_1; front_rank--) {
            ranks_in_front |= RANK_MASKS[front_rank];
        }
        Bitboard adjacent_files = EMPTY_BITBOARD;
        if (file > A_FILE) adjacent_files |= FILE_MASKS[file - 1];
        if (file < H_FILE) adjacent_files |= FILE_MASKS[file + 1];
        masks[BLACK][sq] = ranks_in_front & (adjacent_files | FILE_MASKS[file]);
    }

    return masks;
}();

} // namespace


PawnTableEntry::PawnTableEntry() = default;

PawnTable::PawnTable() {
    clear();
}

void PawnTable::clear() {
    table_.fill(PawnTableEntry{});
}

PawnTableEntry PawnTable::get_pawn_score(const Board& b) {
    PawnTableEntry entry = get_entry(b.pawn_hash());
    if (is_valid_entry(b.pawn_hash(), entry)) {
        return entry;
    }

    PawnTableEntry new_entry;
    new_entry.hash = b.pawn_hash();

    auto score_side = [&](Side side) {
        Side enemy_side = opposite_side(side);

        PositionScore early_score = 0;
        PositionScore late_score = 0;

        Bitboard friendly_pawns = b.pieces()[side][PAWN];
        Bitboard friendly_pawns_copy = friendly_pawns;
        Bitboard enemy_pawns = b.pieces()[enemy_side][PAWN];

        while (friendly_pawns_copy) {
            Square sq = pop_lsb(friendly_pawns_copy);
            int file = get_file(sq);
            int rank = get_rank(sq);

            // Passed pawn: no enemy pawns ahead on this or adjacent files.
            Bitboard passed_pawn_mask = PASSED_PAWN_MASKS[side][sq];
            if ((passed_pawn_mask & enemy_pawns) == 0) {
                early_score += EARLY_PASSED_PAWN_BONUS[side][rank];
                late_score += LATE_PASSED_PAWN_BONUS[side][rank];
            }

            // Isolated pawn: no friendly pawns on neighboring files to support it.
            if ((ADJACENT_FILE_MASKS[sq] & friendly_pawns) == 0) {
                early_score += EARLY_ISOLATED_PAWN_PENALTY;
                late_score += LATE_ISOLATED_PAWN_PENALTY;
            }

            // Doubled pawn: another friendly pawn on the same file.
            Bitboard file_mask = FILE_MASKS[file] ^ get_mask(sq);
            if ((file_mask & friendly_pawns) != 0) {
                early_score += EARLY_STACKED_PAWN_PENALTY;
                late_score += LATE_STACKED_PAWN_PENALTY;
            }
        }

        new_entry.early_pawn_score[side] = early_score;
        new_entry.late_pawn_score[side] = late_score;
    };

    score_side(WHITE);
    score_side(BLACK);

    add_entry(new_entry);
    return new_entry;
}

PawnTableEntry& PawnTable::get_entry(ZobristHash hash) {
    return table_[get_index(hash)];
}

void PawnTable::add_entry(const PawnTableEntry& entry) {
    table_[get_index(entry.hash)] = entry;
}

bool PawnTable::is_valid_entry(ZobristHash hash, const PawnTableEntry& entry) const {
    return hash == entry.hash;
}

uint64_t PawnTable::get_index(ZobristHash hash) const {
    return hash & (PAWN_TABLE_SIZE - 1);
}
