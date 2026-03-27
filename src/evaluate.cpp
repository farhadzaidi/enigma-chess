#include "evaluate.hpp"

#include <algorithm>
#include <array>
#include <bit>

#include "bitboard.hpp"
#include "pawn_table.hpp"
#include "move_generator.hpp"

namespace {

// --- PSQT ---

using PieceValues = std::array<int, NUM_PIECES>;
using PieceSquareTable = std::array<std::array<int, NUM_SQUARES>, NUM_PIECES>;

/** Middlegame material values. */
constexpr PieceValues EARLY_PIECE_VALUES = {
    82, 337, 365, 477, 1025, 0
};

/** Endgame material values. */
constexpr PieceValues LATE_PIECE_VALUES = {
    94, 281, 297, 512, 936, 0
};

constexpr PieceSquareTable EARLY_PSQT = {{
    // PAWN
    {{
          0,   0,   0,   0,   0,   0,   0,   0,
         98, 134,  61,  95,  68, 126,  34, -11,
         -6,   7,  26,  31,  65,  56,  25, -20,
        -14,  13,   6,  21,  23,  12,  17, -23,
        -27,  -2,  -5,  12,  17,   6,  10, -25,
        -26,  -4,  -4, -10,   3,   3,  33, -12,
        -35,  -1, -20, -23, -15,  24,  38, -22,
          0,   0,   0,   0,   0,   0,   0,   0,
    }},
    // KNIGHT
    {{
       -167, -89, -34, -49,  61, -97, -15, -107,
        -73, -41,  72,  36,  23,  62,   7,  -17,
        -47,  60,  37,  65,  84, 129,  73,   44,
         -9,  17,  19,  53,  37,  69,  18,   22,
        -13,   4,  16,  13,  28,  19,  21,   -8,
        -23,  -9,  12,  10,  19,  17,  25,  -16,
        -29, -53, -12,  -3,  -1,  18, -14,  -19,
       -105, -21, -58, -33, -17, -28, -19,  -23,
    }},
    // BISHOP
    {{
        -29,   4, -82, -37, -25, -42,   7,  -8,
        -26,  16, -18, -13,  30,  59,  18, -47,
        -16,  37,  43,  40,  35,  50,  37,  -2,
         -4,   5,  19,  50,  37,  37,   7,  -2,
         -6,  13,  13,  26,  34,  12,  10,   4,
          0,  15,  15,  15,  14,  27,  18,  10,
          4,  15,  16,   0,   7,  21,  33,   1,
        -33,  -3, -14, -21, -13, -12, -39, -21,
    }},
    // ROOK
    {{
         32,  42,  32,  51,  63,   9,  31,  43,
         27,  32,  58,  62,  80,  67,  26,  44,
         -5,  19,  26,  36,  17,  45,  61,  16,
        -24, -11,   7,  26,  24,  35,  -8, -20,
        -36, -26, -12,  -1,   9,  -7,   6, -23,
        -45, -25, -16, -17,   3,   0,  -5, -33,
        -44, -16, -20,  -9,  -1,  11,  -6, -71,
        -19, -13,   1,  17,  16,   7, -37, -26,
    }},
    // QUEEN
    {{
        -28,   0,  29,  12,  59,  44,  43,  45,
        -24, -39,  -5,   1, -16,  57,  28,  54,
        -13, -17,   7,   8,  29,  56,  47,  57,
        -27, -27, -16, -16,  -1,  17,  -2,   1,
         -9, -26,  -9, -10,  -2,  -4,   3,  -3,
        -14,   2, -11,  -2,  -5,   2,  14,   5,
        -35,  -8,  11,   2,   8,  15,  -3,   1,
         -1, -18,  -9,  10, -15, -25, -31, -50,
    }},
    // KING
    {{
        -65,  23,  16, -15, -56, -34,   2,  13,
         29,  -1, -20,  -7,  -8,  -4, -38, -29,
         -9,  24,   2, -16, -20,   6,  22, -22,
        -17, -20, -12, -27, -30, -25, -14, -36,
        -49,  -1, -27, -39, -46, -44, -33, -51,
        -14, -14, -22, -46, -44, -30, -15, -27,
          1,   7,  -8, -64, -43, -16,   9,   8,
        -15,  36,  12, -54,   8, -28,  24,  14,
    }},
}};

constexpr PieceSquareTable LATE_PSQT = {{
    // PAWN
    {{
          0,   0,   0,   0,   0,   0,   0,   0,
        178, 173, 158, 134, 147, 132, 165, 187,
         94, 100,  85,  67,  56,  53,  82,  84,
         32,  24,  13,   5,  -2,   4,  17,  17,
         13,   9,  -3,  -7,  -7,  -8,   3,  -1,
          4,   7,  -6,   1,   0,  -5,  -1,  -8,
         13,   8,   8,  10,  13,   0,   2,  -7,
          0,   0,   0,   0,   0,   0,   0,   0,
    }},
    // KNIGHT
    {{
        -58, -38, -13, -28, -31, -27, -63, -99,
        -25,  -8, -25,  -2,  -9, -25, -24, -52,
        -24, -20,  10,   9,  -1,  -9, -19, -41,
        -17,   3,  22,  22,  22,  11,   8, -18,
        -18,  -6,  16,  25,  16,  17,   4, -18,
        -23,  -3,  -1,  15,  10,  -3, -20, -22,
        -42, -20, -10,  -5,  -2, -20, -23, -44,
        -29, -51, -23, -15, -22, -18, -50, -64,
    }},
    // BISHOP
    {{
        -14, -21, -11,  -8,  -7,  -9, -17, -24,
         -8,  -4,   7, -12,  -3, -13,  -4, -14,
          2,  -8,   0,  -1,  -2,   6,   0,   4,
         -3,   9,  12,   9,  14,  10,   3,   2,
         -6,   3,  13,  19,   7,  10,  -3,  -9,
        -12,  -3,   8,  10,  13,   3,  -7, -15,
        -14, -18,  -7,  -1,   4,  -9, -15, -27,
        -23,  -9, -23,  -5,  -9, -16,  -5, -17,
    }},
    // ROOK
    {{
         13,  10,  18,  15,  12,  12,   8,   5,
         11,  13,  13,  11,  -3,   3,   8,   3,
          7,   7,   7,   5,   4,  -3,  -5,  -3,
          4,   3,  13,   1,   2,   1,  -1,   2,
          3,   5,   8,   4,  -5,  -6,  -8, -11,
         -4,   0,  -5,  -1,  -7, -12,  -8, -16,
         -6,  -6,   0,   2,  -9,  -9, -11,  -3,
         -9,   2,   3,  -1,  -5, -13,   4, -20,
    }},
    // QUEEN
    {{
         -9,  22,  22,  27,  27,  19,  10,  20,
        -17,  20,  32,  41,  58,  25,  30,   0,
        -20,   6,   9,  49,  47,  35,  19,   9,
          3,  22,  24,  45,  57,  40,  57,  36,
        -18,  28,  19,  47,  31,  34,  39,  23,
        -16, -27,  15,   6,   9,  17,  10,   5,
        -22, -23, -30, -16, -16, -23, -36, -32,
        -33, -28, -22, -43,  -5, -32, -20, -41,
    }},
    // KING
    {{
        -74, -35, -18, -18, -11,  15,   4, -17,
        -12,  17,  14,  17,  17,  38,  23,  11,
         10,  17,  23,  15,  20,  45,  44,  13,
         -8,  22,  24,  27,  26,  33,  26,   3,
        -18,  -4,  21,  24,  27,  23,   9, -11,
        -19,  -3,  11,  21,  23,  16,   7,  -9,
        -27, -11,   4,  13,  14,   4,  -5, -17,
        -53, -34, -21, -11, -28, -14, -24, -43,
    }},
}};

/** Build a full eval table by combining material values with piece-square bonuses. */
constexpr EvalTable create_eval_table(
    const PieceValues& piece_values,
    const PieceSquareTable& psqt
) {
    EvalTable table{};
    for (Piece piece = PAWN; piece < NUM_PIECES; piece++) {
        for (Square sq = 0; sq < NUM_SQUARES; sq++) {
            table[WHITE][piece][sq] = piece_values[piece] + psqt[piece][flip_square(sq)];
            table[BLACK][piece][sq] = piece_values[piece] + psqt[piece][sq];
        }
    }
    return table;
}

// --- Eval Constants ---

// Mobility bonus tables, indexed by number of legal moves for a piece.
constexpr std::array<PositionScore, 9> KNIGHT_MOBILITY_EARLY = {
    -25, -15,  -5,   0,   5,  10,  14,  16,  18
};
constexpr std::array<PositionScore, 9> KNIGHT_MOBILITY_LATE = {
    -30, -18,  -6,   0,   6,  12,  16,  18,  20
};

constexpr std::array<PositionScore, 15> BISHOP_MOBILITY_EARLY = {
    -20, -15, -10,  -5,   0,   5,   8,  10,  12,  13,  14,  14,  15,  15,  15
};
constexpr std::array<PositionScore, 15> BISHOP_MOBILITY_LATE = {
    -25, -18, -12,  -6,   0,   6,  10,  13,  15,  16,  17,  17,  18,  18,  18
};

constexpr std::array<PositionScore, 16> ROOK_MOBILITY_EARLY = {
    -20, -15, -10,  -5,  -2,   0,   3,   5,   7,   8,   9,  10,  10,  10,  10,  10
};
constexpr std::array<PositionScore, 16> ROOK_MOBILITY_LATE = {
    -30, -20, -12,  -6,  -2,   0,   5,   8,  11,  13,  14,  15,  16,  16,  16,  16
};

constexpr std::array<PositionScore, 29> QUEEN_MOBILITY_EARLY = {
    -15, -10,  -8,  -5,  -3,  -1,   0,   1,   2,   3,   4,   5,   5,   6,   6,
      6,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7
};
constexpr std::array<PositionScore, 29> QUEEN_MOBILITY_LATE = {
    -20, -14, -10,  -6,  -3,  -1,   0,   2,   4,   6,   7,   8,   9,  10,  10,
     11,  11,  11,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12
};

/** Penalty based on the number of friendly pawns directly shielding the king (0-3). */
constexpr std::array<PositionScore, 4> KING_SHIELD_PENALTY = {-45, -25, -10, 0};

constexpr PositionScore EARLY_BISHOP_PAIR_BONUS = 20;
constexpr PositionScore LATE_BISHOP_PAIR_BONUS = 40;

/** Precomputed masks: squares directly in front of the king on its file and adjacent files. */
const std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SIDES> KING_SHIELD_MASKS = []() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SIDES> masks{};
    for (Square sq = 0; sq < NUM_SQUARES; sq++) {
        int file = get_file(sq);
        int rank = get_rank(sq);
        Bitboard adjacent_files = EMPTY_BITBOARD;
        if (file > A_FILE) adjacent_files |= FILE_MASKS[file - 1];
        if (file < H_FILE) adjacent_files |= FILE_MASKS[file + 1];
        Bitboard file_mask_inclusive = FILE_MASKS[file] | adjacent_files;

        Bitboard white_rank_in_front = rank < RANK_8 ? RANK_MASKS[rank + 1] : EMPTY_BITBOARD;
        masks[WHITE][sq] = file_mask_inclusive & white_rank_in_front;

        Bitboard black_rank_in_front = rank > RANK_1 ? RANK_MASKS[rank - 1] : EMPTY_BITBOARD;
        masks[BLACK][sq] = file_mask_inclusive & black_rank_in_front;
    }

    return masks;
}();

// --- Mobility ---

struct MobilityScore {
    PositionScore early = 0;
    PositionScore late = 0;
};

/** Sum mobility bonuses for all non-pawn, non-king pieces of a given side. */
MobilityScore compute_side_mobility(const Board& b, Side side, Bitboard occupied) {
    MobilityScore score;
    Bitboard friendly_pieces = b.sides()[side];

    Bitboard knights = b.pieces()[side][KNIGHT];
    while (knights) {
        Square sq = pop_lsb(knights);
        int moves = std::popcount(get_piece_attacks<KNIGHT>(sq, occupied) & ~friendly_pieces);
        score.early += KNIGHT_MOBILITY_EARLY[moves];
        score.late  += KNIGHT_MOBILITY_LATE[moves];
    }

    Bitboard bishops = b.pieces()[side][BISHOP];
    while (bishops) {
        Square sq = pop_lsb(bishops);
        int moves = std::popcount(get_piece_attacks<BISHOP>(sq, occupied) & ~friendly_pieces);
        score.early += BISHOP_MOBILITY_EARLY[moves];
        score.late  += BISHOP_MOBILITY_LATE[moves];
    }

    Bitboard rooks = b.pieces()[side][ROOK];
    while (rooks) {
        Square sq = pop_lsb(rooks);
        int moves = std::popcount(get_piece_attacks<ROOK>(sq, occupied) & ~friendly_pieces);
        score.early += ROOK_MOBILITY_EARLY[moves];
        score.late  += ROOK_MOBILITY_LATE[moves];
    }

    Bitboard queens = b.pieces()[side][QUEEN];
    while (queens) {
        Square sq = pop_lsb(queens);
        int moves = std::popcount(
            get_piece_attacks<QUEEN>(sq, occupied) & ~friendly_pieces
        );
        score.early += QUEEN_MOBILITY_EARLY[moves];
        score.late  += QUEEN_MOBILITY_LATE[moves];
    }

    return score;
}

/** Return the net mobility advantage (friendly minus enemy) for early and late game. */
MobilityScore get_mobility_score(const Board& b) {
    Side friendly_side = b.to_move();
    Side enemy_side = opposite_side(friendly_side);
    Bitboard occupied = b.occupied();

    MobilityScore friendly_mobility = compute_side_mobility(b, friendly_side, occupied);
    MobilityScore enemy_mobility = compute_side_mobility(b, enemy_side, occupied);

    return {
        static_cast<PositionScore>(friendly_mobility.early - enemy_mobility.early),
        static_cast<PositionScore>(friendly_mobility.late - enemy_mobility.late)
    };
}

} // namespace

// --- Eval tables ---

const EvalTable EARLY_EVAL_TABLE = create_eval_table(EARLY_PIECE_VALUES, EARLY_PSQT);
const EvalTable LATE_EVAL_TABLE = create_eval_table(LATE_PIECE_VALUES, LATE_PSQT);

// --- Evaluate ---

PositionScore evaluate(const Board& b) {
    Side friendly_side = b.to_move();
    Side enemy_side = opposite_side(friendly_side);

    // --- Pawn structure ---
    PawnTableEntry pt_entry = g_pawn_table.get_pawn_score(b);
    PositionScore early_pawn_score = pt_entry.early_pawn_score[friendly_side] - pt_entry.early_pawn_score[enemy_side];
    PositionScore late_pawn_score = pt_entry.late_pawn_score[friendly_side] - pt_entry.late_pawn_score[enemy_side];

    // --- Bishop pair bonus ---
    PositionScore early_bishop_pair_score = 0;
    PositionScore late_bishop_pair_score = 0;

    if (std::popcount(b.pieces()[friendly_side][BISHOP]) >= 2) {
        early_bishop_pair_score += EARLY_BISHOP_PAIR_BONUS;
        late_bishop_pair_score += LATE_BISHOP_PAIR_BONUS;
    }

    if (std::popcount(b.pieces()[enemy_side][BISHOP]) >= 2) {
        early_bishop_pair_score -= EARLY_BISHOP_PAIR_BONUS;
        late_bishop_pair_score -= LATE_BISHOP_PAIR_BONUS;
    }

    // --- Piece mobility ---
    MobilityScore mobility = get_mobility_score(b);

    // --- King safety: reward pawns shielding the king ---
    int friendly_shield = std::popcount(KING_SHIELD_MASKS[friendly_side][b.king_squares()[friendly_side]] & b.pieces()[friendly_side][PAWN]);
    int enemy_shield = std::popcount(KING_SHIELD_MASKS[enemy_side][b.king_squares()[enemy_side]] & b.pieces()[enemy_side][PAWN]);
    PositionScore king_safety_score = KING_SHIELD_PENALTY[friendly_shield] - KING_SHIELD_PENALTY[enemy_shield];

    // --- Aggregate early-middle game score ---
    // Material + PSQT scores are incrementally updated in the Board.
    PositionScore net_early_score = (
        b.early_scores()[friendly_side] - b.early_scores()[enemy_side] +
        early_pawn_score +
        early_bishop_pair_score +
        king_safety_score +
        mobility.early
    );

    // --- Aggregate late-game score ---
    PositionScore net_late_score = (
        b.late_scores()[friendly_side] - b.late_scores()[enemy_side] +
        late_pawn_score +
        late_bishop_pair_score +
        mobility.late
    );

    // --- Tapered eval: blend early and late scores based on remaining material ---
    int early_multiplier = std::min(b.game_phase(), MAX_GAME_PHASE);
    int late_multiplier = MAX_GAME_PHASE - early_multiplier;

    return (net_early_score * early_multiplier + net_late_score * late_multiplier) / MAX_GAME_PHASE;
}
