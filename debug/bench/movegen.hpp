#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <iostream>
#include <chrono>
#include <iomanip>

#include "types.hpp"
#include "board/board.hpp"
#include "move_generator/perft.hpp"
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

const int NUM_MOVEGEN_POSITIONS_FAST = 1000;

inline std::vector<std::string> collect_lines(bool fast) {
    std::vector<std::string> buffer;
    int limit = fast ? NUM_MOVEGEN_POSITIONS_FAST : -1;

    read_file(buffer, SINGLE_CHECK_EPD, limit);
    read_file(buffer, DOUBLE_CHECK_EPD, limit);
    read_file(buffer, CPW_EPD, limit);
    read_file(buffer, EN_PASSANT_EPD, limit);
    read_file(buffer, MIXED_EPD, limit);

    return buffer;
}

inline MovegenBenchResult run_movegen_bench(const MovegenBenchFlags& flags) {
    std::clog << "Running movegen bench...\n";
    Board b;
    auto lines = collect_lines(flags.fast);
    uint64_t total_nodes = 0;

    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& line : lines) {
        auto result = parse_perft_epd_line(line);

        b.reset();
        b.load_from_fen(result.fen);

        for (const auto& [depth, expected_nodes] : result.depth_nodes) {
            uint64_t nodes = flags.phased ? perft_phased(b, depth) : perft<false>(b, depth);
            total_nodes += nodes;

            if (nodes != expected_nodes) {
                std::clog << "\n[FAILURE] FEN: " << result.fen << "\n";
                std::clog << "At depth " << depth << ", expected " << expected_nodes << " nodes, but generated " << nodes << "\n";
                return {false, lines.size(), total_nodes, 0.0};
            } else if (flags.verbose) {
                std::clog << "\n[SUCCESS] FEN: " << result.fen << "\n";
                std::clog << "At depth " << depth << ", generated " << nodes << " nodes\n";
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();

    MovegenBenchResult result_out = {true, lines.size(), total_nodes, seconds};

    std::clog << "\n========== MOVEGEN BENCH RESULTS ==========\n";
    std::clog << "  Status: SUCCESS\n";
    std::clog << "  Positions tested: " << result_out.positions_tested << "\n";
    std::clog << "  Total nodes: " << result_out.total_nodes << "\n";
    std::clog << "  Time: " << std::fixed << std::setprecision(1) << result_out.total_seconds << " seconds\n";
    std::clog << "  Nodes/sec: " << static_cast<uint64_t>(result_out.total_nodes / result_out.total_seconds) << "\n";
    std::clog << "=============================================\n";

    return result_out;
}
