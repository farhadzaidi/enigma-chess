#include <iostream>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "utils/notation.hpp"
#include "tests/helpers.hpp"

namespace {

bool test_repetition(Board& b) {
    b.load_from_fen();

    // Play Nf3 Nf6 Ng1 Ng8 - back to start
    Move m1 = encode_move_from_uci(b, "g1f3");
    b.make_move(m1);
    Move m2 = encode_move_from_uci(b, "g8f6");
    b.make_move(m2);
    Move m3 = encode_move_from_uci(b, "f3g1");
    b.make_move(m3);
    Move m4 = encode_move_from_uci(b, "f6g8");
    b.make_move(m4);

    ASSERT(b.has_repeated(), "repetition", "Expected has_repeated() to return true");

    // Unmake last move, repetition should no longer be detected
    b.unmake_move(m4);

    ASSERT(!b.has_repeated(), "repetition", "Expected has_repeated() to return false after unmake");

    return true;
}

bool test_fifty_move_rule(Board& b) {
    b.load_from_fen("4k3/8/8/8/8/8/8/4K2R w - - 99 50");

    // After a quiet move (Rh2), halfmoves should hit 100 and trigger draw
    b.make_move(encode_move_from_uci(b, "h1h2"));

    ASSERT(b.halfmoves >= FIFTY_MOVE_PLY_LIMIT, "fifty_move_rule", "Expected halfmoves >= 100 after quiet move, Halfmoves: " << b.halfmoves);

    return true;
}

// A capture resets the halfmove clock and breaks repetition history.
// Positions before the capture should not count as repetitions.
bool test_no_repetition_across_capture(Board& b) {
    b.load_from_fen();

    // Nf3 Nf6 Ng1 Ng8 (creates a repetition of the start position)
    b.make_move(encode_move_from_uci(b, "g1f3"));
    b.make_move(encode_move_from_uci(b, "g8f6"));
    b.make_move(encode_move_from_uci(b, "f3g1"));
    b.make_move(encode_move_from_uci(b, "f6g8"));

    ASSERT(b.has_repeated(), "no_repetition_across_capture", "Precondition: repetition should exist before capture");

    // e4 d5 exd5 (capture resets halfmove clock)
    b.make_move(encode_move_from_uci(b, "e2e4"));
    b.make_move(encode_move_from_uci(b, "d7d5"));
    b.make_move(encode_move_from_uci(b, "e4d5"));

    ASSERT(!b.has_repeated(), "no_repetition_across_capture", "Repetition should not persist after a capture");

    return true;
}

// Same piece layout but different castling rights is a different position.
// King moves that lose castling rights should not produce a repetition
// even if the king returns to its original square.
bool test_no_repetition_with_changed_castling(Board& b) {
    b.load_from_fen("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");

    // Kd1 Kd8 Ke1 Ke8 - kings return to original squares but castling rights are gone
    b.make_move(encode_move_from_uci(b, "e1d1"));
    b.make_move(encode_move_from_uci(b, "e8d8"));
    b.make_move(encode_move_from_uci(b, "d1e1"));
    b.make_move(encode_move_from_uci(b, "d8e8"));

    ASSERT(!b.has_repeated(), "no_repetition_changed_castling",
        "Same piece layout with different castling rights should not be a repetition");

    return true;
}

} // namespace

bool test_draws(Board& b) {
    if (!test_repetition(b)) return false;
    if (!test_fifty_move_rule(b)) return false;
    if (!test_no_repetition_across_capture(b)) return false;
    if (!test_no_repetition_with_changed_castling(b)) return false;
    return true;
}
