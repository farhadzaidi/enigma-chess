#include <iostream>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "core/globals.hpp"
#include "core/uci.hpp"
#include "core/transposition_table.hpp"
#include "eval/pawn_table.hpp"
#include "board/board.hpp"
#include "utils/notation.hpp"
#include "tests/helpers.hpp"

// Expected FEN snapshots generated and validated with python-chess 1.11.2.
static constexpr const char* STARTPOS_SEQ_EXPECTED_FEN =
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";

static constexpr const char* FEN_SEQ_EXPECTED_FEN =
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2";

static bool board_position_equal_ignoring_ply(const Board& a, const Board& b) {
    Board a_copy = a;
    Board b_copy = b;
    b_copy.ply = a_copy.ply;
    return board_position_equal(a_copy, b_copy, true);
}

static bool test_calc_time_limit_movestogo_branch(Board& b) {
    b.load_from_fen(START_POS_FEN);

    SearchTime t = calc_time_limit(b, 60000, 1000, 20);
    if (t.soft_limit != 2100 || t.hard_limit != 8750) {
        std::clog << "[FAILURE] 'uci_helpers_calc_time_movestogo' - Unexpected limits\n";
        std::clog << "Expected soft/hard: 2100/8750 Got: " << t.soft_limit << "/" << t.hard_limit << "\n";
        return false;
    }

    return true;
}

static bool test_calc_time_limit_phase_branch(Board& b) {
    b.load_from_fen(START_POS_FEN);

    if (b.game_phase != MAX_GAME_PHASE) {
        std::clog << "[FAILURE] 'uci_helpers_calc_time_phase' - Start position should be full phase\n";
        std::clog << "game_phase: " << b.game_phase << " max: " << MAX_GAME_PHASE << "\n";
        return false;
    }

    SearchTime t = calc_time_limit(b, 40000, 0, -1);
    if (t.soft_limit != 600 || t.hard_limit != 2500) {
        std::clog << "[FAILURE] 'uci_helpers_calc_time_phase' - Unexpected phase-based limits\n";
        std::clog << "Expected soft/hard: 600/2500 Got: " << t.soft_limit << "/" << t.hard_limit << "\n";
        return false;
    }

    return true;
}

static bool test_cmd_setoption_toggles() {
    bool old_book = g_use_own_book;
    bool old_ponder = g_enable_ponder;

    cmd_setoption("setoption name OwnBook value false");
    if (g_use_own_book) {
        std::clog << "[FAILURE] 'uci_helpers_setoption' - OwnBook false not applied\n";
        g_use_own_book = old_book;
        g_enable_ponder = old_ponder;
        return false;
    }

    cmd_setoption("setoption name OwnBook value true");
    if (!g_use_own_book) {
        std::clog << "[FAILURE] 'uci_helpers_setoption' - OwnBook true not applied\n";
        g_use_own_book = old_book;
        g_enable_ponder = old_ponder;
        return false;
    }

    cmd_setoption("setoption name Ponder value true");
    if (!g_enable_ponder) {
        std::clog << "[FAILURE] 'uci_helpers_setoption' - Ponder true not applied\n";
        g_use_own_book = old_book;
        g_enable_ponder = old_ponder;
        return false;
    }

    cmd_setoption("setoption name Ponder value false");
    if (g_enable_ponder) {
        std::clog << "[FAILURE] 'uci_helpers_setoption' - Ponder false not applied\n";
        g_use_own_book = old_book;
        g_enable_ponder = old_ponder;
        return false;
    }

    g_use_own_book = old_book;
    g_enable_ponder = old_ponder;
    return true;
}

static bool test_cmd_position_parsing(Board& b) {
    // startpos + moves
    cmd_position("position startpos moves e2e4 e7e5 g1f3 b8c6", b);

    Board expected_startpos_seq;
    expected_startpos_seq.load_from_fen(STARTPOS_SEQ_EXPECTED_FEN);

    if (!board_position_equal_ignoring_ply(b, expected_startpos_seq)) {
        std::clog << "[FAILURE] 'uci_helpers_cmd_position' - startpos+moves mismatch\n";
        return false;
    }

    // fen + moves
    cmd_position(
        "position fen rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1 moves c7c5 g1f3",
        b
    );

    Board expected_fen_seq;
    expected_fen_seq.load_from_fen(FEN_SEQ_EXPECTED_FEN);

    if (!board_position_equal_ignoring_ply(b, expected_fen_seq)) {
        std::clog << "[FAILURE] 'uci_helpers_cmd_position' - fen+moves mismatch\n";
        return false;
    }

    return true;
}

static bool test_cmd_ucinewgame_clears_state(Board& b) {
    b.load_from_fen(START_POS_FEN);
    ZobristHash saved_position_hash = b.position_hash;
    ZobristHash saved_pawn_hash = b.pawn_hash;

    g_transposition_table.clear();
    g_transposition_table.generation = 3;
    g_transposition_table.add_entry(TTEntry(saved_position_hash, encode_move_from_uci(b, "e2e4"), 6, 120, TTNode::Exact));

    g_pawn_table.clear();
    PawnTableEntry pt;
    pt.hash = saved_pawn_hash;
    pt.early_pawn_score = {12, -12};
    pt.late_pawn_score = {20, -20};
    g_pawn_table.add_entry(pt);

    cmd_ucinewgame(b);

    if (b.to_move != NO_SIDE || b.occupied != EMPTY_BITBOARD || b.ply != 0) {
        std::clog << "[FAILURE] 'uci_helpers_ucinewgame' - Board reset state mismatch\n";
        return false;
    }

    if (g_transposition_table.generation != 0 || g_transposition_table.get_entry(saved_position_hash) != nullptr) {
        std::clog << "[FAILURE] 'uci_helpers_ucinewgame' - TT should be cleared\n";
        return false;
    }

    PawnTableEntry& probed = g_pawn_table.get_entry(saved_pawn_hash);
    if (g_pawn_table.is_valid_entry(saved_pawn_hash, probed)) {
        std::clog << "[FAILURE] 'uci_helpers_ucinewgame' - Pawn table should be cleared\n";
        return false;
    }

    return true;
}

bool test_uci_helpers(Board& b) {
    if (!test_calc_time_limit_movestogo_branch(b)) return false;
    if (!test_calc_time_limit_phase_branch(b)) return false;
    if (!test_cmd_setoption_toggles()) return false;
    if (!test_cmd_position_parsing(b)) return false;
    if (!test_cmd_ucinewgame_clears_state(b)) return false;
    return true;
}
