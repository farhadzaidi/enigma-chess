#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "types.hpp"
#include "board.hpp"
#include "move.hpp"
#include "move_generator.hpp"
#include "move_selector.hpp"
#include "search_state.hpp"
#include "utils.hpp"

static bool board_position_equal(const Board& a, const Board& b) {
    if (a.zobrist_hash != b.zobrist_hash) return false;
    if (a.occupied != b.occupied) return false;
    if (a.to_move != b.to_move) return false;
    if (a.castling_rights != b.castling_rights) return false;
    if (a.en_passant_target != b.en_passant_target) return false;
    if (a.halfmoves != b.halfmoves) return false;
    if (a.fullmoves != b.fullmoves) return false;
    if (a.ply != b.ply) return false;
    if (a.game_phase != b.game_phase) return false;

    for (int c = 0; c < NUM_COLORS; c++) {
        if (a.colors[c] != b.colors[c]) return false;
        if (a.king_squares[c] != b.king_squares[c]) return false;
        if (a.early_score[c] != b.early_score[c]) return false;
        if (a.late_score[c] != b.late_score[c]) return false;

        for (int p = 0; p < NUM_PIECES; p++) {
            if (a.pieces[c][p] != b.pieces[c][p]) return false;
        }
    }

    for (int sq = 0; sq < NUM_SQUARES; sq++) {
        if (a.piece_map[sq] != b.piece_map[sq]) return false;
    }

    return true;
}

static SearchState make_search_state(const Board& b) {
    SearchState ss{};
    ss.ply_offset = b.ply;
    ss.killer_1.fill(NULL_MOVE);
    ss.killer_2.fill(NULL_MOVE);
    ss.color_piece_to = {};
    ss.from_to = {};
    return ss;
}

static bool collect_selector_moves(
    Board& b,
    SearchState& ss,
    MoveList& out,
    const std::string& test_name,
    const std::string& context,
    Move tt_move = NULL_MOVE,
    Move prev_best_move = NULL_MOVE
) {
    Board before = b;
    MoveSelector selector(b, tt_move, prev_best_move);

    int guard = 0;
    while (true) {
        Move move = selector.next_move(b, ss);
        if (move == NULL_MOVE) break;

        if (out.size >= MAX_MOVES) {
            std::clog << "[FAILURE] '" << test_name << "' - Selector exceeded MAX_MOVES\n";
            std::clog << "Case: " << context << "\n";
            return false;
        }

        out.add(move);
        guard++;
        if (guard > MAX_MOVES + 8) {
            std::clog << "[FAILURE] '" << test_name << "' - Selector did not terminate\n";
            std::clog << "Case: " << context << "\n";
            return false;
        }
    }

    if (!board_position_equal(before, b)) {
        std::clog << "[FAILURE] '" << test_name << "' - Board mutated while selecting moves\n";
        std::clog << "Case: " << context << "\n";
        return false;
    }

    return true;
}

static bool contains_move(const MoveList& moves, Move target) {
    for (const Move move : moves) {
        if (move == target) return true;
    }
    return false;
}

static int find_move_index(const MoveList& moves, Move target) {
    for (int i = 0; i < moves.size; i++) {
        if (moves[i] == target) return i;
    }
    return -1;
}

static int count_occurrences(const MoveList& moves, Move target) {
    int count = 0;
    for (const Move move : moves) {
        if (move == target) count++;
    }
    return count;
}

static bool assert_no_duplicates(
    const MoveList& moves,
    const std::string& test_name,
    const std::string& context
) {
    std::unordered_set<uint16_t> seen;
    for (const Move move : moves) {
        if (!seen.insert(move.move).second) {
            std::clog << "[FAILURE] '" << test_name << "' - Duplicate move returned by selector\n";
            std::clog << "Case: " << context << "\n";
            std::clog << "Move: " << decode_move_to_uci(move) << "\n";
            return false;
        }
    }
    return true;
}

static bool assert_same_move_set(
    const MoveList& expected,
    const MoveList& actual,
    const std::string& test_name,
    const std::string& context
) {
    std::unordered_set<uint16_t> expected_set;
    std::unordered_set<uint16_t> actual_set;

    for (const Move move : expected) expected_set.insert(move.move);
    for (const Move move : actual) actual_set.insert(move.move);

    if (expected_set.size() != actual_set.size()) {
        std::clog << "[FAILURE] '" << test_name << "' - Different move-set size\n";
        std::clog << "Case: " << context << "\n";
        std::clog << "Expected size: " << expected_set.size() << " Got: " << actual_set.size() << "\n";
        return false;
    }

    for (const Move move : expected) {
        if (!actual_set.contains(move.move)) {
            std::clog << "[FAILURE] '" << test_name << "' - Missing move from selector output\n";
            std::clog << "Case: " << context << "\n";
            std::clog << "Missing: " << decode_move_to_uci(move) << "\n";
            return false;
        }
    }

    for (const Move move : actual) {
        if (!expected_set.contains(move.move)) {
            std::clog << "[FAILURE] '" << test_name << "' - Extra move in selector output\n";
            std::clog << "Case: " << context << "\n";
            std::clog << "Extra: " << decode_move_to_uci(move) << "\n";
            return false;
        }
    }

    return true;
}

static bool assert_all_moves_legal(
    Board& b,
    const MoveList& moves,
    const std::string& test_name,
    const std::string& context
) {
    for (const Move move : moves) {
        Board before = b;
        if (!b.is_legal_move(move)) {
            std::clog << "[FAILURE] '" << test_name << "' - Selector returned illegal move\n";
            std::clog << "Case: " << context << "\n";
            std::clog << "Move: " << decode_move_to_uci(move) << "\n";
            return false;
        }
        if (!board_position_equal(before, b)) {
            std::clog << "[FAILURE] '" << test_name << "' - Board mutated while validating legality\n";
            std::clog << "Case: " << context << "\n";
            std::clog << "Move: " << decode_move_to_uci(move) << "\n";
            return false;
        }
    }
    return true;
}

static bool test_move_selector_completeness_and_uniqueness(Board& b) {
    std::vector<std::string> positions = {
        START_POS_FEN,
        KIWIPETE_FEN,
        POSITION_3_FEN,
        POSITION_4_FEN,
        POSITION_5_FEN,
        POSITION_6_FEN,
        "4k3/8/8/4Q3/8/8/8/4K3 b - - 0 1",
        "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
    };

    for (const std::string& fen : positions) {
        b.reset();
        b.load_from_fen(fen);
        SearchState ss = make_search_state(b);

        MoveList expected = generate_moves<ALL>(b);
        MoveList selected;
        std::string context = "set-equivalence on FEN: " + fen;

        if (!collect_selector_moves(b, ss, selected, "move_selector_completeness", context)) return false;
        if (!assert_no_duplicates(selected, "move_selector_completeness", context)) return false;
        if (!assert_same_move_set(expected, selected, "move_selector_completeness", context)) return false;
        if (!assert_all_moves_legal(b, selected, "move_selector_completeness", context)) return false;
    }

    return true;
}

static bool test_move_selector_ordering_priority(Board& b) {
    b.reset();
    b.load_from_fen("4k3/8/8/3p4/4P3/8/8/4K3 b - - 0 1");
    SearchState ss = make_search_state(b);
    int ply = ss.search_ply(b.ply);

    Move prev_best = encode_move_from_uci(b, "d5d4");
    Move tt_move = encode_move_from_uci(b, "d5e4");
    Move killer = encode_move_from_uci(b, "e8d7");

    if (!b.is_legal_move(prev_best) || !b.is_legal_move(tt_move) || !b.is_legal_move(killer)) {
        std::clog << "[FAILURE] 'move_selector_ordering' - Setup moves must be legal\n";
        return false;
    }

    ss.killer_1[ply] = killer;

    MoveList selected;
    if (!collect_selector_moves(
        b,
        ss,
        selected,
        "move_selector_ordering",
        "previous best -> tt -> tactical -> killer -> quiet",
        tt_move,
        prev_best
    )) {
        return false;
    }

    if (selected.size < 3) {
        std::clog << "[FAILURE] 'move_selector_ordering' - Expected at least 3 moves in setup position\n";
        return false;
    }

    if (selected[0] != prev_best) {
        std::clog << "[FAILURE] 'move_selector_ordering' - Previous best was not first\n";
        std::clog << "Expected: " << decode_move_to_uci(prev_best)
                  << " Got: " << decode_move_to_uci(selected[0]) << "\n";
        return false;
    }

    if (selected[1] != tt_move) {
        std::clog << "[FAILURE] 'move_selector_ordering' - TT move was not second\n";
        std::clog << "Expected: " << decode_move_to_uci(tt_move)
                  << " Got: " << decode_move_to_uci(selected[1]) << "\n";
        return false;
    }

    int killer_index = find_move_index(selected, killer);
    if (killer_index == -1) {
        std::clog << "[FAILURE] 'move_selector_ordering' - Legal killer move not returned\n";
        return false;
    }

    int first_non_hint_quiet = -1;
    for (int i = 0; i < selected.size; i++) {
        Move move = selected[i];
        if (move.type() == QUIET && move != prev_best && move != killer) {
            first_non_hint_quiet = i;
            break;
        }
    }

    if (first_non_hint_quiet != -1 && killer_index > first_non_hint_quiet) {
        std::clog << "[FAILURE] 'move_selector_ordering' - Killer should precede regular quiet moves\n";
        std::clog << "Killer index: " << killer_index
                  << " First regular quiet index: " << first_non_hint_quiet << "\n";
        return false;
    }

    if (count_occurrences(selected, prev_best) != 1 || count_occurrences(selected, tt_move) != 1 || count_occurrences(selected, killer) != 1) {
        std::clog << "[FAILURE] 'move_selector_ordering' - Hint moves should appear exactly once\n";
        return false;
    }

    MoveList expected = generate_moves<ALL>(b);
    if (!assert_no_duplicates(selected, "move_selector_ordering", "ordered sequence dedup")) return false;
    if (!assert_same_move_set(expected, selected, "move_selector_ordering", "ordered sequence set-equality")) return false;

    return true;
}

static bool test_move_selector_hint_deduplication(Board& b) {
    b.reset();
    b.load_from_fen(START_POS_FEN);
    SearchState ss = make_search_state(b);
    int ply = ss.search_ply(b.ply);

    Move repeated_hint = encode_move_from_uci(b, "e2e4");
    if (!b.is_legal_move(repeated_hint)) {
        std::clog << "[FAILURE] 'move_selector_hint_dedup' - Setup move e2e4 should be legal\n";
        return false;
    }

    ss.killer_1[ply] = repeated_hint;
    ss.killer_2[ply] = repeated_hint;

    MoveList selected;
    if (!collect_selector_moves(
        b,
        ss,
        selected,
        "move_selector_hint_dedup",
        "same move used as prev/tt/killer",
        repeated_hint,
        repeated_hint
    )) {
        return false;
    }

    if (count_occurrences(selected, repeated_hint) != 1) {
        std::clog << "[FAILURE] 'move_selector_hint_dedup' - Repeated hint move returned multiple times\n";
        std::clog << "Move: " << decode_move_to_uci(repeated_hint)
                  << " Count: " << count_occurrences(selected, repeated_hint) << "\n";
        return false;
    }

    MoveList expected = generate_moves<ALL>(b);
    if (!assert_no_duplicates(selected, "move_selector_hint_dedup", "global dedup")) return false;
    if (!assert_same_move_set(expected, selected, "move_selector_hint_dedup", "set-equivalence")) return false;

    return true;
}

static bool test_move_selector_stale_hint_rejection(Board& b) {
    b.reset();
    b.load_from_fen("4k3/8/8/3p4/4P3/8/8/4K3 b - - 0 1");
    SearchState ss = make_search_state(b);
    int ply = ss.search_ply(b.ply);

    Move stale_tt(E2, E4, QUIET, NORMAL);
    Move stale_killer_1(A1, A8, QUIET, NORMAL);
    Move stale_killer_2(H2, H4, QUIET, NORMAL);

    ss.killer_1[ply] = stale_killer_1;
    ss.killer_2[ply] = stale_killer_2;

    MoveList selected;
    if (!collect_selector_moves(
        b,
        ss,
        selected,
        "move_selector_stale_hint",
        "illegal tt/killer hints should be ignored",
        stale_tt
    )) {
        return false;
    }

    if (contains_move(selected, stale_tt) || contains_move(selected, stale_killer_1) || contains_move(selected, stale_killer_2)) {
        std::clog << "[FAILURE] 'move_selector_stale_hint' - Stale hint leaked into output\n";
        return false;
    }

    MoveList expected = generate_moves<ALL>(b);
    if (!assert_no_duplicates(selected, "move_selector_stale_hint", "stale-hint rejection")) return false;
    if (!assert_same_move_set(expected, selected, "move_selector_stale_hint", "stale-hint rejection")) return false;

    return true;
}

static bool test_move_selector_quiet_history_order(Board& b) {
    b.reset();
    b.load_from_fen(START_POS_FEN);
    SearchState ss = make_search_state(b);

    Move higher = encode_move_from_uci(b, "e2e4");
    Move lower = encode_move_from_uci(b, "d2d4");

    if (!b.is_legal_move(higher) || !b.is_legal_move(lower)) {
        std::clog << "[FAILURE] 'move_selector_quiet_history' - Setup quiet moves must be legal\n";
        return false;
    }

    Piece higher_piece = b.piece_map[higher.from()];
    Piece lower_piece = b.piece_map[lower.from()];
    ss.color_piece_to[b.to_move][higher_piece][higher.to()] = 8000;
    ss.from_to[higher.from()][higher.to()] = 8000;
    ss.color_piece_to[b.to_move][lower_piece][lower.to()] = 1000;
    ss.from_to[lower.from()][lower.to()] = 1000;

    MoveList selected;
    if (!collect_selector_moves(
        b,
        ss,
        selected,
        "move_selector_quiet_history",
        "higher history quiet should be returned before lower history quiet"
    )) {
        return false;
    }

    int higher_index = find_move_index(selected, higher);
    int lower_index = find_move_index(selected, lower);
    if (higher_index == -1 || lower_index == -1) {
        std::clog << "[FAILURE] 'move_selector_quiet_history' - Expected quiet moves not found in selector output\n";
        return false;
    }

    if (higher_index >= lower_index) {
        std::clog << "[FAILURE] 'move_selector_quiet_history' - Quiet history ordering not respected\n";
        std::clog << "Higher move: " << decode_move_to_uci(higher) << " index=" << higher_index << "\n";
        std::clog << "Lower move: " << decode_move_to_uci(lower) << " index=" << lower_index << "\n";
        return false;
    }

    return true;
}

static bool test_move_selector_in_check(Board& b) {
    b.reset();
    b.load_from_fen("4k3/8/8/4Q3/8/8/8/4K3 b - - 0 1");
    SearchState ss = make_search_state(b);
    int ply = ss.search_ply(b.ply);

    MoveList expected = generate_moves<ALL>(b);
    Move stale_tt(E2, E4, QUIET, NORMAL);

    Move legal_killer = NULL_MOVE;
    for (const Move move : expected) {
        if (move.type() == QUIET) {
            legal_killer = move;
            break;
        }
    }

    if (legal_killer != NULL_MOVE) {
        ss.killer_1[ply] = legal_killer;
    }

    MoveList selected;
    if (!collect_selector_moves(
        b,
        ss,
        selected,
        "move_selector_in_check",
        "in-check evasions with stale tt hint and optional killer",
        stale_tt
    )) {
        return false;
    }

    if (!assert_no_duplicates(selected, "move_selector_in_check", "in-check")) return false;
    if (!assert_same_move_set(expected, selected, "move_selector_in_check", "in-check")) return false;
    if (!assert_all_moves_legal(b, selected, "move_selector_in_check", "in-check")) return false;

    return true;
}

bool test_move_selector(Board& b) {
    if (!test_move_selector_completeness_and_uniqueness(b)) return false;
    if (!test_move_selector_ordering_priority(b)) return false;
    if (!test_move_selector_hint_deduplication(b)) return false;
    if (!test_move_selector_stale_hint_rejection(b)) return false;
    if (!test_move_selector_quiet_history_order(b)) return false;
    if (!test_move_selector_in_check(b)) return false;
    return true;
}
