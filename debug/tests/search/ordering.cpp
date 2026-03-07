#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/types.hpp"
#include "board/board.hpp"
#include "core/move.hpp"
#include "move_generator/move_generator.hpp"
#include "search/move_selector.hpp"
#include "search/search_state.hpp"
#include "search/see.hpp"
#include "utils/notation.hpp"
#include "tests/helpers.hpp"

namespace {

// --- Move selector helpers ---

SearchState make_search_state(const Board& b) {
    SearchState ss{};
    ss.ply_offset = b.ply;
    ss.killer_1.fill(NULL_MOVE);
    ss.killer_2.fill(NULL_MOVE);
    ss.side_piece_to_history = {};
    ss.from_to_history = {};
    return ss;
}

bool collect_selector_moves(
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

bool contains_move(const MoveList& moves, Move target) {
    for (const Move move : moves) {
        if (move == target) return true;
    }
    return false;
}

int find_move_index(const MoveList& moves, Move target) {
    for (int i = 0; i < moves.size; i++) {
        if (moves[i] == target) return i;
    }
    return -1;
}

int count_occurrences(const MoveList& moves, Move target) {
    int count = 0;
    for (const Move move : moves) {
        if (move == target) count++;
    }
    return count;
}

bool assert_no_duplicates(
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

bool assert_same_move_set(
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

bool assert_all_moves_legal(
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

// --- Move selector tests ---

bool test_move_selector_completeness_and_uniqueness(Board& b) {
    std::vector<std::string_view> positions = {
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

    for (const std::string_view& fen : positions) {
        b.reset();
        b.load_from_fen(fen);
        SearchState ss = make_search_state(b);

        MoveList expected = generate_moves<MoveGenMode::All>(b);
        MoveList selected;
        std::string context = std::string{"set-equivalence on FEN: "} += fen;

        if (!collect_selector_moves(b, ss, selected, "move_selector_completeness", context)) return false;
        if (!assert_no_duplicates(selected, "move_selector_completeness", context)) return false;
        if (!assert_same_move_set(expected, selected, "move_selector_completeness", context)) return false;
        if (!assert_all_moves_legal(b, selected, "move_selector_completeness", context)) return false;
    }

    return true;
}

bool test_move_selector_ordering_priority(Board& b) {
    b.reset();
    b.load_from_fen("4k3/8/8/3p4/4P3/8/8/4K3 b - - 0 1");
    SearchState ss = make_search_state(b);
    int ply = ss.search_ply(b.ply);

    Move prev_best = encode_move_from_uci(b, "d5d4");
    Move tt_move = encode_move_from_uci(b, "d5e4");
    Move killer = encode_move_from_uci(b, "e8d7");

    ASSERT(b.is_legal_move(prev_best) && b.is_legal_move(tt_move) && b.is_legal_move(killer),
           "move_selector_ordering", "Setup moves must be legal");

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

    ASSERT(selected.size >= 3,
           "move_selector_ordering", "Expected at least 3 moves in setup position");

    ASSERT_EQ(decode_move_to_uci(selected[0]), decode_move_to_uci(prev_best),
              "move_selector_ordering", "Previous best was not first");

    ASSERT_EQ(decode_move_to_uci(selected[1]), decode_move_to_uci(tt_move),
              "move_selector_ordering", "TT move was not second");

    int killer_index = find_move_index(selected, killer);
    ASSERT(killer_index != -1,
           "move_selector_ordering", "Legal killer move not returned");

    int first_non_hint_quiet = -1;
    for (int i = 0; i < selected.size; i++) {
        Move move = selected[i];
        if (move.type() == MoveType::Quiet && move != prev_best && move != killer) {
            first_non_hint_quiet = i;
            break;
        }
    }

    ASSERT(first_non_hint_quiet == -1 || killer_index <= first_non_hint_quiet,
           "move_selector_ordering",
           "Killer should precede regular quiet moves\n"
           << "Killer index: " << killer_index
           << " First regular quiet index: " << first_non_hint_quiet);

    ASSERT(count_occurrences(selected, prev_best) == 1
           && count_occurrences(selected, tt_move) == 1
           && count_occurrences(selected, killer) == 1,
           "move_selector_ordering", "Hint moves should appear exactly once");

    MoveList expected = generate_moves<MoveGenMode::All>(b);
    if (!assert_no_duplicates(selected, "move_selector_ordering", "ordered sequence dedup")) return false;
    if (!assert_same_move_set(expected, selected, "move_selector_ordering", "ordered sequence set-equality")) return false;

    return true;
}

bool test_move_selector_hint_deduplication(Board& b) {
    b.reset();
    b.load_from_fen(START_POS_FEN);
    SearchState ss = make_search_state(b);
    int ply = ss.search_ply(b.ply);

    Move repeated_hint = encode_move_from_uci(b, "e2e4");
    ASSERT(b.is_legal_move(repeated_hint),
           "move_selector_hint_dedup", "Setup move e2e4 should be legal");

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

    ASSERT(count_occurrences(selected, repeated_hint) == 1,
           "move_selector_hint_dedup",
           "Repeated hint move returned multiple times\n"
           << "Move: " << decode_move_to_uci(repeated_hint)
           << " Count: " << count_occurrences(selected, repeated_hint));

    MoveList expected = generate_moves<MoveGenMode::All>(b);
    if (!assert_no_duplicates(selected, "move_selector_hint_dedup", "global dedup")) return false;
    if (!assert_same_move_set(expected, selected, "move_selector_hint_dedup", "set-equivalence")) return false;

    return true;
}

bool test_move_selector_stale_hint_rejection(Board& b) {
    b.reset();
    b.load_from_fen("4k3/8/8/3p4/4P3/8/8/4K3 b - - 0 1");
    SearchState ss = make_search_state(b);
    int ply = ss.search_ply(b.ply);

    Move stale_tt(E2, E4, MoveType::Quiet, MoveFlag::Normal);
    Move stale_killer_1(A1, A8, MoveType::Quiet, MoveFlag::Normal);
    Move stale_killer_2(H2, H4, MoveType::Quiet, MoveFlag::Normal);

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

    ASSERT(!contains_move(selected, stale_tt)
           && !contains_move(selected, stale_killer_1)
           && !contains_move(selected, stale_killer_2),
           "move_selector_stale_hint", "Stale hint leaked into output");

    MoveList expected = generate_moves<MoveGenMode::All>(b);
    if (!assert_no_duplicates(selected, "move_selector_stale_hint", "stale-hint rejection")) return false;
    if (!assert_same_move_set(expected, selected, "move_selector_stale_hint", "stale-hint rejection")) return false;

    return true;
}

bool test_move_selector_quiet_history_order(Board& b) {
    b.reset();
    b.load_from_fen(START_POS_FEN);
    SearchState ss = make_search_state(b);

    Move higher = encode_move_from_uci(b, "e2e4");
    Move lower = encode_move_from_uci(b, "d2d4");

    ASSERT(b.is_legal_move(higher) && b.is_legal_move(lower),
           "move_selector_quiet_history", "Setup quiet moves must be legal");

    Piece higher_piece = b.piece_map[higher.from()];
    Piece lower_piece = b.piece_map[lower.from()];
    ss.side_piece_to_history[b.to_move][higher_piece][higher.to()] = 8000;
    ss.from_to_history[higher.from()][higher.to()] = 8000;
    ss.side_piece_to_history[b.to_move][lower_piece][lower.to()] = 1000;
    ss.from_to_history[lower.from()][lower.to()] = 1000;

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
    ASSERT(higher_index != -1 && lower_index != -1,
           "move_selector_quiet_history", "Expected quiet moves not found in selector output");

    if (higher_index >= lower_index) {
        std::clog << "[FAILURE] 'move_selector_quiet_history' - Quiet history ordering not respected\n";
        std::clog << "Higher move: " << decode_move_to_uci(higher) << " index=" << higher_index << "\n";
        std::clog << "Lower move: " << decode_move_to_uci(lower) << " index=" << lower_index << "\n";
        return false;
    }

    return true;
}

bool test_move_selector_see_phase_split(Board& b) {
    b.reset();
    b.load_from_fen("4k3/8/2p5/3p4/4P3/8/8/3QK3 w - - 0 1");
    SearchState ss = make_search_state(b);

    Move good_capture = encode_move_from_uci(b, "e4d5");
    Move bad_capture = encode_move_from_uci(b, "d1d5");

    ASSERT(b.is_legal_move(good_capture) && b.is_legal_move(bad_capture),
           "move_selector_see_phase_split", "Setup captures must be legal");

    int good_see = see(b, good_capture);
    int bad_see = see(b, bad_capture);
    ASSERT(good_see >= 0 && bad_see < 0,
           "move_selector_see_phase_split",
           "Setup must produce one good and one bad capture\n"
           << "good_see(e4d5)=" << good_see << " bad_see(d1d5)=" << bad_see);

    MoveList selected;
    if (!collect_selector_moves(
        b,
        ss,
        selected,
        "move_selector_see_phase_split",
        "good captures should come before quiets, bad captures after quiets"
    )) {
        return false;
    }

    int good_index = find_move_index(selected, good_capture);
    int bad_index = find_move_index(selected, bad_capture);
    ASSERT(good_index != -1 && bad_index != -1,
           "move_selector_see_phase_split", "Expected captures not found in selector output");

    ASSERT(good_index < bad_index,
           "move_selector_see_phase_split",
           "Good capture should precede bad capture\n"
           << "good index=" << good_index << " bad index=" << bad_index);

    int first_quiet = -1;
    int last_quiet = -1;
    for (int i = 0; i < selected.size; i++) {
        if (selected[i].type() == MoveType::Quiet) {
            if (first_quiet == -1) first_quiet = i;
            last_quiet = i;
        }
    }

    ASSERT(first_quiet != -1,
           "move_selector_see_phase_split", "Expected at least one quiet move in setup");

    ASSERT(good_index <= first_quiet,
           "move_selector_see_phase_split",
           "Good capture should be before first quiet\n"
           << "good index=" << good_index << " first quiet index=" << first_quiet);

    ASSERT(bad_index >= last_quiet,
           "move_selector_see_phase_split",
           "Bad capture should be after quiet phase\n"
           << "bad index=" << bad_index << " last quiet index=" << last_quiet);

    for (int i = bad_index + 1; i < selected.size; i++) {
        ASSERT(selected[i].type() != MoveType::Quiet,
               "move_selector_see_phase_split",
               "Quiet move appeared after bad capture phase\n"
               << "Quiet move: " << decode_move_to_uci(selected[i]) << " at index " << i);
    }

    return true;
}

bool test_move_selector_bad_capture_ordering(Board& b) {
    b.reset();
    b.load_from_fen("4k3/8/1pp5/p2p4/8/8/8/R2QK3 w - - 0 1");
    SearchState ss = make_search_state(b);

    Move rook_bad = encode_move_from_uci(b, "a1a5");
    Move queen_bad = encode_move_from_uci(b, "d1d5");

    ASSERT(b.is_legal_move(rook_bad) && b.is_legal_move(queen_bad),
           "move_selector_bad_capture_ordering", "Setup captures must be legal");

    int rook_see = see(b, rook_bad);
    int queen_see = see(b, queen_bad);
    ASSERT(rook_see < 0 && queen_see < 0,
           "move_selector_bad_capture_ordering",
           "Setup captures should both be SEE-negative\n"
           << "rook_see(a1a5)=" << rook_see << " queen_see(d1d5)=" << queen_see);

    MoveList selected;
    if (!collect_selector_moves(
        b,
        ss,
        selected,
        "move_selector_bad_capture_ordering",
        "bad captures should still be tactical-score ordered in BAD_CAPTURE phase"
    )) {
        return false;
    }

    int rook_index = find_move_index(selected, rook_bad);
    int queen_index = find_move_index(selected, queen_bad);
    ASSERT(rook_index != -1 && queen_index != -1,
           "move_selector_bad_capture_ordering", "Expected bad captures not found");

    if (rook_index >= queen_index) {
        std::clog << "[FAILURE] 'move_selector_bad_capture_ordering' - Bad capture ordering mismatch\n";
        std::clog << "Expected a1a5 before d1d5 based on tactical score\n";
        std::clog << "a1a5 index=" << rook_index << " d1d5 index=" << queen_index << "\n";
        return false;
    }

    return true;
}

bool test_move_selector_in_check(Board& b) {
    b.reset();
    b.load_from_fen("4k3/8/8/4Q3/8/8/8/4K3 b - - 0 1");
    SearchState ss = make_search_state(b);
    int ply = ss.search_ply(b.ply);

    MoveList expected = generate_moves<MoveGenMode::All>(b);
    Move stale_tt(E2, E4, MoveType::Quiet, MoveFlag::Normal);

    Move legal_killer = NULL_MOVE;
    for (const Move move : expected) {
        if (move.type() == MoveType::Quiet) {
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

// --- SEE tests ---

bool assert_see_score(
    Board& b,
    const std::string& fen,
    const std::string& move_uci,
    int expected_score,
    const std::string& description
) {
    b.reset();
    b.load_from_fen(fen);
    Move move = encode_move_from_uci(b, move_uci);

    if (!b.is_legal_move(move)) {
        std::clog << "[FAILURE] 'see' - Setup move must be legal\n";
        std::clog << "Case: " << description << "\n";
        std::clog << "FEN: " << fen << "\n";
        std::clog << "Move: " << move_uci << "\n";
        return false;
    }

    Board before = b;
    int actual_score = see(b, move);

    if (!board_position_equal(before, b)) {
        std::clog << "[FAILURE] 'see' - Board mutated during SEE evaluation\n";
        std::clog << "Case: " << description << "\n";
        std::clog << "FEN: " << fen << "\n";
        std::clog << "Move: " << move_uci << "\n";
        return false;
    }

    if (actual_score != expected_score) {
        std::clog << "[FAILURE] 'see' - Incorrect SEE score\n";
        std::clog << "Case: " << description << "\n";
        std::clog << "FEN: " << fen << "\n";
        std::clog << "Move: " << move_uci << "\n";
        std::clog << "Expected: " << expected_score << " Got: " << actual_score << "\n";
        return false;
    }

    return true;
}

bool test_see_basic_cases(Board& b) {
    struct TestCase {
        std::string fen;
        std::string move_uci;
        int expected_score;
        std::string description;
    };

    TestCase tests[] = {
        {
            "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1",
            "e4d5",
            100,
            "pawn capture wins undefended pawn"
        },
        {
            "4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1",
            "e4d5",
            0,
            "pawn capture traded back by pawn recapture"
        },
        {
            "4k3/8/2p5/3p4/3Q4/8/8/4K3 w - - 0 1",
            "d4d5",
            -800,
            "queen captures defended pawn and loses exchange"
        },
        {
            "6k1/2p5/8/3pP3/8/8/8/3R2K1 w - d6 0 1",
            "e5d6",
            100,
            "en passant updates occupancy so rook x-ray recapture is seen"
        },
    };

    for (const auto& tc : tests) {
        if (!assert_see_score(b, tc.fen, tc.move_uci, tc.expected_score, tc.description)) {
            return false;
        }
    }

    return true;
}

} // namespace

bool test_move_selector(Board& b) {
    if (!test_move_selector_completeness_and_uniqueness(b)) return false;
    if (!test_move_selector_ordering_priority(b)) return false;
    if (!test_move_selector_hint_deduplication(b)) return false;
    if (!test_move_selector_stale_hint_rejection(b)) return false;
    if (!test_move_selector_quiet_history_order(b)) return false;
    if (!test_move_selector_see_phase_split(b)) return false;
    if (!test_move_selector_bad_capture_ordering(b)) return false;
    if (!test_move_selector_in_check(b)) return false;
    return true;
}

bool test_see(Board& b) {
    if (!test_see_basic_cases(b)) return false;
    return true;
}
