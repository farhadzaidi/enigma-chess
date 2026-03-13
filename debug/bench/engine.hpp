#pragma once

#include <algorithm>
#include <string>
#include <cstdint>
#include <vector>
#include <iostream>

#include "types.hpp"
#include "board/board.hpp"
#include "parse.hpp"
#include "search/search.hpp"
#include "utils/notation.hpp"

struct EngineBenchFlags {
    bool verbose;
    bool fast;
    int threads;
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

const int NUM_ENGINE_POSITIONS_FAST = 10;
const int ENGINE_SEARCH_TIME_MS = 10000;
const int MAX_FAILURES_TO_DISPLAY = 10;

inline EngineBenchResult run_engine_bench(const EngineBenchFlags& flags) {
    std::clog << "Running engine bench...\n";
    Board b;

    std::vector<std::string> lines;
    read_file(lines, ENGINE_EPD, flags.fast ? NUM_ENGINE_POSITIONS_FAST : -1);

    size_t positions_tested = 0;
    size_t positions_correct = 0;
    std::vector<EngineFailure> failures;

    for (const auto& line : lines) {
        auto epd = parse_engine_epd_line(line);

        if (epd.fen.empty() || epd.best_move_san.empty()) {
            continue;
        }

        b.reset();
        b.load_from_fen(epd.fen);

        Move expected_move = parse_move_from_san(b, epd.best_move_san);
        if (expected_move == NULL_MOVE) {
            std::clog << "\n[FAILURE] Failed to parse expected move SAN\n";
            std::clog << "FEN: " << epd.fen << "\n";
            std::clog << "SAN: " << epd.best_move_san << "\n";
            failures.push_back({epd.fen, epd.best_move_san, "NULL"});
            positions_tested++;
            continue;
        }

        SearchLimits limits;
        limits.hard_time = ENGINE_SEARCH_TIME_MS;
        Move best_move = search(b, limits, flags.threads);
        positions_tested++;

        if (best_move != expected_move) {
            failures.push_back({
                epd.fen,
                epd.best_move_san + " (" + decode_move_to_uci(expected_move) + ")",
                decode_move_to_uci(best_move)
            });

            if (flags.verbose) {
                std::clog << "\n[FAILURE] Move mismatch\n";
                std::clog << "FEN: " << epd.fen << "\n";
                std::clog << "Expected: " << epd.best_move_san << " (" << decode_move_to_uci(expected_move) << ")\n";
                std::clog << "Got: " << decode_move_to_uci(best_move) << "\n";
            }
        } else {
            positions_correct++;
            if (flags.verbose) {
                std::clog << "\n[SUCCESS] FEN: " << epd.fen << "\n";
            }
        }
    }

    bool success = failures.empty();
    EngineBenchResult result = {success, positions_tested, positions_correct, failures};

    std::clog << "\n========== ENGINE BENCH RESULTS ==========\n";
    std::clog << "  Status: " << (result.success ? "SUCCESS - All positions matched" : "FAILED") << "\n";
    std::clog << "  Positions tested: " << result.positions_tested << "\n";
    std::clog << "  Positions correct: " << result.positions_correct << "/" << result.positions_tested << "\n";
    std::clog << "  Time per search: " << ENGINE_SEARCH_TIME_MS << " ms\n";

    if (!result.failures.empty()) {
        std::clog << "\n  Failures:\n";
        size_t num_to_display = std::min(result.failures.size(), static_cast<size_t>(MAX_FAILURES_TO_DISPLAY));
        for (size_t i = 0; i < num_to_display; i++) {
            const auto& failure = result.failures[i];
            std::clog << "    [" << (i + 1) << "] FEN: " << failure.fen << "\n";
            std::clog << "        Expected: " << failure.expected_move << "\n";
            std::clog << "        Got: " << failure.got_move << "\n";
        }
        if (result.failures.size() > static_cast<size_t>(MAX_FAILURES_TO_DISPLAY)) {
            std::clog << "    ... and " << (result.failures.size() - MAX_FAILURES_TO_DISPLAY) << " more failures (output truncated)\n";
        }
    }

    std::clog << "============================================\n";

    return result;
}
