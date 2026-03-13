#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include "board/board.hpp"
#include "constants.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include "types.hpp"
#include "print.hpp"
#include "search/context.hpp"
#include "utils/notation.hpp"

Move search(Board& b, ThreadContext& ctx, SearchDepth depth);

constexpr int MIN_THREADS = 1;
constexpr int MAX_THREADS = 64;

struct SearchLimits {
    int soft_time = -1;
    int hard_time = -1;
    SearchDepth max_depth = MAX_SEARCH_PLY - 1;
    uint64_t max_nodes = 0;
};

struct ThreadPool {
    std::thread main_thread;
    std::vector<std::thread> helper_threads;
    std::vector<ThreadContext> contexts;
    Move best_move = NULL_MOVE;

    ~ThreadPool() { stop(); }

    inline void start(
        const Board& b,
        const SearchLimits& limits,
        int num_threads
    ) {
        stop();

        reset();
        num_threads = std::clamp(num_threads, MIN_THREADS, MAX_THREADS);
        contexts.resize(num_threads);
        for (int i = 0; i < num_threads; i++) {
            contexts[i] = ThreadContext{};
            contexts[i].is_main_thread = i == 0;
            contexts[i].thread_pool = this;
        }

        ThreadContext& main_ctx = contexts[0];
        main_ctx.max_depth = limits.max_depth;
        main_ctx.soft_time = limits.soft_time;
        main_ctx.hard_time = limits.hard_time;
        main_ctx.max_nodes = limits.max_nodes;

        for (int i = 0; i < num_threads; i++) {
            ThreadContext& ctx = contexts[i];
            SearchDepth start_depth = static_cast<SearchDepth>(1 + i % 2);
            Board thread_board = b;
            auto worker = [this, i, board = std::move(thread_board), start_depth]() mutable {
                Move move = ::search(board, contexts[i], start_depth);
                if (contexts[i].is_main_thread) {
                    best_move = move;
                    emit_best_move(board);
                }
            };

            if (ctx.is_main_thread) {
                main_thread = std::thread(std::move(worker));
            } else {
                helper_threads.emplace_back(std::move(worker));
            }
        }
    }

    inline void apply_limits(const SearchLimits& limits) {
        if (contexts.empty()) {
            return;
        }

        ThreadContext& ctx = contexts[0];
        ctx.max_depth = limits.max_depth;
        ctx.soft_time = limits.soft_time;
        ctx.hard_time = limits.hard_time;
        ctx.max_nodes = limits.max_nodes;
        ctx.set_deadlines_from(std::chrono::steady_clock::now());
    }

    inline Move finish() {
        if (main_thread.joinable()) {
            main_thread.join();
        }

        for (auto& thread : helper_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        helper_threads.clear();
        g_shared.external_stop = false;
        g_shared.main_finished = false;
        return best_move;
    }

    inline Move stop() {
        g_shared.external_stop = true;
        return finish();
    }

    inline Move search(
        const Board& b,
        const SearchLimits& limits,
        int num_threads
    ) {
        start(b, limits, num_threads);
        return finish();
    }

    inline void clear() {
        stop();
        reset();
        g_shared.transposition_table.clear();
    }

    inline uint64_t total_nodes() const {
        uint64_t total = 0;
        for (const auto& ctx : contexts) {
            total += ctx.nodes;
        }
        return total;
    }

private:
    inline void reset() {
        best_move = NULL_MOVE;
        contexts.clear();
        g_shared.external_stop = false;
        g_shared.main_finished = false;
        g_shared.transposition_table.generation++;
    }

    inline void emit_best_move(Board& b) {
        std::string ponder_str;
        if (best_move != NULL_MOVE) {
            b.make_move(best_move);
            TTEntry* tt_entry = g_shared.transposition_table.get_entry(b.position_hash);
            if (tt_entry && tt_entry->move() != NULL_MOVE) {
                ponder_str = " ponder " + decode_move_to_uci(tt_entry->move());
            }
            b.unmake_move(best_move);
        }

        uci_print("bestmove " + decode_move_to_uci(best_move) + ponder_str);
    }
};
