#include <iostream>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "eval/eval.hpp"
#include "eval/pawn_eval.hpp"
#include "eval/pawn_table.hpp"
#include "tests/helpers.hpp"

namespace {

// --- Eval tests ---

const std::string MIDGAME_WHITE_TO_MOVE =
    "r1bqk2r/1pppbppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 4 6";
const std::string MIDGAME_BLACK_TO_MOVE =
    "r1bqk2r/1pppbppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 4 6";

const std::string PASSED_WHITE_PAWN_FEN =
    "4k3/8/8/4P3/8/8/8/4K3 w - - 0 1";
const std::string BLOCKED_WHITE_PAWN_FEN =
    "4k3/8/4p3/4P3/8/8/8/4K3 w - - 0 1";

const std::string BARE_KINGS_WHITE_TO_MOVE =
    "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
const std::string BARE_KINGS_BLACK_TO_MOVE =
    "4k3/8/8/8/8/8/8/4K3 b - - 0 1";

bool test_evaluate_side_to_move_negation(Board& b) {
    g_pawn_table.clear();

    b.load_from_fen(MIDGAME_WHITE_TO_MOVE);
    PositionScore white_score = evaluate(b);

    b.load_from_fen(MIDGAME_BLACK_TO_MOVE);
    PositionScore black_score = evaluate(b);

    if (white_score != -black_score) {
        std::clog << "[FAILURE] 'eval_side_to_move_negation' - Score should negate when only side-to-move flips\n";
        std::clog << "White score: " << white_score << " Black score: " << black_score << "\n";
        return false;
    }

    return true;
}

bool test_evaluate_bare_kings_drawish(Board& b) {
    g_pawn_table.clear();

    b.load_from_fen(BARE_KINGS_WHITE_TO_MOVE);
    PositionScore white_score = evaluate(b);

    b.load_from_fen(BARE_KINGS_BLACK_TO_MOVE);
    PositionScore black_score = evaluate(b);

    if (white_score != 0 || black_score != 0) {
        std::clog << "[FAILURE] 'eval_bare_kings' - Bare kings should evaluate to zero for both sides\n";
        std::clog << "White score: " << white_score << " Black score: " << black_score << "\n";
        return false;
    }

    return true;
}

bool test_pawn_eval_passed_vs_blocked(Board& b) {
    g_pawn_table.clear();

    b.load_from_fen(PASSED_WHITE_PAWN_FEN);
    PawnTableEntry passed = get_pawn_score(b);

    b.load_from_fen(BLOCKED_WHITE_PAWN_FEN);
    PawnTableEntry blocked = get_pawn_score(b);

    if (passed.early_pawn_score[WHITE] <= blocked.early_pawn_score[WHITE]) {
        std::clog << "[FAILURE] 'pawn_eval_passed_vs_blocked' - Passed pawn should score higher than blocked pawn (early)\n";
        std::clog << "Passed early: " << passed.early_pawn_score[WHITE]
                  << " Blocked early: " << blocked.early_pawn_score[WHITE] << "\n";
        return false;
    }

    if (passed.late_pawn_score[WHITE] <= blocked.late_pawn_score[WHITE]) {
        std::clog << "[FAILURE] 'pawn_eval_passed_vs_blocked' - Passed pawn should score higher than blocked pawn (late)\n";
        std::clog << "Passed late: " << passed.late_pawn_score[WHITE]
                  << " Blocked late: " << blocked.late_pawn_score[WHITE] << "\n";
        return false;
    }

    return true;
}

} // namespace

bool test_eval(Board& b) {
    if (!test_evaluate_side_to_move_negation(b)) return false;
    if (!test_evaluate_bare_kings_drawish(b)) return false;
    if (!test_pawn_eval_passed_vs_blocked(b)) return false;
    g_pawn_table.clear();
    return true;
}
