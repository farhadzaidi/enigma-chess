#pragma once

#include <string>

#include "bench/movegen.hpp"
#include "bench/search.hpp"

void run_bench(
    const std::string& type = "",
    bool verbose = false,
    bool fast = false,
    bool phased = false,
    int threads = 1
);
