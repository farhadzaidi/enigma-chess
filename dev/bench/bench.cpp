#include "bench/bench.hpp"

void run_bench(
    const std::string& type,
    bool verbose,
    bool fast,
    bool phased,
    int threads
) {
    bool run_movegen = type.empty() || type == "movegen";
    bool run_search = type.empty() || type == "search" || type == "engine";

    if (run_movegen) {
        run_movegen_bench({verbose, fast, phased});
    }

    if (run_search) {
        run_search_bench({verbose, fast, threads});
    }
}
