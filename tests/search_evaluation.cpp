#include <iostream>

#include "core/types.hpp"
#include "board/board.hpp"
#include "eval/eval.hpp"
#include "eval/pawn_eval.hpp"
#include "eval/pawn_table.hpp"

// FEN fixtures generated and validated with python-chess 1.11.2.
static constexpr const char* MIDGAME_WHITE_TO_MOVE =
    "r1bqk2r/1pppbppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 4 6";
static constexpr const char* MIDGAME_BLACK_TO_MOVE =
    "r1bqk2r/1pppbppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 4 6";

static constexpr const char* PASSED_WHITE_PAWN_FEN =
    "4k3/8/8/4P3/8/8/8/4K3 w - - 0 1";
static constexpr const char* BLOCKED_WHITE_PAWN_FEN =
    "4k3/8/4p3/4P3/8/8/8/4K3 w - - 0 1";

static constexpr const char* BARE_KINGS_WHITE_TO_MOVE =
    "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
static constexpr const char* BARE_KINGS_BLACK_TO_MOVE =
    "4k3/8/8/8/8/8/8/4K3 b - - 0 1";

static bool test_evaluate_side_to_move_negation(Board& b) {
    pawn_table.clear();

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

static bool test_evaluate_bare_kings_drawish(Board& b) {
    pawn_table.clear();

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

static bool test_pawn_eval_passed_vs_blocked(Board& b) {
    pawn_table.clear();

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

bool test_eval(Board& b) {
    if (!test_evaluate_side_to_move_negation(b)) return false;
    if (!test_evaluate_bare_kings_drawish(b)) return false;
    if (!test_pawn_eval_passed_vs_blocked(b)) return false;
    pawn_table.clear();
    return true;
}
#include <iostream>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "move_generator/move_generator.hpp"
#include "search/quiescence.hpp"
#include "search/see.hpp"
#include "eval/eval.hpp"
#include "utils/notation.hpp"
#include "helpers.hpp"

static bool test_quiescence_stand_pat_cutoff(Board& b) {
    b.load_from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    reset_search_state_for_test(b);

    Board before = b;
    PositionScore score = quiescence_search<SearchMode::Depth>(b, -50, -10);

    if (!board_position_equal(before, b, true)) {
        std::clog << "[FAILURE] 'quiescence_stand_pat_cutoff' - Board mutated\n";
        return false;
    }

    if (score != 0) {
        std::clog << "[FAILURE] 'quiescence_stand_pat_cutoff' - Expected stand-pat score 0\n";
        std::clog << "Got: " << score << "\n";
        return false;
    }

    if (search_state.nodes != 1) {
        std::clog << "[FAILURE] 'quiescence_stand_pat_cutoff' - Expected single-node cutoff\n";
        std::clog << "Nodes: " << search_state.nodes << "\n";
        return false;
    }

    return true;
}

static bool test_quiescence_in_check_mate_score(Board& b) {
    b.load_from_fen("rnb1kbnr/pppp1ppp/4p3/8/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    reset_search_state_for_test(b);

    PositionScore expected = -CHECKMATE_SCORE + search_state.search_ply(b.ply);
    PositionScore score = quiescence_search<SearchMode::Depth>(b, -CHECKMATE_SCORE, CHECKMATE_SCORE);

    if (score != expected) {
        std::clog << "[FAILURE] 'quiescence_in_check_mate' - Mate score mismatch\n";
        std::clog << "Expected: " << expected << " Got: " << score << "\n";
        return false;
    }

    return true;
}

static bool test_quiescence_see_bad_capture_pruning(Board& b) {
    b.load_from_fen("4k3/8/2p5/3p4/3Q4/8/8/4K3 w - - 0 1");
    reset_search_state_for_test(b);

    Move bad_capture = encode_move_from_uci(b, "d4d5");
    MoveList tacticals = generate_moves<MoveGenMode::TacticalOnly>(b);
    bool found = false;
    for (const Move& move : tacticals) {
        if (move == bad_capture) {
            found = true;
            break;
        }
    }

    if (!found) {
        std::clog << "[FAILURE] 'quiescence_see_pruning' - Fixture move d4d5 not in tactical list\n";
        return false;
    }

    int bad_see = see(b, bad_capture);
    if (bad_see >= SEE_CUTOFF) {
        std::clog << "[FAILURE] 'quiescence_see_pruning' - Fixture must be SEE-below cutoff\n";
        std::clog << "SEE(d4d5): " << bad_see << " cutoff: " << SEE_CUTOFF << "\n";
        return false;
    }

    PositionScore static_eval = evaluate(b);
    PositionScore score = quiescence_search<SearchMode::Depth>(b, static_eval - 1, CHECKMATE_SCORE);

    if (score != static_eval) {
        std::clog << "[FAILURE] 'quiescence_see_pruning' - Expected stand-pat to be preserved\n";
        std::clog << "Static eval: " << static_eval << " Got: " << score << "\n";
        return false;
    }

    if (search_state.nodes != 1) {
        std::clog << "[FAILURE] 'quiescence_see_pruning' - Expected no recursive search on pruned capture\n";
        std::clog << "Nodes: " << search_state.nodes << "\n";
        return false;
    }

    return true;
}

static bool test_quiescence_draw_detection(Board& b) {
    b.load_from_fen("4k3/8/8/8/8/8/8/4K3 w - - 100 50");
    reset_search_state_for_test(b);

    PositionScore score_halfmove = quiescence_search<SearchMode::Depth>(b, -CHECKMATE_SCORE, CHECKMATE_SCORE);
    if (score_halfmove != STALEMATE_SCORE) {
        std::clog << "[FAILURE] 'quiescence_draw_detection' - 50-move draw not detected\n";
        return false;
    }

    b.load_from_fen(START_POS_FEN);
    b.make_move(encode_move_from_uci(b, "g1f3"));
    b.make_move(encode_move_from_uci(b, "g8f6"));
    b.make_move(encode_move_from_uci(b, "f3g1"));
    b.make_move(encode_move_from_uci(b, "f6g8"));

    if (!b.has_repeated()) {
        std::clog << "[FAILURE] 'quiescence_draw_detection' - Repetition precondition failed\n";
        return false;
    }

    reset_search_state_for_test(b);
    PositionScore score_rep = quiescence_search<SearchMode::Depth>(b, -CHECKMATE_SCORE, CHECKMATE_SCORE);
    if (score_rep != STALEMATE_SCORE) {
        std::clog << "[FAILURE] 'quiescence_draw_detection' - Repetition draw not detected\n";
        return false;
    }

    return true;
}

bool test_quiescence(Board& b) {
    if (!test_quiescence_stand_pat_cutoff(b)) return false;
    if (!test_quiescence_in_check_mate_score(b)) return false;
    if (!test_quiescence_see_bad_capture_pruning(b)) return false;
    if (!test_quiescence_draw_detection(b)) return false;
    return true;
}
#include <iostream>
#include <array>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "core/move.hpp"
#include "core/transposition_table.hpp"
#include "search/helpers.hpp"
#include "utils/notation.hpp"
#include "helpers.hpp"

static bool test_tt_score_normalization_round_trip() {
    const int ply = 7;
    const std::array<PositionScore, 5> samples = {
        static_cast<PositionScore>(0),
        static_cast<PositionScore>(123),
        static_cast<PositionScore>(-456),
        static_cast<PositionScore>(CHECKMATE_SCORE - 10),
        static_cast<PositionScore>(-CHECKMATE_SCORE + 10),
    };

    for (PositionScore score : samples) {
        PositionScore normalized = normalize_tt_score(score, ply);
        PositionScore denormalized = denormalize_tt_score(normalized, ply);
        if (denormalized != score) {
            std::clog << "[FAILURE] 'search_helpers_normalize' - Round-trip mismatch\n";
            std::clog << "Score: " << score << " Normalized: " << normalized
                      << " Denormalized: " << denormalized << "\n";
            return false;
        }
    }

    return true;
}

static bool test_store_tt_result_node_classification(Board& b) {
    b.load_from_fen(START_POS_FEN);
    reset_search_state_for_test(b);
    transposition_table.clear();
    transposition_table.generation = 12;

    Move m = encode_move_from_uci(b, "e2e4");

    store_tt_result(b, m, 5, 150, 100, 200);
    TTEntry* exact = transposition_table.get_entry(b.position_hash);
    if (!exact || exact->node != TTNode::Exact || exact->best_move != m) {
        std::clog << "[FAILURE] 'search_helpers_store_tt' - Exact node classification failed\n";
        return false;
    }

    store_tt_result(b, m, 5, 250, 100, 200);
    TTEntry* fail_high = transposition_table.get_entry(b.position_hash);
    if (!fail_high || fail_high->node != TTNode::FailHigh) {
        std::clog << "[FAILURE] 'search_helpers_store_tt' - FailHigh classification failed\n";
        return false;
    }

    store_tt_result(b, m, 5, 90, 100, 200);
    TTEntry* fail_low = transposition_table.get_entry(b.position_hash);
    if (!fail_low || fail_low->node != TTNode::FailLow) {
        std::clog << "[FAILURE] 'search_helpers_store_tt' - FailLow classification failed\n";
        return false;
    }

    return true;
}

static bool test_update_killer_table_rotation(Board& b) {
    b.load_from_fen(START_POS_FEN);
    reset_search_state_for_test(b);

    Move m1 = encode_move_from_uci(b, "e2e4");
    Move m2 = encode_move_from_uci(b, "d2d4");
    const int ply = 0;

    update_killer_table(m1, ply);
    if (search_state.killer_1[ply] != m1 || search_state.killer_2[ply] != NULL_MOVE) {
        std::clog << "[FAILURE] 'search_helpers_killer' - First insertion failed\n";
        return false;
    }

    update_killer_table(m2, ply);
    if (search_state.killer_1[ply] != m2 || search_state.killer_2[ply] != m1) {
        std::clog << "[FAILURE] 'search_helpers_killer' - Rotation failed\n";
        return false;
    }

    update_killer_table(m2, ply);
    if (search_state.killer_1[ply] != m2 || search_state.killer_2[ply] != m1) {
        std::clog << "[FAILURE] 'search_helpers_killer' - Duplicate insert should not rotate\n";
        return false;
    }

    return true;
}

static bool test_handle_beta_cutoff_updates(Board& b) {
    b.load_from_fen(START_POS_FEN);
    reset_search_state_for_test(b);

    Move quiet_penalized = encode_move_from_uci(b, "e2e4");
    Move quiet_cutoff = encode_move_from_uci(b, "d2d4");

    Piece penalty_piece = b.piece_map[quiet_penalized.from()];
    Piece cutoff_piece = b.piece_map[quiet_cutoff.from()];

    MoveList searched_quiets;
    searched_quiets.add(quiet_penalized);
    searched_quiets.add(quiet_cutoff);

    handle_beta_cutoff(b, quiet_cutoff, 4, searched_quiets);

    int ply = search_state.search_ply(b.ply);
    if (search_state.killer_1[ply] != quiet_cutoff) {
        std::clog << "[FAILURE] 'search_helpers_beta_cutoff' - Quiet cutoff should update killer_1\n";
        return false;
    }

    MoveScore cutoff_hist = search_state.side_piece_to_history[b.to_move][cutoff_piece][quiet_cutoff.to()]
        + search_state.from_to_history[quiet_cutoff.from()][quiet_cutoff.to()];
    MoveScore penalized_hist = search_state.side_piece_to_history[b.to_move][penalty_piece][quiet_penalized.to()]
        + search_state.from_to_history[quiet_penalized.from()][quiet_penalized.to()];

    if (cutoff_hist <= 0) {
        std::clog << "[FAILURE] 'search_helpers_beta_cutoff' - Cutoff move should get positive history bonus\n";
        return false;
    }

    if (penalized_hist >= 0) {
        std::clog << "[FAILURE] 'search_helpers_beta_cutoff' - Earlier quiet move should get negative history malus\n";
        return false;
    }

    reset_search_state_for_test(b);
    MoveList empty;
    Move tactical(A1, A2, MoveType::Capture, MoveFlag::Normal);
    handle_beta_cutoff(b, tactical, 4, empty);

    if (search_state.killer_1[ply] != NULL_MOVE || search_state.killer_2[ply] != NULL_MOVE) {
        std::clog << "[FAILURE] 'search_helpers_beta_cutoff' - Tactical cutoff should not update killers\n";
        return false;
    }

    return true;
}

bool test_search_helpers() {
    Board b;
    if (!test_tt_score_normalization_round_trip()) return false;
    if (!test_store_tt_result_node_classification(b)) return false;
    if (!test_update_killer_table_rotation(b)) return false;
    if (!test_handle_beta_cutoff_updates(b)) return false;
    transposition_table.clear();
    return true;
}
