#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>

#include "board/board.hpp"

struct TestEntry {
    std::string name;
    std::function<bool(Board&)> run;
};

// Forward declarations
bool test_check(Board& b);
bool test_legal(Board& b);
bool test_make_unmake(Board& b);
bool test_perft(Board& b);
bool test_move_selector(Board& b);
bool test_see(Board& b);
bool test_eval(Board& b);
bool test_quiescence(Board& b);
bool test_zobrist(Board& b);
bool test_opening_book(Board& b);
bool test_search(Board& b);
bool test_draws(Board& b);
bool test_transposition_table();
bool test_null_move(Board& b);
bool test_pawn_table(Board& b);


inline const std::vector<TestEntry> TESTS = {
    {"check",               [](Board& b) { return test_check(b); }},
    {"legal",               [](Board& b) { return test_legal(b); }},
    {"make_unmake",         [](Board& b) { return test_make_unmake(b); }},
    {"perft",               [](Board& b) { return test_perft(b); }},
    {"move_selector",       [](Board& b) { return test_move_selector(b); }},
    {"see",                 [](Board& b) { return test_see(b); }},
    {"eval",                [](Board& b) { return test_eval(b); }},
    {"quiescence",          [](Board& b) { return test_quiescence(b); }},
    {"zobrist",             [](Board& b) { return test_zobrist(b); }},
    {"opening_book",        [](Board& b) { return test_opening_book(b); }},
    {"search",              [](Board& b) { return test_search(b); }},
    {"draws",               [](Board& b) { return test_draws(b); }},
    {"transposition_table", [](Board&)   { return test_transposition_table(); }},
    {"null_move",           [](Board& b) { return test_null_move(b); }},
    {"pawn_table",          [](Board& b) { return test_pawn_table(b); }},
};

inline bool is_valid_test_selector(const std::string& selector) {
    for (const auto& test : TESTS) {
        if (selector == test.name) return true;
    }
    return false;
}

inline int run_tests(const std::vector<std::string>& selected) {
    Board b;
    int failures = 0;

    for (const auto& test : TESTS) {
        if (!selected.empty()) {
            bool matched = false;
            for (const auto& s : selected) {
                if (s == test.name) {
                    matched = true;
                    break;
                }
            }
            if (!matched) continue;
        }

        if (test.run(b)) {
            std::clog << "[SUCCESS] '" << test.name << "'\n";
        } else {
            failures++;
        }
    }

    return failures;
}
