#include <iostream>

#include "types.hpp"
#include "constants.hpp"
#include "move.hpp"
#include "board/board.hpp"
#include "eval/eval.hpp"
#include "move_generator/move_generator.hpp"
#include "search/quiescence.hpp"
#include "search/see.hpp"
#include "utils/notation.hpp"
#include "tests/helpers.hpp"

namespace {

bool test_quiescence_stand_pat_cutoff(Board& b) {
    b.load_from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    ThreadContext ctx = make_thread_context_for_test(b);

    Board before = b;
    PositionScore score = quiescence_search(b, ctx, -50, -10);

    ASSERT_BOARD(b, before, "quiescence_stand_pat_cutoff", "Board mutated")

    ASSERT_EQ(score, 0, "quiescence_stand_pat_cutoff", "Expected stand-pat score 0")

    ASSERT_EQ(ctx.nodes, 1, "quiescence_stand_pat_cutoff", "Expected single-node cutoff")

    return true;
}

bool test_quiescence_in_check_mate_score(Board& b) {
    b.load_from_fen("rnb1kbnr/pppp1ppp/4p3/8/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    ThreadContext ctx = make_thread_context_for_test(b);

    PositionScore expected = -CHECKMATE_SCORE + ctx.search_ply(b.ply);
    PositionScore score = quiescence_search(b, ctx, -CHECKMATE_SCORE, CHECKMATE_SCORE);

    ASSERT_EQ(score, expected, "quiescence_in_check_mate", "Mate score mismatch")

    return true;
}

bool test_quiescence_see_bad_capture_pruning(Board& b) {
    b.load_from_fen("4k3/8/2p5/3p4/3Q4/8/8/4K3 w - - 0 1");
    ThreadContext ctx = make_thread_context_for_test(b);

    Move bad_capture = encode_move_from_uci(b, "d4d5");
    MoveList tacticals = generate_moves<MoveGenMode::TacticalOnly>(b);
    bool found = false;
    for (const Move& move : tacticals) {
        if (move == bad_capture) {
            found = true;
            break;
        }
    }

    ASSERT(found, "quiescence_see_pruning", "Fixture move d4d5 not in tactical list")

    int bad_see = see(b, bad_capture);
    ASSERT(bad_see < SEE_CUTOFF, "quiescence_see_pruning",
        "Fixture must be SEE-below cutoff\n" << "SEE(d4d5): " << bad_see << " cutoff: " << SEE_CUTOFF)

    PositionScore static_eval = evaluate(b);
    PositionScore score = quiescence_search(b, ctx, static_eval - 1, CHECKMATE_SCORE);

    ASSERT_EQ(score, static_eval, "quiescence_see_pruning", "Expected stand-pat to be preserved")

    ASSERT_EQ(ctx.nodes, 1, "quiescence_see_pruning", "Expected no recursive search on pruned capture")

    return true;
}

bool test_quiescence_draw_detection(Board& b) {
    b.load_from_fen("4k3/8/8/8/8/8/8/4K3 w - - 100 50");
    ThreadContext ctx = make_thread_context_for_test(b);

    PositionScore score_halfmove = quiescence_search(b, ctx, -CHECKMATE_SCORE, CHECKMATE_SCORE);
    ASSERT_EQ(score_halfmove, STALEMATE_SCORE, "quiescence_draw_detection", "50-move draw not detected")

    b.load_from_fen(START_POS_FEN);
    b.make_move(encode_move_from_uci(b, "g1f3"));
    b.make_move(encode_move_from_uci(b, "g8f6"));
    b.make_move(encode_move_from_uci(b, "f3g1"));
    b.make_move(encode_move_from_uci(b, "f6g8"));

    ASSERT(b.has_repeated(), "quiescence_draw_detection", "Repetition precondition failed")

    ctx = make_thread_context_for_test(b);
    PositionScore score_rep = quiescence_search(b, ctx, -CHECKMATE_SCORE, CHECKMATE_SCORE);
    ASSERT_EQ(score_rep, STALEMATE_SCORE, "quiescence_draw_detection", "Repetition draw not detected")

    return true;
}

} // namespace

bool test_quiescence(Board& b) {
    if (!test_quiescence_stand_pat_cutoff(b)) return false;
    if (!test_quiescence_in_check_mate_score(b)) return false;
    if (!test_quiescence_see_bad_capture_pruning(b)) return false;
    if (!test_quiescence_draw_detection(b)) return false;
    return true;
}
