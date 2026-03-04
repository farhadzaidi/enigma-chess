#pragma once

#include "types.hpp"

// --- Board Constants ---

constexpr int NUM_SQUARES    = 64;
constexpr int NUM_COLORS     = 2;
constexpr int NUM_PIECES     = 6;
constexpr int BOARD_SIZE     = 8;

// --- Bounds ---

constexpr int MAX_SEARCH_PLY       = 256;
constexpr int MAX_GAME_PLY         = 2048;
constexpr int FIFTY_MOVE_PLY_LIMIT = 100;
constexpr int MAX_MOVES            = 256;

// --- Scores ---

constexpr PositionScore CHECKMATE_SCORE    =  32'000;
constexpr PositionScore STALEMATE_SCORE    =  0;
constexpr PositionScore DUMMY_SCORE        = -32'700;
constexpr PositionScore SEARCH_INTERRUPTED = DUMMY_SCORE;

constexpr MoveScore MAX_MOVE_SCORE = 32'000;
constexpr MoveScore MIN_MOVE_SCORE = -MAX_MOVE_SCORE;

// --- Bitboard Masks ---

constexpr Bitboard rank_mask(int rank) {
    return Bitboard{0xFF} << (rank * 8);
}

constexpr Bitboard file_mask(int file) {
    return Bitboard{0x0101010101010101} << file;
}

constexpr auto RANK_MASKS = []() {
    std::array<Bitboard, BOARD_SIZE> m{};
    for (int i = 0; i < BOARD_SIZE; i++) {
        m[i] = rank_mask(i);
    }
    return m;
}();

constexpr auto FILE_MASKS = []() {
    std::array<Bitboard, BOARD_SIZE> m{};
    for (int i = 0; i < BOARD_SIZE; i++) {
        m[i] = file_mask(i);
    }
    return m;
}();

// --- FEN Strings ---

constexpr const char* START_POS_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr const char* KIWIPETE_FEN =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
constexpr const char* POSITION_3_FEN =
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
constexpr const char* POSITION_4_FEN =
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
constexpr const char* POSITION_5_FEN =
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8  ";
constexpr const char* POSITION_6_FEN =
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10 ";

