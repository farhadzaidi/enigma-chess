#include "engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

#include "bitboard.hpp"
#include "evaluate.hpp"
#include "pawn_table.hpp"
#include "move_generator.hpp"
#include "print.hpp"
#include "notation.hpp"

namespace {

// --- Constants ---

constexpr PositionScore DUMMY_SCORE = -32'700;
constexpr PositionScore SEARCH_INTERRUPTED = DUMMY_SCORE;

// Only check the clock every N nodes (N = mask + 1) to avoid syscall overhead
constexpr uint64_t TIME_CHECK_PERIOD_MASK = 2047;

constexpr int ASPIRATION_WINDOW = 25;
constexpr int SCORE_DROP_THRESHOLD = 50;

constexpr int NULL_MOVE_BASE_REDUCTION = 2;
constexpr SearchDepth NULL_MOVE_DEEPER_THRESHOLD = 6;

constexpr int FUTILITY_MARGIN_PER_DEPTH = 90;
constexpr int FUTILITY_MARGIN_BASE = 40;

constexpr int SEE_CUTOFF = -200;

// --- LMR table ---

constexpr int LMR_MAX_MOVES = 128;

/** Pre-computed late move reduction table; reduction = f(depth, move_index). */
const std::array<std::array<int, LMR_MAX_MOVES>, MAX_SEARCH_PLY> LMR_TABLE = []() {
    constexpr double LMR_TUNING_CONSTANT = 2.0;
    std::array<std::array<int, LMR_MAX_MOVES>, MAX_SEARCH_PLY> table{};
    for (int depth = 0; depth < MAX_SEARCH_PLY; depth++) {
        for (int move_index = 0; move_index < LMR_MAX_MOVES; move_index++) {
            table[depth][move_index] = std::log(depth + 1) * std::log(move_index + 1) / LMR_TUNING_CONSTANT;
        }
    }
    return table;
}();

// --- MVV/LVA table ---

/** Most-Valuable-Victim / Least-Valuable-Attacker scoring for capture ordering. */
constexpr std::array<std::array<MoveScore, NUM_PIECES>, NUM_PIECES> MVV_LVA_TABLE = {{
    {106, 206, 306, 406, 506, 0},
    {105, 205, 305, 405, 505, 0},
    {104, 204, 304, 404, 504, 0},
    {103, 203, 303, 403, 503, 0},
    {102, 202, 302, 402, 502, 0},
    {101, 201, 301, 401, 501, 0},
}};

constexpr int NUM_MOVE_FLAGS = 7;
constexpr std::array<MoveScore, NUM_MOVE_FLAGS> PROMOTION_BONUS = {
    0, 0, 0, 200, 300, 200, 600,
};

// --- SEE ---

/** Static Exchange Evaluation — estimate the material outcome of a capture sequence. */
int see(Board& board, Move move) {
    struct Attacker {
        Piece piece;
        Square square;
    };

    constexpr int MAX_CAPTURES = 32;
    constexpr std::array<int, NUM_PIECES> SEE_PIECE_VALUES = {100, 300, 325, 500, 900, 0};

    auto en_passant_capture_square = [](Square to, Side moving_side) {
        return moving_side == WHITE ? to + SOUTH : to + NORTH;
    };

    auto get_least_valuable_attacker = [&](Side side, Bitboard attackers) {
        for (Piece piece = PAWN; piece < NUM_PIECES; piece++) {
            Bitboard from_mask = attackers & board.pieces()[side][piece];
            if (from_mask) {
                return Attacker{piece, get_lsb(from_mask)};
            }
        }
        return Attacker{NO_PIECE, NO_SQUARE};
    };

    std::array<int, MAX_CAPTURES> exchange_scores;
    Square target_square = move.to();
    Side attacking_side = board.to_move();
    Bitboard occupied = board.occupied();
    Bitboard attackers = EMPTY_BITBOARD;
    Piece last_attacker_piece = NO_PIECE;
    int num_exchanges = 0;

    // Seed with the initial capture value
    exchange_scores[0] = move.flag() == MF_EN_PASSANT
        ? SEE_PIECE_VALUES[PAWN]
        : SEE_PIECE_VALUES[board.piece_map()[target_square]];
    last_attacker_piece = board.piece_map()[move.from()];

    // Remove the initial attacker from the occupied set to reveal x-ray attackers behind it
    occupied ^= get_mask(move.from());
    if (move.flag() == MF_EN_PASSANT) {
        Square capture_square = en_passant_capture_square(target_square, board.to_move());
        occupied ^= get_mask(capture_square);
        occupied |= get_mask(target_square);
    }
    attacking_side = opposite_side(attacking_side);
    num_exchanges++;

    // Build the exchange sequence: each side captures with its least valuable piece
    while ((attackers = board.attackers_to(target_square, occupied)) != EMPTY_BITBOARD) {
        Attacker attacker = get_least_valuable_attacker(attacking_side, attackers);

        if (attacker.piece == NO_PIECE) break;
        // King can't capture if the opponent still has defenders on the square
        if (attacker.piece == KING && (attackers & board.sides()[opposite_side(attacking_side)])) break;

        int captured_value = SEE_PIECE_VALUES[last_attacker_piece];
        exchange_scores[num_exchanges] = captured_value - exchange_scores[num_exchanges - 1];

        last_attacker_piece = attacker.piece;
        occupied ^= get_mask(attacker.square);
        attacking_side = opposite_side(attacking_side);
        num_exchanges++;
    }

    // Minimax backwards through the exchange list — each side can choose to stop capturing
    while (num_exchanges > 1) {
        num_exchanges--;
        int score_if_continue = exchange_scores[num_exchanges];
        int score_if_stop = -exchange_scores[num_exchanges - 1];
        exchange_scores[num_exchanges - 1] = -std::max(score_if_continue, score_if_stop);
    }

    return exchange_scores[0];
}

// --- Helpers ---

/** Detect 50-move rule or repetition draws without generating moves. */
bool is_engine_draw(const Board& b) {
    return b.halfmoves() >= FIFTY_MOVE_PLY_LIMIT || b.has_repeated();
}

/** Adjust mate scores before storing in TT — make them relative to the root. */
PositionScore normalize_tt_score(PositionScore score, int ply) {
    if (score >= CHECKMATE_SCORE - MAX_SEARCH_PLY) return score + ply;
    if (score <= -CHECKMATE_SCORE + MAX_SEARCH_PLY) return score - ply;
    return score;
}

/** Reverse the TT mate-score adjustment for the current search ply. */
PositionScore denormalize_tt_score(PositionScore score, int ply) {
    if (score >= CHECKMATE_SCORE - MAX_SEARCH_PLY) return score - ply;
    if (score <= -CHECKMATE_SCORE + MAX_SEARCH_PLY) return score + ply;
    return score;
}

/** Guard conditions for null move pruning — avoid zugzwang-prone endgames. */
bool can_apply_null_move(
    bool in_check,
    SearchDepth depth,
    bool is_pv_node,
    bool allow_null_move,
    bool has_non_pawn_material
) {
    return (
        allow_null_move &&
        !in_check &&
        depth >= 3 &&
        !is_pv_node &&
        has_non_pawn_material
    );
}

/** Guard conditions for futility pruning — only safe far from mate and at low depth. */
bool can_apply_futility(
    PositionScore alpha,
    PositionScore beta,
    bool is_pv_node,
    bool in_check,
    SearchDepth depth
) {
    constexpr PositionScore MATE_THRESHOLD = CHECKMATE_SCORE - MAX_SEARCH_PLY;
    return (
        std::abs(alpha) < MATE_THRESHOLD &&
        std::abs(beta) < MATE_THRESHOLD &&
        !is_pv_node &&
        !in_check &&
        depth < 4
    );
}

} // namespace

// --- Context ---

int Engine::Context::search_ply(int board_ply) const {
    return board_ply - ply_offset;
}

bool Engine::Context::has_runtime_limits() const {
    return soft_time != -1 || hard_time != -1 || max_nodes > 0;
}

void Engine::Context::set_deadlines_from(std::chrono::steady_clock::time_point now) {
    soft_deadline = std::chrono::steady_clock::time_point::max();
    hard_deadline = std::chrono::steady_clock::time_point::max();
    if (soft_time != -1) soft_deadline = now + std::chrono::milliseconds(soft_time);
    if (hard_time != -1) hard_deadline = now + std::chrono::milliseconds(hard_time);
}

uint64_t Engine::Context::elapsed_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - search_start
    ).count();
}

void Engine::Context::reset(int board_ply) {
    nodes = 0;
    ply_offset = board_ply;
    search_interrupted = false;
    killer_1.fill(NULL_MOVE);
    killer_2.fill(NULL_MOVE);
    side_piece_to_history = {};
    from_to_history = {};
}

// --- MoveSelector ---

/**
 * Staged move generator that yields moves in order of likely quality:
 * prev_best -> TT move -> winning tacticals -> killers -> quiets -> losing captures.
 */
class Engine::MoveSelector {
public:
    MoveSelector(Board& board, Move tt_move, Move prev_best_move = NULL_MOVE)
        : phase_(MSP_PREV_BEST), board_(board), move_generator_(board), tt_move_(tt_move), prev_best_move_(prev_best_move) {}

    Move next_move(Context& ctx);
    bool before_quiet_phase() const { return phase_ <= MSP_KILLER; }
    bool in_quiet_phase() const { return phase_ == MSP_QUIET; }

private:
    MoveSelectorPhase phase_;
    Board& board_;
    MoveGenerator move_generator_;
    MoveList tactical_moves_;
    MoveList bad_captures_;
    MoveList quiet_moves_;
    Move prev_best_move_;
    Move tt_move_;
    Move returned_killer_1_;
    Move returned_killer_2_;
    bool tacticals_generated_ = false;
    bool quiets_generated_ = false;
    bool bad_captures_sorted_ = false;

    void generate_tactical_moves();
    void generate_quiet_moves(Context& ctx);
    MoveScore get_tactical_score(Move move);
    void sort_tactical_moves(MoveList& moves);
    void sort_quiet_moves(Context& ctx);
    Move pop_next(MoveList& moves);
    bool is_already_returned(Move move);
};

/** Return the next move in priority order, or NULL_MOVE when exhausted. */
Move Engine::MoveSelector::next_move(Context& ctx) {
    switch (phase_) {
        // Phase 1: previous iteration's best move (root only)
        case MSP_PREV_BEST: {
            if (prev_best_move_ != NULL_MOVE) {
                phase_ = MSP_TT;
                return prev_best_move_;
            }
            phase_ = MSP_TT;
            [[fallthrough]];
        }
        // Phase 2: transposition table move
        case MSP_TT: {
            if (tt_move_ != NULL_MOVE && tt_move_ != prev_best_move_ && board_.is_legal_move(tt_move_)) {
                phase_ = MSP_TACTICAL;
                return tt_move_;
            }
            phase_ = MSP_TACTICAL;
            [[fallthrough]];
        }
        // Phase 3: winning/equal captures and promotions (SEE >= 0)
        case MSP_TACTICAL: {
            if (!tacticals_generated_) generate_tactical_moves();
            Move next_tactical = pop_next(tactical_moves_);
            if (next_tactical != NULL_MOVE) return next_tactical;
            phase_ = MSP_KILLER;
            [[fallthrough]];
        }
        // Phase 4: killer moves — quiet moves that caused a beta cutoff at this ply
        case MSP_KILLER: {
            int ply = ctx.search_ply(board_.ply());
            Move killer_move_1 = ctx.killer_1[ply];
            Move killer_move_2 = ctx.killer_2[ply];

            if (
                killer_move_1 != NULL_MOVE &&
                returned_killer_1_ == NULL_MOVE &&
                !is_already_returned(killer_move_1) &&
                board_.is_legal_move(killer_move_1)
            ) {
                returned_killer_1_ = killer_move_1;
                return killer_move_1;
            }

            if (
                killer_move_2 != NULL_MOVE &&
                returned_killer_2_ == NULL_MOVE &&
                !is_already_returned(killer_move_2) &&
                board_.is_legal_move(killer_move_2)
            ) {
                returned_killer_2_ = killer_move_2;
                return killer_move_2;
            }

            phase_ = MSP_QUIET;
            [[fallthrough]];
        }
        // Phase 5: remaining quiet moves, ordered by history heuristic
        case MSP_QUIET: {
            if (!quiets_generated_) generate_quiet_moves(ctx);
            Move next_quiet = pop_next(quiet_moves_);
            if (next_quiet != NULL_MOVE) return next_quiet;
            phase_ = MSP_BAD_CAPTURE;
            [[fallthrough]];
        }
        // Phase 6: losing captures (SEE < 0), tried last
        case MSP_BAD_CAPTURE: {
            if (!bad_captures_sorted_) {
                sort_tactical_moves(bad_captures_);
                bad_captures_sorted_ = true;
            }
            Move next_bad = pop_next(bad_captures_);
            if (next_bad != NULL_MOVE) return next_bad;
            return NULL_MOVE;
        }
        default:
            return NULL_MOVE;
    }
}

/** Split tacticals into winning/equal (SEE >= 0) and losing (SEE < 0) captures. */
void Engine::MoveSelector::generate_tactical_moves() {
    MoveList all_tacticals = move_generator_.generate_tacticals();

    for (const Move move : all_tacticals) {
        if (move.type() == MT_CAPTURE && see(board_, move) < 0) {
            bad_captures_.add(move);
        } else {
            tactical_moves_.add(move);
        }
    }

    sort_tactical_moves(tactical_moves_);
    tacticals_generated_ = true;
}

void Engine::MoveSelector::generate_quiet_moves(Context& ctx) {
    quiet_moves_ = move_generator_.generate_quiets();
    sort_quiet_moves(ctx);
    quiets_generated_ = true;
}

MoveScore Engine::MoveSelector::get_tactical_score(Move move) {
    MoveScore score = 0;
    if (move.type() == MT_CAPTURE) {
        Piece attacker = board_.piece_map()[move.from()];
        Piece victim = move.flag() == MF_EN_PASSANT ? PAWN : board_.piece_map()[move.to()];
        score += MVV_LVA_TABLE[attacker][victim];
    }
    if (move.is_promotion()) {
        score += PROMOTION_BONUS[move.flag()];
    }
    return score;
}

void Engine::MoveSelector::sort_tactical_moves(MoveList& moves) {
    std::sort(moves.begin(), moves.end(), [&](Move move_1, Move move_2) {
        return get_tactical_score(move_1) < get_tactical_score(move_2);
    });
}

/** Order quiets by combined side-piece-to and from-to history scores. */
void Engine::MoveSelector::sort_quiet_moves(Context& ctx) {
    std::sort(quiet_moves_.begin(), quiet_moves_.end(), [this, &ctx](Move move_1, Move move_2) {
        Square move_1_from = move_1.from();
        Square move_1_to = move_1.to();
        Piece move_1_piece = board_.piece_map()[move_1_from];
        MoveScore move_1_score = ctx.side_piece_to_history[board_.to_move()][move_1_piece][move_1_to] +
            ctx.from_to_history[move_1_from][move_1_to];

        Square move_2_from = move_2.from();
        Square move_2_to = move_2.to();
        Piece move_2_piece = board_.piece_map()[move_2_from];
        MoveScore move_2_score = ctx.side_piece_to_history[board_.to_move()][move_2_piece][move_2_to] +
            ctx.from_to_history[move_2_from][move_2_to];

        return move_1_score < move_2_score;
    });
}

Move Engine::MoveSelector::pop_next(MoveList& moves) {
    Move next = moves.pop();
    while (next != NULL_MOVE && is_already_returned(next)) {
        next = moves.pop();
    }
    return next;
}

bool Engine::MoveSelector::is_already_returned(Move move) {
    return move == prev_best_move_ || move == tt_move_ || move == returned_killer_1_ || move == returned_killer_2_;
}

// --- Engine API ---

Engine::~Engine() {
    stop();
}

void Engine::set_threads(int n) {
    num_threads_ = std::clamp(n, MIN_THREADS, MAX_THREADS);
}

void Engine::set_use_opening_book(bool enabled) {
    use_opening_book_ = enabled;
}

void Engine::stop() {
    external_stop_ = true;
    finish();
}

void Engine::clear() {
    stop();
    g_tt.clear();
    g_pawn_table.clear();
}

uint64_t Engine::total_nodes() const {
    uint64_t total = 0;
    for (const auto& ctx : contexts_) {
        total += ctx.nodes;
    }
    return total;
}

void Engine::search_depth(const Board& board, SearchDepth depth) {
    search(board, depth, -1, -1, 0);
}

void Engine::search_time(const Board& board, int soft_time, int hard_time) {
    search(board, MAX_SEARCH_PLY - 1, soft_time, hard_time, 0);
}

void Engine::search_nodes(const Board& board, uint64_t max_nodes) {
    search(board, MAX_SEARCH_PLY - 1, -1, -1, max_nodes);
}

void Engine::search_infinite(const Board& board) {
    search(board, MAX_SEARCH_PLY - 1, -1, -1, 0);
}

void Engine::apply_time(int soft_time, int hard_time) {
    if (contexts_.empty()) return;
    Context& ctx = contexts_[0];
    ctx.soft_time = soft_time;
    ctx.hard_time = hard_time;
    ctx.set_deadlines_from(std::chrono::steady_clock::now());
}

Move Engine::sync_search(Board& board, SearchDepth max_depth, int soft_time, int hard_time, uint64_t max_nodes) {
    search(board, max_depth, soft_time, hard_time, max_nodes);
    return finish();
}

Move Engine::sync_search_depth(Board& board, SearchDepth depth) {
    return sync_search(board, depth, -1, -1, 0);
}

Move Engine::sync_search_time(Board& board, int soft_time, int hard_time) {
    return sync_search(board, MAX_SEARCH_PLY - 1, soft_time, hard_time, 0);
}

Move Engine::sync_search_nodes(Board& board, uint64_t max_nodes) {
    return sync_search(board, MAX_SEARCH_PLY - 1, -1, -1, max_nodes);
}

// --- Threading ---

void Engine::search(const Board& board, SearchDepth max_depth, int soft_time, int hard_time, uint64_t max_nodes) {
    stop();
    reset();

    best_move_ = find_book_move(board);
    if (best_move_ != NULL_MOVE) {
        Board emit_board = board;
        emit_best_move(emit_board);
        return;
    }

    contexts_.resize(num_threads_);
    for (int i = 0; i < num_threads_; i++) {
        contexts_[i] = Context{};
        contexts_[i].is_main_thread = i == 0;
    }

    // Only the main thread respects search limits; helpers search until signalled
    Context& main_ctx = contexts_[0];
    main_ctx.max_depth = max_depth;
    main_ctx.soft_time = soft_time;
    main_ctx.hard_time = hard_time;
    main_ctx.max_nodes = max_nodes;

    for (int i = 0; i < num_threads_; i++) {
        // Stagger start depths across threads for better lazy SMP diversity
        SearchDepth start_depth = 1 + i % 2;
        Board thread_board = board;
        auto worker = [this, i, b = std::move(thread_board), start_depth]() mutable {
            Move move = iterative_deepening(b, contexts_[i], start_depth);
            if (contexts_[i].is_main_thread) {
                best_move_ = move;
                emit_best_move(b);
            }
        };

        if (i == 0) {
            main_thread_ = std::thread(std::move(worker));
        } else {
            helper_threads_.emplace_back(std::move(worker));
        }
    }
}

Move Engine::finish() {
    if (main_thread_.joinable()) {
        main_thread_.join();
    }

    for (auto& thread : helper_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    helper_threads_.clear();
    external_stop_ = false;
    main_finished_ = false;
    return best_move_;
}

void Engine::reset() {
    best_move_ = NULL_MOVE;
    contexts_.clear();
    external_stop_ = false;
    main_finished_ = false;
    g_tt.increment_generation();
}

Move Engine::find_book_move(const Board& board) {
    if (!use_opening_book_) {
        return NULL_MOVE;
    }

    Move book_move = opening_book_.pick_move(board);
    if (book_move == NULL_MOVE) {
        return NULL_MOVE;
    }

    Board root = board;
    return root.is_legal_move(book_move) ? book_move : NULL_MOVE;
}

bool Engine::should_stop_search(Context& ctx) {
    if (ctx.search_interrupted || external_stop_ || (!ctx.is_main_thread && main_finished_)) {
        return true;
    }

    // Amortise the cost of clock reads by only checking every 2048 nodes
    if ((ctx.nodes & TIME_CHECK_PERIOD_MASK) != 0) {
        return false;
    }

    if (!ctx.is_main_thread || !ctx.has_runtime_limits()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();

    if (ctx.hard_time != -1 && now >= ctx.hard_deadline) {
        ctx.search_interrupted = true;
        external_stop_ = true;
        return true;
    }

    if (ctx.max_nodes > 0 && ctx.nodes >= ctx.max_nodes) {
        ctx.search_interrupted = true;
        external_stop_ = true;
        return true;
    }

    return false;
}

void Engine::signal_stop(const Context& ctx) {
    if (ctx.is_main_thread) {
        main_finished_ = true;
    }
}

// --- UCI output ---

void Engine::emit_search_info(const Context& ctx, SearchDepth depth, PositionScore score) {
    if (!ctx.is_main_thread) {
        return;
    }

    uint64_t elapsed_ms = ctx.elapsed_ms();
    uint64_t nodes = total_nodes();

    uint64_t nps = elapsed_ms > 0 ? (nodes * 1000) / elapsed_ms : 0;
    bool is_mate = std::abs(score) > CHECKMATE_SCORE - MAX_SEARCH_PLY;
    int plies_to_mate = CHECKMATE_SCORE - std::abs(score);
    int mate_in = (plies_to_mate + 1) / 2 * (score > 0 ? 1 : -1);
    std::string score_str = is_mate ? "mate " + std::to_string(mate_in) : "cp " + std::to_string(score);

    uci_print(
        "info"
        " depth " + std::to_string(depth) +
        " score " + score_str +
        " nodes " + std::to_string(nodes) +
        " nps " + std::to_string(nps) +
        " time " + std::to_string(elapsed_ms)
    );
}

/** Print UCI bestmove with a ponder move if one exists in the TT. */
void Engine::emit_best_move(Board& board) {
    std::string ponder_str;
    if (best_move_ != NULL_MOVE) {
        // Make the best move on the board to look up the opponent's expected reply in the TT
        board.make_move(best_move_);
        TTEntry* tt_entry = g_tt.get_entry(board.position_hash());
        if (tt_entry && tt_entry->move() != NULL_MOVE && board.is_legal_move(tt_entry->move())) {
            ponder_str = " ponder " + decode_move_to_uci(tt_entry->move());
        }
        board.unmake_move(best_move_);
    }

    uci_print("bestmove " + decode_move_to_uci(best_move_) + ponder_str);
}

// --- Search ---

struct Engine::SearchResult {
    Move best_move;
    PositionScore score;
};

struct Engine::TTProbeResult {
    Move tt_move;
    bool has_cutoff;
    PositionScore cutoff_score;
};

void Engine::store_tt_result(
    const Board& b,
    Context& ctx,
    Move best_move,
    SearchDepth depth,
    PositionScore best_score,
    PositionScore original_alpha,
    PositionScore beta
) {
    // Determine the bound type from how the score relates to the original window
    TTNode tt_node;
    if (best_score >= beta)                tt_node = TT_FAIL_HIGH;
    else if (best_score <= original_alpha) tt_node = TT_FAIL_LOW;
    else                                   tt_node = TT_EXACT;

    PositionScore tt_score = normalize_tt_score(best_score, ctx.search_ply(b.ply()));
    g_tt.add_entry(TTEntry{b.position_hash(), best_move, depth, tt_score, tt_node});
}

Engine::TTProbeResult Engine::probe_tt(
    Board& board,
    Context& ctx,
    SearchDepth depth,
    PositionScore alpha,
    PositionScore beta,
    bool is_pv_node
) {
    TTEntry* tt_entry = g_tt.get_entry(board.position_hash());

    if (tt_entry) {
        PositionScore tt_score = denormalize_tt_score(tt_entry->score(), ctx.search_ply(board.ply()));
        Move tt_move = tt_entry->move();

        // Use the TT score as a cutoff if the entry is deep enough and bounds match
        if (
            tt_entry->depth() >= depth &&
            (
                tt_entry->node() == TT_EXACT ||
                (tt_entry->node() == TT_FAIL_HIGH && tt_score >= beta) ||
                (tt_entry->node() == TT_FAIL_LOW && tt_score <= alpha)
            )
        ) {
            return {tt_move, true, tt_score};
        }

        // No cutoff, but still use the TT move for move ordering
        return {tt_move, false, 0};
    }

    // Internal Iterative Deepening: do a shallow search to find a move for ordering
    // when we have no TT entry on a PV node at sufficient depth
    constexpr SearchDepth MINIMUM_IID_DEPTH = 4;
    if (is_pv_node && depth >= MINIMUM_IID_DEPTH) {
        SearchDepth iid_depth = std::max<SearchDepth>(0, depth / 2);
        negamax(board, ctx, iid_depth, alpha, beta);

        tt_entry = g_tt.get_entry(board.position_hash());
        if (tt_entry) {
            return {tt_entry->move(), false, 0};
        }
    }

    return {NULL_MOVE, false, 0};
}

void Engine::update_killer_table(Context& ctx, Move move, int ply) {
    if (move != ctx.killer_1[ply]) {
        ctx.killer_2[ply] = ctx.killer_1[ply];
        ctx.killer_1[ply] = move;
    }
}

void Engine::handle_beta_cutoff(
    Board& b,
    Context& ctx,
    Move cutoff_move,
    SearchDepth depth,
    MoveList& searched_quiet_moves
) {
    // Only update quiet-move heuristics; captures have their own ordering (MVV/LVA + SEE)
    if (cutoff_move.type() != MT_QUIET) return;

    // History gravity: blend the bonus toward the current score so values stay bounded.
    // Formula: score += bonus - score * |bonus| / MAX, which asymptotically saturates.
    auto update_history_tables = [&](Move move, MoveScore bonus) {
        MoveScore clamped_bonus = std::clamp(bonus, MIN_MOVE_SCORE, MAX_MOVE_SCORE);
        Piece moving_piece = b.piece_map()[move.from()];

        MoveScore& side_piece_to_history_score = ctx.side_piece_to_history[b.to_move()][moving_piece][move.to()];
        side_piece_to_history_score += clamped_bonus - side_piece_to_history_score * std::abs(clamped_bonus) / MAX_MOVE_SCORE;

        MoveScore& from_to_history_score = ctx.from_to_history[move.from()][move.to()];
        from_to_history_score += clamped_bonus - from_to_history_score * std::abs(clamped_bonus) / MAX_MOVE_SCORE;
    };

    // Remove the cutoff move itself from the quiet list before applying malus
    searched_quiet_moves.pop();

    int ply = ctx.search_ply(b.ply());
    MoveScore bonus = depth * depth;

    update_killer_table(ctx, cutoff_move, ply);
    update_history_tables(cutoff_move, bonus);

    // Penalise quiet moves that were tried before the cutoff move (they failed)
    MoveScore malus = -(bonus / 2);
    for (const Move move : searched_quiet_moves) {
        update_history_tables(move, malus);
    }
}

PositionScore Engine::quiescence_search(Board& board, Context& ctx, PositionScore alpha, PositionScore beta) {
    ctx.nodes++;

    if (should_stop_search(ctx)) {
        ctx.search_interrupted = true;
        return SEARCH_INTERRUPTED;
    }

    if (is_engine_draw(board)) {
        return STALEMATE_SCORE;
    }

    bool in_check = board.in_check();

    // Standing pat: if not in check, use static eval as a lower bound
    if (!in_check) {
        PositionScore static_eval = evaluate(board);
        alpha = std::max(alpha, static_eval);
        if (alpha >= beta) {
            return alpha;
        }
    }

    // In check we must search all moves (evasions); otherwise only captures/promotions
    MoveGenerator qmg(board);
    MoveList moves = in_check ? qmg.generate_all() : qmg.generate_tacticals();

    if (moves.is_empty()) {
        if (in_check) {
            return -CHECKMATE_SCORE + ctx.search_ply(board.ply());
        }
        return alpha;
    }

    for (Move move : moves) {
        // SEE pruning: skip captures that lose too much material
        if (!in_check && move.type() == MT_CAPTURE && see(board, move) < SEE_CUTOFF) continue;

        board.make_move(move);
        PositionScore score = -quiescence_search(board, ctx, -beta, -alpha);
        board.unmake_move(move);

        if (should_stop_search(ctx)) {
            return SEARCH_INTERRUPTED;
        }

        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }

    return alpha;
}

PositionScore Engine::negamax(
    Board& board,
    Context& ctx,
    SearchDepth depth,
    PositionScore alpha,
    PositionScore beta,
    bool allow_null_move
) {
    ctx.nodes++;

    if (should_stop_search(ctx)) {
        ctx.search_interrupted = true;
        return SEARCH_INTERRUPTED;
    }

    if (is_engine_draw(board)) {
        return STALEMATE_SCORE;
    }

    if (depth == 0) {
        return quiescence_search(board, ctx, alpha, beta);
    }

    bool is_pv_node = beta - alpha > 1;

    // TT probe — may produce an immediate cutoff or a best-move hint
    TTProbeResult tt_result = probe_tt(board, ctx, depth, alpha, beta, is_pv_node);
    if (tt_result.has_cutoff) return tt_result.cutoff_score;

    PositionScore original_alpha = alpha;
    bool in_check = board.in_check();

    // Null move pruning: skip a turn and search with a reduced window.
    // If the opponent still can't beat beta, the position is likely so good we can prune.
    if (can_apply_null_move(in_check, depth, is_pv_node, allow_null_move, board.has_non_pawn_material(board.to_move()))) {
        int reduction = NULL_MOVE_BASE_REDUCTION + (depth >= NULL_MOVE_DEEPER_THRESHOLD);
        board.make_null_move();
        PositionScore score = -negamax(board, ctx, depth - reduction, -beta, -beta + 1, false);
        board.unmake_null_move();

        if (should_stop_search(ctx)) {
            return SEARCH_INTERRUPTED;
        }

        if (score >= beta) return score;
    }

    // Futility pruning setup: at shallow depths, if static eval is far below alpha,
    // quiet moves are unlikely to raise it enough — skip them in the move loop below.
    bool can_use_futility_pruning = can_apply_futility(alpha, beta, is_pv_node, in_check, depth);

    PositionScore static_eval = 0;
    if (can_use_futility_pruning) {
        static_eval = evaluate(board);
    }
    PositionScore futility_margin = FUTILITY_MARGIN_PER_DEPTH * depth + FUTILITY_MARGIN_BASE;

    PositionScore best_score = DUMMY_SCORE;
    Move best_move;
    MoveList searched_quiet_moves;

    MoveSelector move_selector(board, tt_result.tt_move);
    bool has_moves = false;
    bool is_first_move = true;
    int num_moves = 0;

    while (true) {
        Move move = move_selector.next_move(ctx);
        if (move == NULL_MOVE) break;
        else has_moves = true;

        // Futility pruning: skip quiet moves when static eval + margin can't reach alpha
        if (can_use_futility_pruning && num_moves > 0 && move_selector.in_quiet_phase()) {
            if (static_eval + futility_margin < alpha) continue;
        }

        board.make_move(move);
        num_moves++;

        // Late Move Reductions: search later moves at reduced depth since
        // good move ordering means they're unlikely to be best
        int reduction = LMR_TABLE[depth][num_moves];
        if (is_pv_node) reduction -= 1;   // be less aggressive on PV nodes
        if (in_check) reduction = 0;       // don't reduce when in check
        if (move_selector.before_quiet_phase()) reduction = 0;  // don't reduce tacticals/killers

        // Check extension: extend by one ply when the move gives check
        int extension = board.in_check() ? 1 : 0;

        reduction = std::clamp(reduction, 0, depth - 1);

        // Principal Variation Search
        PositionScore score;
        if (is_first_move) {
            // First move: search with full window
            score = -negamax(board, ctx, depth - 1 + extension, -beta, -alpha);
            is_first_move = false;
        } else {
            // Null-window search with LMR
            score = -negamax(board, ctx, depth - 1 - reduction + extension, -alpha - 1, -alpha);

            // Re-search at full depth if LMR reduced search beat alpha
            if (score > alpha && reduction > 0) {
                score = -negamax(board, ctx, depth - 1 + extension, -alpha - 1, -alpha);
            }

            // Full-window re-search if null-window search found a better move
            if (score > alpha && score < beta) {
                score = -negamax(board, ctx, depth - 1 + extension, -beta, -alpha);
            }
        }

        board.unmake_move(move);

        if (should_stop_search(ctx)) {
            return SEARCH_INTERRUPTED;
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (move.type() == MT_QUIET) {
            searched_quiet_moves.add(move);
        }

        if (alpha >= beta) {
            handle_beta_cutoff(board, ctx, move, depth, searched_quiet_moves);
            break;
        }
    }

    if (!has_moves) {
        if (board.in_check()) {
            return -CHECKMATE_SCORE + ctx.search_ply(board.ply());
        } else {
            return STALEMATE_SCORE;
        }
    }

    store_tt_result(board, ctx, best_move, depth, best_score, original_alpha, beta);

    return best_score;
}

Engine::SearchResult Engine::search_at_depth(
    Board& board,
    Context& ctx,
    SearchDepth depth,
    Move prev_best_move,
    PositionScore alpha,
    PositionScore beta
) {
    Move best_move;
    PositionScore best_score = DUMMY_SCORE;
    MoveList searched_quiet_moves;
    TTEntry* tt_entry = g_tt.get_entry(board.position_hash());
    Move tt_move = tt_entry ? tt_entry->move() : NULL_MOVE;
    MoveSelector move_selector(board, tt_move, prev_best_move);
    bool is_first_move = true;

    while (true) {
        Move move = move_selector.next_move(ctx);
        if (move == NULL_MOVE) break;

        board.make_move(move);

        // PVS at the root: full window for the first move, null window for the rest
        PositionScore score;
        if (is_first_move) {
            score = -negamax(board, ctx, depth - 1, -beta, -alpha);
            is_first_move = false;
        } else {
            score = -negamax(board, ctx, depth - 1, -alpha - 1, -alpha);
            if (score > alpha && score < beta) {
                score = -negamax(board, ctx, depth - 1, -beta, -alpha);
            }
        }

        board.unmake_move(move);

        if (should_stop_search(ctx)) {
            return {NULL_MOVE, 0};
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (move.type() == MT_QUIET) {
            searched_quiet_moves.add(move);
        }

        if (alpha >= beta) {
            handle_beta_cutoff(board, ctx, move, depth, searched_quiet_moves);
            break;
        }
    }

    return {best_move, best_score};
}

Move Engine::iterative_deepening(Board& board, Context& ctx, SearchDepth depth) {
    MoveGenerator move_generator(board);
    MoveList moves = move_generator.generate_all();
    if (moves.is_empty()) {
        signal_stop(ctx);
        return NULL_MOVE;
    }

    Move prev_best_move;
    Move best_move;
    int best_move_stability = 0;  // how many consecutive iterations picked the same move

    PositionScore prev_score = 0;
    PositionScore score = 0;
    ctx.reset(board.ply());
    ctx.search_start = std::chrono::steady_clock::now();
    ctx.set_deadlines_from(ctx.search_start);

    while (true) {
        if (should_stop_search(ctx)) {
            break;
        }

        if (ctx.is_main_thread && depth > ctx.max_depth) {
            break;
        }

        // Soft time management: stop after the soft deadline unless the score dropped
        // significantly or the best move keeps changing (unstable search).
        if (ctx.is_main_thread && ctx.has_runtime_limits() && ctx.soft_time != -1) {
            bool soft_limit_hit = std::chrono::steady_clock::now() >= ctx.soft_deadline;
            if (soft_limit_hit) {
                bool score_dropped = (prev_score - score) > SCORE_DROP_THRESHOLD;
                if (!score_dropped && best_move_stability > 0) {
                    break;
                }
            }
        }

        // Aspiration windows: start with a narrow window around the previous score.
        // Use full window at depth 1 since we have no prior score to centre on.
        int alpha;
        int beta;
        int alpha_delta = ASPIRATION_WINDOW;
        int beta_delta = ASPIRATION_WINDOW;
        if (depth == 1) {
            alpha = -CHECKMATE_SCORE;
            beta = CHECKMATE_SCORE;
        } else {
            alpha = std::max(score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            beta = std::min(score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
        }

        // Re-search with exponentially wider windows on fail-high/fail-low
        SearchResult search_result;
        while (true) {
            if (should_stop_search(ctx)) {
                break;
            }

                search_result = search_at_depth(board, ctx, depth, best_move, alpha, beta);
            if (search_result.score <= alpha) {
                alpha_delta *= 2;
                alpha = std::max(score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            } else if (search_result.score >= beta) {
                beta_delta *= 2;
                beta = std::min(score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
            } else {
                break;
            }
        }

        if (should_stop_search(ctx)) {
            break;
        }

        prev_score = score;
        score = search_result.score;
        if (search_result.best_move != NULL_MOVE) {
            prev_best_move = best_move;
            best_move = search_result.best_move;
        }

        // Track best-move stability for soft time management decision
        if (best_move == prev_best_move) {
            best_move_stability++;
        } else {
            best_move_stability = 0;
        }

        emit_search_info(ctx, depth, score);

        depth++;
    }

    signal_stop(ctx);
    // Fall back to the first legal move if search was interrupted before completing depth 1
    return best_move == NULL_MOVE && !moves.is_empty() ? moves[0] : best_move;
}
