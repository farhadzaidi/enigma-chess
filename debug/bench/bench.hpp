#pragma once

#include <string>

#include "bench/movegen.hpp"
#include "bench/engine.hpp"

inline void run_bench(
    const std::string& type = "",
    bool verbose = false,
    bool fast = false,
    bool phased = false,
    int threads = 1
) {
    bool run_movegen = type.empty() || type == "movegen";
    bool run_engine = type.empty() || type == "engine";

    if (run_movegen) {
        run_movegen_bench({verbose, fast, phased});
    }
    
    if (run_engine) {
        run_engine_bench({verbose, fast, threads});
    }
}
