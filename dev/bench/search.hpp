#pragma once

#include <algorithm>
#include <string>
#include <cstdint>
#include <vector>
#include <iostream>

#include "types.hpp"
#include "board.hpp"
#include "parse.hpp"
#include "engine.hpp"
#include "notation.hpp"

struct SearchBenchFlags {
    bool verbose;
    bool fast;
    int threads;
};

struct SearchFailure {
    std::string fen;
    std::string expected_move;
    std::string got_move;
};

struct SearchBenchResult {
    bool success;
    size_t positions_tested;
    size_t positions_correct;
    std::vector<SearchFailure> failures;
};

const int NUM_SEARCH_POSITIONS_FAST = 10;
const int SEARCH_BENCH_TIME_MS = 5000;
const int MAX_FAILURES_TO_DISPLAY = 10;

SearchBenchResult run_search_bench(const SearchBenchFlags& flags);
