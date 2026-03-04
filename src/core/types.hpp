#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// --- Type Definitions ---

using Bitboard      = uint64_t;
using ZobristHash   = uint64_t;
using MoveScore     = int32_t;
using PositionScore = int16_t;
using Square        = uint8_t;
using Side          = uint8_t;
using Piece         = uint8_t;
using CastlingRights = uint8_t;
using SearchDepth   = uint8_t;
using Direction     = int;

// --- Scoped Enums ---

enum class MoveType : uint16_t {
    Quiet   = 0,
    Capture = 1
};

enum class MoveFlag : uint16_t {
    Normal         = 0,
    EnPassant      = 1,
    Castle         = 2,
    PromoBishop    = 3,
    PromoKnight    = 4,
    PromoRook      = 5,
    PromoQueen     = 6
};

enum class MoveGenMode : uint8_t {
    All,
    QuietOnly,
    TacticalOnly
};

enum class SearchMode : uint8_t {
    Time,
    Nodes,
    Depth,
    Infinite
};

enum class TTNode : uint8_t {
    Exact,
    FailHigh,
    FailLow,
    None
};

enum class MoveSelPhase : uint8_t {
    PrevBest,
    TT,
    Tactical,
    Killer,
    Quiet,
    BadCapture
};

// --- Unscoped Enums (used as array indices) ---

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

enum RankEnum : uint8_t {
    RANK_1,
    RANK_2,
    RANK_3,
    RANK_4,
    RANK_5,
    RANK_6,
    RANK_7,
    RANK_8
};

enum FileEnum : uint8_t {
    A_FILE,
    B_FILE,
    C_FILE,
    D_FILE,
    E_FILE,
    F_FILE,
    G_FILE,
    H_FILE,
};

enum SideEnum : Side {
    WHITE,
    BLACK,
    NO_SIDE
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

