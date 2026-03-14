#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <iostream>
#include <chrono>
#include <iomanip>

#include "types.hpp"
#include "board.hpp"
#include "perft.hpp"
#include "parse.hpp"

struct MovegenBenchFlags {
    bool verbose;
    bool fast;
    bool phased;
};

struct MovegenBenchResult {
    bool success;
    size_t positions_tested;
    uint64_t total_nodes;
    double total_seconds;
};

const int NUM_MOVEGEN_POSITIONS_FAST = 10;

std::vector<std::string> collect_lines(bool fast);
MovegenBenchResult run_movegen_bench(const MovegenBenchFlags& flags);
