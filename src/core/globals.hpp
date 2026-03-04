#pragma once

#include <atomic>

inline std::atomic<bool> stop_requested(false);
inline std::atomic<bool> pondering(false);
inline bool use_own_book = true;
