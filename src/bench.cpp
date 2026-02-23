// This file implements two benchmark suites for the chess engine:
//
// 1. MOVEGEN BENCH: Tests the accuracy of the move generator by loading FEN strings with
//    known node counts at various depths and comparing generated nodes against expected counts.
//    Due to the large number of positions, this is better suited for correctness testing.
//    For raw performance, perft is a better test.
//
// 2. ENGINE BENCH: Tests the search quality by searching tactical positions and comparing
//    the engine's best move against known best moves. Each position is searched for a fixed
//    time, and results show the percentage of positions where the engine found the best move.
//
// Usage: ./build/enigma bench [--fast] [--verbose] [--phased] [--movegen] [--engine]

#include <filesystem>
#include <iostream>
#include <cstdint>
#include <chrono>
#include <iomanip>

#include "types.hpp"
#include "bench.hpp"
#include "board.hpp"
#include "perft.hpp"
#include "utils.hpp"
#include "search.hpp"

const int NUM_MOVEGEN_POSITIONS_FAST = 1000;
const int NUM_ENGINE_POSITIONS_FAST = 10;
const int ENGINE_SEARCH_TIME_MS = 10000;
const int MAX_FAILURES_TO_DISPLAY = 10;

static inline std::vector<std::string> collect_lines(bool fast) {
    std::vector<std::string> buffer;
    int limit = fast ? NUM_MOVEGEN_POSITIONS_FAST : -1;

    // Read all movegen EPD files
    read_file(buffer, SINGLE_CHECK_EPD, limit);
    read_file(buffer, DOUBLE_CHECK_EPD, limit);
    read_file(buffer, CPW_EPD, limit);
    read_file(buffer, EN_PASSANT_EPD, limit);
    read_file(buffer, MIXED_EPD, limit);

    return buffer;
}

MovegenBenchResult run_movegen_bench(bool verbose, bool fast, bool phased) {
    std::clog << "Running movegen bench...\n";
    Board b;
    auto lines = collect_lines(fast);
    uint64_t total_nodes = 0;

    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& line : lines) {
        auto result = parse_perft_epd_line(line);

        b.reset();
        b.load_from_fen(result.fen);

        // Loop through all depth/node pairs and test each
        for (const auto& [depth, expected_nodes] : result.depth_nodes) {
            uint64_t nodes = phased ? perft_phased(b, depth) : perft<false>(b, depth);
            total_nodes += nodes;

            if (nodes != expected_nodes) {
                std::clog << "\n[FAILURE] FEN: " << result.fen << "\n";
                std::clog << "At depth " << depth << ", expected " << expected_nodes << " nodes, but generated " << nodes << "\n";
                return {false, lines.size(), total_nodes, 0.0};
            } else if (verbose) {
                std::clog << "\n[SUCCESS] FEN: " << result.fen << "\n";
                std::clog << "At depth " << depth << ", generated " << nodes << " nodes\n";
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();

    return {true, lines.size(), total_nodes, seconds};
}

EngineBenchResult run_engine_bench(bool verbose, bool fast) {
    std::clog << "Running engine bench...\n";
    Board b;

    // Read engine benchmark file
    std::vector<std::string> lines;
    read_file(lines, ENGINE_EPD, fast ? NUM_ENGINE_POSITIONS_FAST : -1);

    size_t positions_tested = 0;
    size_t positions_correct = 0;
    std::vector<EngineFailure> failures;

    for (const auto& line : lines) {
        auto epd = parse_engine_epd_line(line);

        if (epd.fen.empty() || epd.best_move_san.empty()) {
            continue;  // Skip invalid lines
        }

        // Load position
        b.reset();
        b.load_from_fen(epd.fen);

        // Parse expected move from SAN
        Move expected_move = parse_move_from_san(b, epd.best_move_san);
        if (expected_move == NULL_MOVE) {
            std::clog << "\n[FAILURE] Failed to parse expected move SAN\n";
            std::clog << "FEN: " << epd.fen << "\n";
            std::clog << "SAN: " << epd.best_move_san << "\n";
            failures.push_back({epd.fen, epd.best_move_san, "NULL"});
            positions_tested++;
            continue;
        }

        // Search the position
        Move best_move = search_time(b, ENGINE_SEARCH_TIME_MS);
        positions_tested++;

        // Compare moves
        if (best_move != expected_move) {
            failures.push_back({
                epd.fen,
                epd.best_move_san + " (" + decode_move_to_uci(expected_move) + ")",
                decode_move_to_uci(best_move)
            });

            if (verbose) {
                std::clog << "\n[FAILURE] Move mismatch\n";
                std::clog << "FEN: " << epd.fen << "\n";
                std::clog << "Expected: " << epd.best_move_san << " (" << decode_move_to_uci(expected_move) << ")\n";
                std::clog << "Got: " << decode_move_to_uci(best_move) << "\n";
            }
        } else {
            positions_correct++;
            if (verbose) {
                std::clog << "\n[SUCCESS] FEN: " << epd.fen << "\n";
            }
        }
    }

    bool success = failures.empty();
    return {success, positions_tested, positions_correct, failures};
}

BenchResults run_bench(const BenchFlags& flags) {
    BenchResults results = {};

    // Determine what to run
    bool run_movegen = !flags.engine_only;
    bool run_engine = !flags.movegen_only;

    // Run movegen bench
    if (run_movegen) {
        results.movegen = run_movegen_bench(flags.verbose, flags.fast, flags.phased);
        results.ran_movegen = true;
    }

    // Run engine bench
    if (run_engine) {
        results.engine = run_engine_bench(flags.verbose, flags.fast);
        results.ran_engine = true;
    }

    // Output final results
    std::clog << "\n========== BENCH RESULTS ==========\n";

    if (results.ran_movegen) {
        std::clog << "\n[MOVEGEN BENCH]\n";
        if (results.movegen.success) {
            std::clog << "  Status: SUCCESS\n";
            std::clog << "  Positions tested: " << results.movegen.positions_tested << "\n";
            std::clog << "  Total nodes: " << results.movegen.total_nodes << "\n";
            std::clog << "  Time: " << std::fixed << std::setprecision(1) << results.movegen.total_seconds << " seconds\n";
            std::clog << "  Nodes/sec: " << static_cast<uint64_t>(results.movegen.total_nodes / results.movegen.total_seconds) << "\n";
        } else {
            std::clog << "  Status: FAILED\n";
        }
    }

    if (results.ran_engine) {
        std::clog << "\n[ENGINE BENCH]\n";
        if (results.engine.success) {
            std::clog << "  Status: SUCCESS - All positions matched\n";
        } else {
            std::clog << "  Status: FAILED\n";
        }
        std::clog << "  Positions tested: " << results.engine.positions_tested << "\n";
        std::clog << "  Positions correct: " << results.engine.positions_correct << "/" << results.engine.positions_tested << "\n";
        std::clog << "  Time per search: " << ENGINE_SEARCH_TIME_MS << " ms\n";

        if (!results.engine.failures.empty()) {
            std::clog << "\n  Failures:\n";
            size_t num_to_display = std::min(results.engine.failures.size(), static_cast<size_t>(MAX_FAILURES_TO_DISPLAY));
            for (size_t i = 0; i < num_to_display; i++) {
                const auto& failure = results.engine.failures[i];
                std::clog << "    [" << (i + 1) << "] FEN: " << failure.fen << "\n";
                std::clog << "        Expected: " << failure.expected_move << "\n";
                std::clog << "        Got: " << failure.got_move << "\n";
            }
            if (results.engine.failures.size() > MAX_FAILURES_TO_DISPLAY) {
                std::clog << "    ... and " << (results.engine.failures.size() - MAX_FAILURES_TO_DISPLAY) << " more failures (output truncated)\n";
            }
        }
    }

    std::clog << "===================================\n";

    return results;
}
