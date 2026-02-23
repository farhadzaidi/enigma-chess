#pragma once

#include <string>
#include <cstdint>
#include <vector>

struct BenchFlags {
    bool verbose;
    bool fast;
    bool phased;
    bool movegen_only;
    bool engine_only;
};

struct MovegenBenchResult {
    bool success;
    size_t positions_tested;
    uint64_t total_nodes;
    double total_seconds;
};

struct EngineFailure {
    std::string fen;
    std::string expected_move;
    std::string got_move;
};

struct EngineBenchResult {
    bool success;
    size_t positions_tested;
    size_t positions_correct;
    std::vector<EngineFailure> failures;
};

struct BenchResults {
    MovegenBenchResult movegen;
    EngineBenchResult engine;
    bool ran_movegen;
    bool ran_engine;
};

MovegenBenchResult run_movegen_bench(bool verbose, bool fast, bool phased);
EngineBenchResult run_engine_bench(bool verbose, bool fast);
BenchResults run_bench(const BenchFlags& flags);