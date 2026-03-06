#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>

#include "board/board.hpp"

struct TestEntry {
    std::string group;
    std::string name;
    std::function<bool(Board&)> run;
};

// Forward declarations
bool test_in_check(Board& b);
bool test_is_legal_move(Board& b);
bool test_move_selector(Board& b);
bool test_see(Board& b);
bool test_eval(Board& b);
bool test_quiescence(Board& b);
bool test_san_parsing(Board& b);
bool test_zobrist(Board& b);
bool test_opening_book(Board& b);
bool test_game_end(Board& b);
bool test_search_limits(Board& b);
bool test_transposition_table();
bool test_null_move(Board& b);
bool test_pawn_table(Board& b);
bool test_search_helpers();
bool test_uci_helpers(Board& b);

inline const std::vector<TestEntry>& get_tests() {
    static const std::vector<TestEntry> tests = {
        {"core",      "in_check",            [](Board& b) { return test_in_check(b); }},
        {"core",      "is_legal_move",       [](Board& b) { return test_is_legal_move(b); }},
        {"search",    "move_selector",       [](Board& b) { return test_move_selector(b); }},
        {"search",    "see",                 [](Board& b) { return test_see(b); }},
        {"search",    "eval",                [](Board& b) { return test_eval(b); }},
        {"search",    "quiescence",          [](Board& b) { return test_quiescence(b); }},
        {"protocol",  "san_parsing",         [](Board& b) { return test_san_parsing(b); }},
        {"core",      "zobrist",             [](Board& b) { return test_zobrist(b); }},
        {"search",    "opening_book",        [](Board& b) { return test_opening_book(b); }},
        {"search",    "game_end",            [](Board& b) { return test_game_end(b); }},
        {"search",    "search_limits",       [](Board& b) { return test_search_limits(b); }},
        {"core",      "transposition_table", [](Board&)   { return test_transposition_table(); }},
        {"core",      "null_move",           [](Board& b) { return test_null_move(b); }},
        {"core",      "pawn_table",          [](Board& b) { return test_pawn_table(b); }},
        {"search",    "search_helpers",      [](Board&)   { return test_search_helpers(); }},
        {"protocol",  "uci_helpers",         [](Board& b) { return test_uci_helpers(b); }},
    };
    return tests;
}

inline const std::vector<std::string> TEST_GROUPS = {
    "core",
    "search",
    "protocol",
};

inline bool test_matches_selector(const TestEntry& test, const std::string& selector) {
    return (
        selector == test.group
        || selector == (test.group + "/" + test.name)
    );
}

inline bool is_valid_test_selector(const std::string& selector) {
    for (const std::string& group : TEST_GROUPS) {
        if (selector == group) return true;
    }

    for (const auto& test : get_tests()) {
        if (test_matches_selector(test, selector)) return true;
    }

    return false;
}

inline int run_tests(const std::vector<std::string>& selected) {
    Board b;
    int failures = 0;

    for (const auto& test : get_tests()) {
        if (!selected.empty()) {
            bool matched = false;
            for (const auto& s : selected) {
                if (test_matches_selector(test, s)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) continue;
        }

        if (test.run(b)) {
            std::clog << "[SUCCESS] '" << test.group << "/" << test.name << "'\n";
        } else {
            failures++;
        }
    }

    return failures;
}
