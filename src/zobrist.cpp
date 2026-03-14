#include "zobrist.hpp"

#include <limits>
#include <random>

namespace {

constexpr uint64_t ZOBRIST_PRNG_SEED = 0xC0DEC0FFEE1234ULL;

/** Generate the next deterministic random 64-bit value for Zobrist table init */
uint64_t next_zobrist_u64() {
    static std::mt19937_64 prng(ZOBRIST_PRNG_SEED);
    static std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());
    return dist(prng);
}

} // namespace

const ZobristPieces ZOBRIST_PIECES = []() {
    ZobristPieces pieces{};
    for (int side = 0; side < NUM_SIDES; side++) {
        for (int piece = 0; piece < NUM_PIECES; piece++) {
            for (int square = 0; square < NUM_SQUARES; square++) {
                pieces[side][piece][square] = next_zobrist_u64();
            }
        }
    }
    return pieces;
}();

const ZobristCastlingRights ZOBRIST_CASTLING_RIGHTS = []() {
    ZobristCastlingRights rights{};
    for (int castling_rights = 0; castling_rights < CASTLING_RIGHTS_COMBINATIONS; castling_rights++) {
        rights[castling_rights] = next_zobrist_u64();
    }
    return rights;
}();

const ZobristEnPassantTargets ZOBRIST_EN_PASSANT_TARGETS = []() {
    ZobristEnPassantTargets targets{};
    for (int file = 0; file < EN_PASSANT_TARGET_FILES; file++) {
        targets[file] = next_zobrist_u64();
    }
    return targets;
}();

const ZobristHash ZOBRIST_SIDE_TO_MOVE = next_zobrist_u64();
