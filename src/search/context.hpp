#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <atomic>

#include "types.hpp"
#include "constants.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include "eval/pawn_table.hpp"
#include "search/opening_book.hpp"

struct ThreadPool;

// Per-thread search context
struct ThreadContext {
    bool is_main_thread = false;
    SearchDepth max_depth = MAX_SEARCH_PLY - 1; // Only used by main thread
    uint64_t max_nodes = 0;
    int soft_time = -1;
    int hard_time = -1;
    bool search_interrupted = false;

    uint64_t nodes = 0; // Per thread nodes
    int ply_offset = 0;
    const ThreadPool* thread_pool = nullptr;

    using KillerMoves = std::array<Move, MAX_SEARCH_PLY>;
    using SidePieceToHistory = std::array<std::array<std::array<MoveScore, NUM_SQUARES>, NUM_PIECES>, NUM_SIDES>;
    using FromToHistory = std::array<std::array<MoveScore, NUM_SQUARES>, NUM_SQUARES>;

    // Killer moves
    KillerMoves killer_1;
    KillerMoves killer_2;

    // History heuristic tables (for quiet moves)
    SidePieceToHistory side_piece_to_history;
    FromToHistory from_to_history;

    std::chrono::steady_clock::time_point search_start;
    std::chrono::steady_clock::time_point soft_deadline;
    std::chrono::steady_clock::time_point hard_deadline;

    int search_ply(int board_ply) const {
        return board_ply - ply_offset;
    }

    bool has_runtime_limits() const {
        return soft_time != -1 || hard_time != -1 || max_nodes > 0;
    }

    void set_deadlines_from(std::chrono::steady_clock::time_point now) {
        soft_deadline = std::chrono::steady_clock::time_point::max();
        hard_deadline = std::chrono::steady_clock::time_point::max();

        if (soft_time != -1) {
            soft_deadline = now + std::chrono::milliseconds(soft_time);
        }

        if (hard_time != -1) {
            hard_deadline = now + std::chrono::milliseconds(hard_time);
        }
    }

    uint64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - search_start
        ).count();
    }

    void reset(int board_ply) {
        nodes = 0;
        ply_offset = board_ply;
        search_interrupted = false;
        killer_1.fill(NULL_MOVE);
        killer_2.fill(NULL_MOVE);
        side_piece_to_history = {};
        from_to_history = {};
    }
};

// Global context shared by all threads
struct SharedContext {
    std::atomic<bool> external_stop{false};
    std::atomic<bool> main_finished{false};

    TranspositionTable transposition_table;
    PawnTable pawn_table;

    bool use_opening_book = true;
    OpeningBook opening_book;
};

inline SharedContext g_shared;
