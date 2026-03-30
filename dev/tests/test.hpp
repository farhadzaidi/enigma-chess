#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>

#include "board.hpp"

struct TestEntry {
    std::string name;
    std::function<bool(Board&)> run;
};

// Forward declarations
bool test_check(Board& b);
bool test_legal(Board& b);
bool test_make_unmake(Board& b);
bool test_perft(Board& b);
bool test_zobrist(Board& b);
bool test_opening_book(Board& b);
bool test_search(Board& b);
bool test_draws(Board& b);
bool test_transposition_table();
bool test_null_move(Board& b);
bool test_nnue(Board& b);


extern const std::vector<TestEntry> TESTS;

bool is_valid_test_selector(const std::string& selector);
int run_tests(const std::vector<std::string>& selected);
