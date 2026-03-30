#include "tests/test.hpp"

const std::vector<TestEntry> TESTS = {
    {"check",               [](Board& b) { return test_check(b); }},
    {"legal",               [](Board& b) { return test_legal(b); }},
    {"make_unmake",         [](Board& b) { return test_make_unmake(b); }},
    {"perft",               [](Board& b) { return test_perft(b); }},
    {"zobrist",             [](Board& b) { return test_zobrist(b); }},
    {"opening_book",        [](Board& b) { return test_opening_book(b); }},
    {"search",              [](Board& b) { return test_search(b); }},
    {"draws",               [](Board& b) { return test_draws(b); }},
    {"transposition_table", [](Board&)   { return test_transposition_table(); }},
    {"null_move",           [](Board& b) { return test_null_move(b); }},
};

bool is_valid_test_selector(const std::string& selector) {
    for (const auto& test : TESTS) {
        if (selector == test.name) return true;
    }
    return false;
}

int run_tests(const std::vector<std::string>& selected) {
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
