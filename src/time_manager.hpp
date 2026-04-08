#pragma once

#include <cstdint>

class TimeManager {
public:
    /** Compute base and maximum time from clock info. Returns hard max time. */
    int allocate_time(
        int remaining,
        int increment,
        int movestogo,
        double phase_ratio,
        bool is_ponder
    );

    /** Disable soft TM for explicit movetime/depth/nodes searches. */
    void disable();

    /** Initialize per-search state. */
    void init_search(int num_legal_moves);

    /** Notify that root best move changed. */
    void on_root_best_move_change();

    /** Returns true if soft time says stop. */
    bool should_stop_after_depth(
        int depth,
        int score,
        uint64_t elapsed_ms
    );

private:
    // Allocation results (set by allocate_time)
    bool enabled_ = false;
    double soft_base_ = 0.0;
    double hard_max_ = 0.0;

    // Per-search rolling state (reset in init_search)
    int num_legal_moves_ = 0;
    int best_move_stability_ = 0;
    int best_move_changes_ = 0;
    int prev_score_ = 0;
    bool has_prev_score_ = false;
};

inline TimeManager& g_tm() {
    static TimeManager instance;
    return instance;
}
