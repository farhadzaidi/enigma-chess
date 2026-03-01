#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>

#include "board.hpp"

struct TestEntry {
    std::string name;
    std::function<bool()> run;
};

// Forward declarations
bool test_in_check(Board& b);
bool test_is_legal_move(Board& b);
bool test_move_selector(Board& b);
bool test_parse_move_from_fen(Board& b);
bool test_zobrist(Board& b);
bool test_opening_book(Board& b);
bool test_game_end(Board& b);
bool test_transposition_table();

inline const std::vector<std::string> TEST_NAMES = {
    "in_check", "is_legal_move", "move_selector", "san_parsing", "zobrist", "opening_book",
    "game_end", "transposition_table"
};

inline int run_tests(const std::vector<std::string>& selected) {
    Board b;
    int failures = 0;

    TestEntry tests[] = {
        {"in_check",            [&]() { return test_in_check(b); }},
        {"is_legal_move",       [&]() { return test_is_legal_move(b); }},
        {"move_selector",       [&]() { return test_move_selector(b); }},
        {"san_parsing",         [&]() { return test_parse_move_from_fen(b); }},
        {"zobrist",             [&]() { return test_zobrist(b); }},
        {"opening_book",        [&]() { return test_opening_book(b); }},
        {"game_end",            [&]() { return test_game_end(b); }},
        {"transposition_table", [&]() { return test_transposition_table(); }},
    };

    for (const auto& test : tests) {
        if (!selected.empty()) {
            bool matched = false;
            for (const auto& s : selected) {
                if (s == test.name) { matched = true; break; }
            }
            if (!matched) continue;
        }

        if (test.run()) {
            std::clog << "[SUCCESS] '" << test.name << "'\n";
        } else {
            failures++;
        }
    }

    return failures;
}
