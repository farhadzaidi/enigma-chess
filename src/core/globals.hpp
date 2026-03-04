#pragma once

#include <atomic>
#include <thread>

#include "core/transposition_table.hpp"
#include "eval/pawn_table.hpp"
#include "search/search_state.hpp"
#include "search/opening_book.hpp"

// --- Mutable Global State ---

// UCI control flags
inline std::atomic<bool> g_stop_requested(false);
inline std::atomic<bool> g_pondering(false);
inline bool g_use_own_book = true;
inline bool g_enable_ponder = false;
inline std::thread g_search_thread;

// Core engine state
inline TranspositionTable g_transposition_table;
inline PawnTable g_pawn_table;
inline SearchState g_search_state;
inline OpeningBook g_opening_book;
