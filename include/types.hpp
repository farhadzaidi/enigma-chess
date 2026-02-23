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
using MoveScore         = uint32_t;
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
constexpr PositionScore MAX_SCORE          =  30'000;
constexpr PositionScore MIN_SCORE          = -MAX_SCORE;
constexpr PositionScore CHECKMATE_SCORE    =  32'000;
constexpr PositionScore STALEMATE_SCORE    =  0;
constexpr PositionScore DUMMY_SCORE        = -32'700;
constexpr PositionScore SEARCH_INTERRUPTED = DUMMY_SCORE;

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
    TRANSPOSITION,
    GOOD_CAPTURE,
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
constexpr std::array<int, NUM_PIECES> PIECE_VALUE = {
    // PAWN, KNIGHT/BISHOP, ROOK, QUEEN, KING
    100, 300, 300, 500, 900, 0 // King adds nothing to material value since it can never be captured
};

