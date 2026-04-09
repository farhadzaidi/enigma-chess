#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "board.hpp"
#include "move.hpp"
#include "opening_book.hpp"
#include "transposition_table.hpp"
#include "types.hpp"

/** Rebuild the LMR table from the current prm.lmr_tuning_constant. */
void build_lmr_table();

// --- Search Constants ---

constexpr PositionScore CHECKMATE_SCORE = 32'000;
constexpr PositionScore STALEMATE_SCORE = 0;

constexpr MoveScore MAX_MOVE_SCORE = 32'000;
constexpr MoveScore MIN_MOVE_SCORE = -MAX_MOVE_SCORE;

constexpr int MIN_THREADS = 1;
constexpr int MAX_THREADS = 64;
constexpr int MIN_MULTI_PV = 1;
constexpr int MAX_MULTI_PV = 16;

// --- Engine ---

class Engine {
public:
    struct SearchResult {
        Move move;
        PositionScore score;
    };

    // --- Lifecycle ---

    Engine();
    /** Joins all search threads and cleans up. */
    ~Engine();

    // --- Async search ---

    /** Launch an async search with any combination of limits (depth, time, nodes). */
    void search(const Board& board, SearchDepth max_depth, int max_time, uint64_t max_nodes);
    /** Launch an async search to a fixed depth. */
    void search_depth(const Board& board, SearchDepth depth);
    /** Launch an async search with a hard time limit (ms). */
    void search_time(const Board& board, int max_time);
    /** Launch an async search bounded by a node count. */
    void search_nodes(const Board& board, uint64_t max_nodes);
    /** Launch an unbounded async search (stopped only by stop()). */
    void search_infinite(const Board& board);

    // --- Ponder support ---

    /** Apply limits to an already-running search (e.g. after ponderhit). */
    void apply_limits(SearchDepth max_depth, int max_time, uint64_t max_nodes);

    // --- Control ---

    /** Signal all threads to stop and wait for them to finish. */
    void stop();
    /** Stop any running search and wipe the TT and pawn table. */
    void clear();

    // --- Config ---

    /** Set the number of search threads. */
    void set_threads(int n);
    /** Enable or disable the compiled-in opening book. */
    void set_use_opening_book(bool enabled);
    /** Set the number of principal variations to report. */
    void set_multi_pv(int n);

    // --- Info ---

    /** Sum of nodes searched across all threads. */
    uint64_t total_nodes() const;

    // --- Synchronous search ---

    /** Blocking search with any combination of limits. */
    SearchResult sync_search(Board& board, SearchDepth max_depth, int max_time, uint64_t max_nodes);
    /** Blocking search to a fixed depth. */
    SearchResult sync_search_depth(Board& board, SearchDepth depth);
    /** Blocking search with a hard time limit. */
    SearchResult sync_search_time(Board& board, int max_time);
    /** Blocking search bounded by node count. */
    SearchResult sync_search_nodes(Board& board, uint64_t max_nodes);


private:

    // --- Types ---

    /** Main-thread-only state: search limits, deadlines, and PV table. */
    struct MainState {
        SearchDepth max_depth = MAX_SEARCH_PLY - 1;
        uint64_t max_nodes = 0;
        int max_time = -1;

        std::chrono::steady_clock::time_point search_start;
        std::chrono::steady_clock::time_point soft_deadline;
        std::chrono::steady_clock::time_point hard_deadline;

        Move pv[MAX_SEARCH_PLY][MAX_SEARCH_PLY];
        int pv_length[MAX_SEARCH_PLY] = {};

        bool has_runtime_limits() const;
        void set_deadlines_from(std::chrono::steady_clock::time_point now);
        uint64_t elapsed_ms() const;
    };

    /** Per-thread search state: counters and move-ordering tables. */
    struct Context {
        MainState* main = nullptr;
        bool search_interrupted = false;

        uint64_t nodes = 0;
        int ply_offset = 0;  // board ply at root, so search_ply = board_ply - ply_offset

        using KillerMoves = std::array<Move, MAX_SEARCH_PLY>;
        using SidePieceToHistory = std::array<std::array<std::array<MoveScore, NUM_SQUARES>, NUM_PIECES>, NUM_SIDES>;
        using FromToHistory = std::array<std::array<MoveScore, NUM_SQUARES>, NUM_SQUARES>;
        using CounterMoveTable = std::array<std::array<Move, NUM_SQUARES>, NUM_PIECES>;
        using ContinuationHistory = std::array<std::array<std::array<std::array<MoveScore, NUM_SQUARES>, NUM_PIECES>, NUM_SQUARES>, NUM_PIECES>;

        KillerMoves killer_1;
        KillerMoves killer_2;

        CounterMoveTable countermoves;
        SidePieceToHistory side_piece_to_history;
        FromToHistory from_to_history;
        ContinuationHistory continuation_history;

        int search_ply(int board_ply) const;
        Move get_countermove(const Board& board) const;
        void reset(int board_ply);
    };

    class MoveSelector;
    struct TTProbeResult;

    // --- State ---

    OpeningBook opening_book_;
    std::atomic<bool> external_stop_{false};
    std::atomic<bool> main_finished_{false};
    bool use_opening_book_ = true;
    int num_threads_ = 1;
    int multi_pv_ = -1;

    std::thread main_thread_;
    std::vector<std::thread> helper_threads_;
    std::vector<Context> contexts_;
    MainState main_state_;
    SearchResult best_result_;

    // --- Thread management ---

    /** Join all threads and return the result. */
    SearchResult finish();
    /** Reset shared state for the next search. */
    void reset();
    /** Return a legal root book move, or NULL_MOVE if the position is out of book. */
    Move find_book_move(const Board& board);

    // --- Stop check ---

    /** Check time/node limits; may set search_interrupted. Amortised via node-count mask. */
    bool should_stop_search(Context& ctx);
    /** Notify other threads that this thread is done (main tells helpers to quit). */
    void signal_stop(const Context& ctx);

    // --- UCI output ---

    /** Whether multi-PV mode has been explicitly enabled. */
    bool is_multi_pv_enabled() const;
    /** Print a UCI info line with depth, score, nodes, PV, and multi-PV index. */
    void emit_search_info(const Context& ctx, SearchDepth depth, PositionScore score, int multipv = 0);
    /** Print UCI bestmove with the engine's best move and optionally a ponder move. */
    void emit_best_move(Board& board);

    // --- TT helpers ---

    /** Write a search result into the transposition table with the correct bound type. */
    void store_tt_result(
        const Board& b,
        Context& ctx,
        Move best_move,
        SearchDepth depth,
        PositionScore best_score,
        PositionScore original_alpha,
        PositionScore beta
    );

    // --- Search algorithms ---

    /** Alpha-beta search with null move pruning, futility pruning, and LMR. */
    PositionScore negamax(
        Board& board,
        Context& ctx,
        SearchDepth depth,
        PositionScore alpha,
        PositionScore beta,
        bool allow_null_move = true,
        Move excluded_move = NULL_MOVE
    );

    /** Probe the TT for a cutoff or best-move hint; falls back to IID on PV misses. */
    TTProbeResult probe_tt(
        Board& board,
        Context& ctx,
        SearchDepth depth,
        PositionScore alpha,
        PositionScore beta,
        bool is_pv_node
    );

    /** Capture-only search to stabilise the evaluation at horizon nodes. */
    PositionScore quiescence_search(
        Board& board,
        Context& ctx,
        PositionScore alpha,
        PositionScore beta
    );

    /** Root-level PVS search at a given depth (called from iterative deepening). */
    SearchResult search_at_depth(
        Board& board,
        Context& ctx,
        SearchDepth depth,
        Move prev_best_move,
        PositionScore alpha,
        PositionScore beta,
        const MoveList& excluded_moves = {}
    );

    /** Iterative deepening loop with aspiration windows and soft time management. */
    SearchResult iterative_deepening(Board& board, Context& ctx, SearchDepth depth);

    // --- Internal helpers ---

    /** Prepend a move to the child's PV line, building the PV for the current ply. */
    static void update_pv(Context& ctx, int ply, Move move);
    /** Slot a new killer move into the two-slot killer table for this ply. */
    static void update_killer_table(Context& ctx, Move move, int ply);
    /** Update killer, history, and butterfly tables on a beta cutoff. */
    static void handle_beta_cutoff(Board& b, Context& ctx, Move cutoff_move, SearchDepth depth, MoveList& searched_quiet_moves);
};
