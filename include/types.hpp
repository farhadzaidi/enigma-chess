#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <limits>
#include <filesystem>
#include <limits>

// This is really just to silence the IDE warning since PROJECT_ROOT 
// should be defined in CMakeLists.txt
#ifndef PROJECT_ROOT
#define PROJECT_ROOT "./"
#endif

// --- Globals ---

extern std::atomic<bool> stop_requested;
extern bool use_own_book;
inline std::filesystem::path FEN_DIR = std::filesystem::path(PROJECT_ROOT) / "fen";

// --- FEN/EPD Files ---

inline const std::filesystem::path SINGLE_CHECK_EPD = FEN_DIR / "single_check.epd";
inline const std::filesystem::path DOUBLE_CHECK_EPD = FEN_DIR / "double_check.epd";
inline const std::filesystem::path NOT_IN_CHECK_FEN = FEN_DIR / "not_in_check.fen";
inline const std::filesystem::path MIXED_EPD = FEN_DIR / "mixed.epd";
inline const std::filesystem::path CPW_EPD = FEN_DIR / "cpw.epd";
inline const std::filesystem::path EN_PASSANT_EPD = FEN_DIR / "en_passant.epd";
inline const std::filesystem::path ENGINE_EPD = FEN_DIR / "engine.epd";
inline const std::filesystem::path GAMES_SAN = FEN_DIR / "games.san";

// --- Board Constants ---

constexpr int NUM_SQUARES    = 64;
constexpr int NUM_COLORS     = 2;
constexpr int NUM_PIECES     = 6;
constexpr int BOARD_SIZE     = 8;

// --- Bounds ---

constexpr int MAX_SEARCH_PLY       = 256;
constexpr int MAX_GAME_PLY         = 2048;
constexpr int FIFTY_MOVE_PLY_LIMIT = 100;

// Upper bound for the maximum number of moves we can generate at a given depth
constexpr int MAX_MOVES = 256;

// --- Type Definitions ---

using Bitboard          = uint64_t;
using ZobristHash       = uint64_t;
using MoveScore         = int32_t;
using MoveType          = uint16_t;
using MoveFlag          = uint16_t;
using PositionScore     = int16_t;
using Square            = uint8_t;
using Color             = uint8_t;
using Piece             = uint8_t;
using CastlingRights    = uint8_t;
using Rank              = uint8_t;
using File              = uint8_t;
using CastleType        = uint8_t;
using MoveSelectorPhase = uint8_t;
using SearchDepth       = uint8_t;
using TTNode            = uint8_t;
using Direction         = int;
using SearchMode        = int;
using MoveGenMode       = int;

// --- History Table Type Definitions ---

// color_piece_to[color][piece][to]
using ColorPieceToHistory = std::array<std::array<std::array<MoveScore, NUM_SQUARES>, NUM_PIECES>, NUM_COLORS>;

// from_to[from][to]
using FromToHistory = std::array<std::array<MoveScore, NUM_SQUARES>, NUM_SQUARES>;

// --- Scores ---
constexpr PositionScore CHECKMATE_SCORE    =  32'000;
constexpr PositionScore STALEMATE_SCORE    =  0;
constexpr PositionScore DUMMY_SCORE        = -32'700;
constexpr PositionScore SEARCH_INTERRUPTED = DUMMY_SCORE;

constexpr MoveScore MAX_MOVE_SCORE = 32'000;
constexpr MoveScore MIN_MOVE_SCORE = -MAX_MOVE_SCORE;

// --- Enums ---

enum SquareEnum : Square {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQUARE
};

enum DirectionEnum : Direction {
    NO_DIRECTION = 0,
    NORTH = 8,
    EAST = 1,
    SOUTH = -NORTH,
    WEST = -EAST,

    NORTHEAST = NORTH + EAST,
    NORTHWEST = NORTH + WEST,
    SOUTHEAST = SOUTH + EAST,
    SOUTHWEST = SOUTH + WEST,

    NORTH_NORTH = NORTH + NORTH,
    SOUTH_SOUTH = SOUTH + SOUTH
};

enum RankEnum : Rank {
    RANK_1,
    RANK_2,
    RANK_3,
    RANK_4,
    RANK_5,
    RANK_6,
    RANK_7,
    RANK_8
};

enum FileEnum : File {
    A_FILE,
    B_FILE,
    C_FILE,
    D_FILE,
    E_FILE,
    F_FILE,
    G_FILE,
    H_FILE,
};

enum ColorEnum: Color {
    WHITE,
    BLACK,
    NO_COLOR
};

enum PieceEnum : Piece {
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    NO_PIECE
};

enum CastlingRightsEnum : CastlingRights {
    NO_CASTLING_RIGHTS = 0b0,
    WHITE_SHORT        = 0b1,
    WHITE_LONG         = 0b10,
    BLACK_SHORT        = 0b100,
    BLACK_LONG         = 0b1000
};

enum MoveTypeEnum : MoveType {
    QUIET,
    CAPTURE
};

enum MoveFlagEnum : MoveFlag {
    NORMAL,
    EN_PASSANT,
    CASTLE,
    PROMOTION_BISHOP,
    PROMOTION_KNIGHT,
    PROMOTION_ROOK,
    PROMOTION_QUEEN
};

enum CastleTypeEnum : CastleType {
    NO_CASTLE_TYPE,
    WHITE_SHORT_CASTLE_TYPE,
    WHITE_LONG_CASTLE_TYPE,
    BLACK_SHORT_CASTLE_TYPE,
    BLACK_LONG_CASTLE_TYPE
};

enum SearchModeEnum : SearchMode {
    TIME,
    NODES,
    DEPTH,
    INFINITE
};

enum MoveSelectorPhaseEnum : MoveSelectorPhase {
    PREVIOUS_BEST,
    TRANSPOSITION,
    TACTICAL_MOVE,
    KILLER,
    QUIET_MOVE,
    BAD_CAPTURE
};

enum MoveGenModeEnum : MoveGenMode {
    ALL,
    QUIET_ONLY,
    CAPTURES_AND_PROMOTIONS
};

enum TTNodeEnum : TTNode {
    EXACT,
    FAIL_HIGH,
    FAIL_LOW,
    NO_TT_ENTRY
};

// Ranks and Files

constexpr Bitboard RANK_1_MASK = 0x00000000000000FFULL;
constexpr Bitboard RANK_2_MASK = 0x000000000000FF00ULL;
constexpr Bitboard RANK_3_MASK = 0x0000000000FF0000ULL;
constexpr Bitboard RANK_4_MASK = 0x00000000FF000000ULL;
constexpr Bitboard RANK_5_MASK = 0x000000FF00000000ULL;
constexpr Bitboard RANK_6_MASK = 0x0000FF0000000000ULL;
constexpr Bitboard RANK_7_MASK = 0x00FF000000000000ULL;
constexpr Bitboard RANK_8_MASK = 0xFF00000000000000ULL;

constexpr Bitboard A_FILE_MASK = 0x0101010101010101ULL;
constexpr Bitboard B_FILE_MASK = 0x0202020202020202ULL;
constexpr Bitboard C_FILE_MASK = 0x0404040404040404ULL;
constexpr Bitboard D_FILE_MASK = 0x0808080808080808ULL;
constexpr Bitboard E_FILE_MASK = 0x1010101010101010ULL;
constexpr Bitboard F_FILE_MASK = 0x2020202020202020ULL;
constexpr Bitboard G_FILE_MASK = 0x4040404040404040ULL;
constexpr Bitboard H_FILE_MASK = 0x8080808080808080ULL;

constexpr std::array<Bitboard, BOARD_SIZE> FILE_MASKS = {
    A_FILE_MASK,
    B_FILE_MASK,
    C_FILE_MASK,
    D_FILE_MASK,
    E_FILE_MASK,
    F_FILE_MASK,
    G_FILE_MASK,
    H_FILE_MASK
};

constexpr std::array<Bitboard, BOARD_SIZE> RANK_MASKS = {
    RANK_1_MASK,
    RANK_2_MASK,
    RANK_3_MASK,
    RANK_4_MASK,
    RANK_5_MASK,
    RANK_6_MASK,
    RANK_7_MASK,
    RANK_8_MASK
};

// --- Sentinel Values ---

constexpr Bitboard EMPTY_BITBOARD = 0;

// --- FEN Strings ---

constexpr const char* START_POS_FEN = 
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr const char* KIWIPETE_FEN = 
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
constexpr const char* POSITION_3_FEN = // Castling, en passant, and promotions
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
constexpr const char* POSITION_4_FEN = // En passant legality
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
constexpr const char* POSITION_5_FEN = // Quiet move edge cases
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8  ";
constexpr const char* POSITION_6_FEN = // Promotion + check
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10 ";


// --- Evaluation ---
// Credit: https://www.chessprogramming.org/PeSTO%27s_Evaluation_Function

using PieceValues = std::array<int, NUM_PIECES>;
using PieceSquareTable = std::array<std::array<int, NUM_SQUARES>, NUM_PIECES>;
using EvalTable = std::array<std::array<std::array<int, NUM_SQUARES>, NUM_PIECES>, NUM_COLORS> ;

constexpr PieceValues EARLY_PIECE_VALUES = {
    82, 337, 365, 477, 1025, 0
};

constexpr PieceValues LATE_PIECE_VALUES = {
    94, 281, 297, 512, 936, 0
};

// Piece Square Tables
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


constexpr EvalTable create_eval_table(
    const PieceValues& piece_values,
    const PieceSquareTable& psqt
) {
    EvalTable table{};
    for (Piece p = PAWN; p < NUM_PIECES; p++) {
        for (Square sq = 0; sq < NUM_SQUARES; sq++) {
            // PSQT data is laid out from rank 8 to rank 1 (A8..H1)
            // Our square indexing is A1..H8, so white requires a vertical flip
            table[WHITE][p][sq] = piece_values[p] + psqt[p][sq ^ 56];
            table[BLACK][p][sq] = piece_values[p] + psqt[p][sq];
        }
    }
    return table;
}

constexpr EvalTable EARLY_EVAL_TABLE = create_eval_table(EARLY_PIECE_VALUES, EARLY_PSQT);
constexpr EvalTable LATE_EVAL_TABLE  = create_eval_table(LATE_PIECE_VALUES, LATE_PSQT);

// How much each piece type contributes to the game phase
constexpr std::array<int, NUM_PIECES> GAME_PHASE_INCREMENT = {0, 1, 1, 2, 4, 0};
constexpr int MAX_GAME_PHASE = []{
    return (
        8 * GAME_PHASE_INCREMENT[PAWN]   +
        2 * GAME_PHASE_INCREMENT[KNIGHT] + 
        2 * GAME_PHASE_INCREMENT[BISHOP] + 
        2 * GAME_PHASE_INCREMENT[ROOK]   + 
        1 * GAME_PHASE_INCREMENT[QUEEN]  + 
        1 * GAME_PHASE_INCREMENT[KING]
    ) * 2;
}();