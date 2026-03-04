#pragma once

#include <atomic>
#include <thread>

#include "core/transposition_table.hpp"
#include "eval/pawn_table.hpp"
#include "search/search_state.hpp"
#include "search/opening_book.hpp"

// --- Mutable Global State ---

// UCI control flags
inline std::atomic<bool> stop_requested(false);
inline std::atomic<bool> pondering(false);
inline bool use_own_book = true;
inline bool enable_ponder = false;
inline std::thread search_thread;

// Core engine state
inline TranspositionTable transposition_table;
inline PawnTable pawn_table;
inline SearchState search_state;
inline OpeningBook opening_book;
