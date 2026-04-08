#include "engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

#include "bitboard.hpp"
#include "params.hpp"
#include "move_generator.hpp"
#include "print.hpp"
#include "notation.hpp"
#include "time_manager.hpp"

// --- LMR table ---

constexpr int LMR_MAX_MOVES = 128;

/** Pre-computed late move reduction table; reduction = f(depth, move_index). */
static std::array<std::array<int, LMR_MAX_MOVES>, MAX_SEARCH_PLY> LMR_TABLE{};

void build_lmr_table() {
    for (int depth = 0; depth < MAX_SEARCH_PLY; depth++) {
        for (int move_index = 0; move_index < LMR_MAX_MOVES; move_index++) {
            LMR_TABLE[depth][move_index] =
                std::log(depth + 1) * std::log(move_index + 1) / prm.lmr_tuning_constant;
        }
    }
}

namespace {

// --- Constants ---

constexpr PositionScore DUMMY_SCORE = -32'700;
constexpr PositionScore SEARCH_INTERRUPTED = DUMMY_SCORE;

// Only check the clock every N nodes (N = mask + 1) to avoid syscall overhead
constexpr uint64_t TIME_CHECK_PERIOD_MASK = 2047;

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

/** Score a tactical move by MVV-LVA + promotion bonus. */
MoveScore get_tactical_score(const Board& board, Move move) {
    MoveScore score = 0;
    if (move.type() == MT_CAPTURE) {
        Piece attacker = board.piece_map()[move.from()];
        Piece victim = move.flag() == MF_EN_PASSANT ? PAWN : board.piece_map()[move.to()];
        score += MVV_LVA_TABLE[attacker][victim];
    }
    if (move.is_promotion()) {
        score += PROMOTION_BONUS[move.flag()];
    }
    return score;
}


// --- SEE ---

struct Attacker {
    Piece piece;
    Square square;
};

constexpr int MAX_CAPTURES = 32;
constexpr std::array<int, NUM_PIECES> SEE_PIECE_VALUES = {100, 300, 325, 500, 900, 0};

/** Static Exchange Evaluation — estimate the material outcome of a capture sequence. */
int see(Board& board, Move move) {
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

    // Each side captures with its least valuable piece until one side runs out
    while ((attackers = board.attackers_to(target_square, occupied)) != EMPTY_BITBOARD) {
        Attacker attacker = get_least_valuable_attacker(attacking_side, attackers);

        if (attacker.piece == NO_PIECE) {
            break;
        }
        // King can't capture if the opponent still has defenders on the square
        if (attacker.piece == KING && (attackers & board.sides()[opposite_side(attacking_side)])) {
            break;
        }

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
    if (score >= CHECKMATE_SCORE - MAX_SEARCH_PLY) {
        return score + ply;
    }
    if (score <= -CHECKMATE_SCORE + MAX_SEARCH_PLY) {
        return score - ply;
    }
    return score;
}

/** Reverse the TT mate-score adjustment for the current search ply. */
PositionScore denormalize_tt_score(PositionScore score, int ply) {
    if (score >= CHECKMATE_SCORE - MAX_SEARCH_PLY) {
        return score - ply;
    }
    if (score <= -CHECKMATE_SCORE + MAX_SEARCH_PLY) {
        return score + ply;
    }
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
        depth >= prm.null_move_min_depth &&
        !is_pv_node &&
        has_non_pawn_material
    );
}

/** Guard conditions for shallow-depth pruning (futility, RFP, LMP, etc.) */
bool can_apply_pruning(
    PositionScore alpha,
    PositionScore beta,
    bool is_pv_node,
    bool in_check,
    SearchDepth depth,
    SearchDepth max_depth
) {
    constexpr PositionScore MATE_THRESHOLD = CHECKMATE_SCORE - MAX_SEARCH_PLY;
    return (
        std::abs(alpha) < MATE_THRESHOLD &&
        std::abs(beta) < MATE_THRESHOLD &&
        !is_pv_node &&
        !in_check &&
        depth <= max_depth
    );
}

/** Return true if a quiet move attacks the enemy king after the occupancy update. */
bool quiet_move_gives_check(const Board& board, Move move) {
    Side moving_side = board.to_move();
    Square enemy_king = board.king_square(opposite_side(moving_side));

    std::array<Bitboard, NUM_PIECES> our_pieces = board.pieces()[moving_side];
    Bitboard occupied = board.occupied();

    Square from = move.from();
    Square to = move.to();
    Piece moving_piece = board.piece_map()[from];
    Bitboard from_mask = get_mask(from);
    Bitboard to_mask = get_mask(to);

    occupied = (occupied & ~from_mask) | to_mask;
    our_pieces[moving_piece] = (our_pieces[moving_piece] & ~from_mask) | to_mask;

    if (move.flag() == MF_CASTLE) {
        Square rook_from = NO_SQUARE;
        Square rook_to = NO_SQUARE;
        switch (to) {
            case C1: rook_from = A1; rook_to = D1; break;
            case G1: rook_from = H1; rook_to = F1; break;
            case C8: rook_from = A8; rook_to = D8; break;
            case G8: rook_from = H8; rook_to = F8; break;
        }

        Bitboard rook_from_mask = get_mask(rook_from);
        Bitboard rook_to_mask = get_mask(rook_to);
        occupied &= ~rook_from_mask;
        occupied |= rook_to_mask;
        our_pieces[ROOK] &= ~rook_from_mask;
        our_pieces[ROOK] |= rook_to_mask;
    }

    return (
        (get_pawn_attacks(moving_side, enemy_king) & our_pieces[PAWN]) |
        (get_piece_attacks(KNIGHT, enemy_king, occupied) & our_pieces[KNIGHT]) |
        (get_piece_attacks(KING, enemy_king, occupied) & our_pieces[KING]) |
        (get_piece_attacks(ROOK, enemy_king, occupied) & (our_pieces[ROOK] | our_pieces[QUEEN])) |
        (get_piece_attacks(BISHOP, enemy_king, occupied) & (our_pieces[BISHOP] | our_pieces[QUEEN]))
    );
}

} // namespace

// --- Context ---

int Engine::Context::search_ply(int board_ply) const {
    return board_ply - ply_offset;
}

Move Engine::Context::get_countermove(const Board& board) const {
    Move prev = board.previous_move();
    if (prev == NULL_MOVE) {
        return NULL_MOVE;
    }

    Square to = prev.to();
    Piece prev_piece = board.piece_map()[to];
    return countermoves[prev_piece][to];
}

bool Engine::Context::has_runtime_limits() const {
    return max_time != -1 || max_nodes > 0;
}

void Engine::Context::set_deadlines_from(std::chrono::steady_clock::time_point now) {
    soft_deadline = std::chrono::steady_clock::time_point::max();
    hard_deadline = std::chrono::steady_clock::time_point::max();
    if (max_time != -1) {
        hard_deadline = now + std::chrono::milliseconds(max_time);
    }
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
    killer_1 = {};
    killer_2 = {};
    countermoves = {};
    side_piece_to_history = {};
    from_to_history = {};
    continuation_history = {};
}

// --- MoveSelector ---

/**
 * Staged move generator that yields moves in order of likely quality:
 * prev_best -> TT move -> winning tacticals -> killers -> quiets -> losing captures.
 */
class Engine::MoveSelector {
public:
    MoveSelector(Board& board, Move tt_move, Move prev_best_move = NULL_MOVE)
        : phase_(MSP_PREV_BEST),
          board_(board),
          move_generator_(board),
          tt_move_(tt_move),
          prev_best_move_(prev_best_move) {}

    Move next_move(Context& ctx);
    bool before_quiet_phase() const { return phase_ <= MSP_COUNTERMOVE; }
    bool in_quiet_phase() const { return phase_ == MSP_QUIET; }
    bool in_bad_capture_phase() const { return phase_ == MSP_BAD_CAPTURE; }

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

    void sort_tactical_moves(MoveList& moves);
    void sort_quiet_moves(Context& ctx);
    Move pop_next(MoveList& moves);
    bool is_already_returned(Move move);
};

/** Return the next move in priority order, or NULL_MOVE when exhausted. */
Move Engine::MoveSelector::next_move(Context& ctx) {
    switch (phase_) {
        // Try the previous iteration's best move first (root only)
        case MSP_PREV_BEST: {
            if (prev_best_move_ != NULL_MOVE) {
                phase_ = MSP_TT;
                return prev_best_move_;
            }
            phase_ = MSP_TT;
            [[fallthrough]];
        }
        // Then the transposition table move
        case MSP_TT: {
            if (tt_move_ != NULL_MOVE && tt_move_ != prev_best_move_ && board_.is_legal_move(tt_move_)) {
                phase_ = MSP_TACTICAL;
                return tt_move_;
            }
            phase_ = MSP_TACTICAL;
            [[fallthrough]];
        }
        // Winning/equal captures and promotions (SEE >= 0)
        case MSP_TACTICAL: {
            if (!tacticals_generated_) generate_tactical_moves();
            Move next_tactical = pop_next(tactical_moves_);
            if (next_tactical != NULL_MOVE) {
                return next_tactical;
            }
            phase_ = MSP_KILLER;
            [[fallthrough]];
        }
        // Killer moves — quiet moves that caused a beta cutoff at this ply
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

            phase_ = MSP_COUNTERMOVE;
            [[fallthrough]];
        }
        // The move that historically refuted the opponent's last move
        case MSP_COUNTERMOVE: {
            Move countermove = ctx.get_countermove(board_);
            if (
                countermove != NULL_MOVE &&
                !is_already_returned(countermove) &&
                board_.is_legal_move(countermove)
            ) {
                phase_ = MSP_QUIET;
                return countermove;
            }
            phase_ = MSP_QUIET;
            [[fallthrough]];
        }
        // Remaining quiet moves, ordered by history heuristic
        case MSP_QUIET: {
            if (!quiets_generated_) generate_quiet_moves(ctx);
            Move next_quiet = pop_next(quiet_moves_);
            if (next_quiet != NULL_MOVE) {
                return next_quiet;
            }
            phase_ = MSP_BAD_CAPTURE;
            [[fallthrough]];
        }
        // Losing captures (SEE < 0), tried last
        case MSP_BAD_CAPTURE: {
            if (!bad_captures_sorted_) {
                sort_tactical_moves(bad_captures_);
                bad_captures_sorted_ = true;
            }
            Move next_bad = pop_next(bad_captures_);
            if (next_bad != NULL_MOVE) {
                return next_bad;
            }
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


void Engine::MoveSelector::sort_tactical_moves(MoveList& moves) {
    std::sort(moves.begin(), moves.end(), [this](Move a, Move b) {
        return get_tactical_score(board_, a) < get_tactical_score(board_, b);
    });
}

/** Order quiets by combined side-piece-to and from-to history scores. */
void Engine::MoveSelector::sort_quiet_moves(Context& ctx) {
    Move prev = board_.previous_move();
    Piece prev_piece = prev != NULL_MOVE ? board_.piece_map()[prev.to()] : NO_PIECE;

    auto score = [&](Move move) {
        Piece piece = board_.piece_map()[move.from()];
        MoveScore s = ctx.side_piece_to_history[board_.to_move()][piece][move.to()]
            + ctx.from_to_history[move.from()][move.to()];

        if (prev != NULL_MOVE) {
            s += ctx.continuation_history[prev_piece][prev.to()][piece][move.to()];
        }

        return s;
    };

    std::sort(quiet_moves_.begin(), quiet_moves_.end(), [&](Move a, Move b) {
        return score(a) < score(b);
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
    return move == prev_best_move_
        || move == tt_move_
        || move == returned_killer_1_
        || move == returned_killer_2_;
}

// --- Engine API ---

Engine::Engine() {
    build_lmr_table();
}

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
}

uint64_t Engine::total_nodes() const {
    uint64_t total = 0;
    for (const auto& ctx : contexts_) {
        total += ctx.nodes;
    }
    return total;
}

void Engine::search_depth(const Board& board, SearchDepth depth) {
    search(board, depth, -1, 0);
}

void Engine::search_time(const Board& board, int max_time) {
    search(board, MAX_SEARCH_PLY - 1, max_time, 0);
}

void Engine::search_nodes(const Board& board, uint64_t max_nodes) {
    search(board, MAX_SEARCH_PLY - 1, -1, max_nodes);
}

void Engine::search_infinite(const Board& board) {
    search(board, MAX_SEARCH_PLY - 1, -1, 0);
}

void Engine::apply_limits(SearchDepth max_depth, int max_time, uint64_t max_nodes) {
    if (contexts_.empty()) {
        return;
    }
    Context& ctx = contexts_[0];
    ctx.max_depth = max_depth;
    ctx.max_time = max_time;
    ctx.max_nodes = max_nodes;
    ctx.set_deadlines_from(std::chrono::steady_clock::now());
}

Engine::SearchResult Engine::sync_search(
    Board& board,
    SearchDepth max_depth,
    int max_time,
    uint64_t max_nodes
) {
    search(board, max_depth, max_time, max_nodes);
    return finish();
}

Engine::SearchResult Engine::sync_search_depth(Board& board, SearchDepth depth) {
    return sync_search(board, depth, -1, 0);
}

Engine::SearchResult Engine::sync_search_time(Board& board, int max_time) {
    return sync_search(board, MAX_SEARCH_PLY - 1, max_time, 0);
}

Engine::SearchResult Engine::sync_search_nodes(Board& board, uint64_t max_nodes) {
    return sync_search(board, MAX_SEARCH_PLY - 1, -1, max_nodes);
}

// --- Threading ---

void Engine::search(
    const Board& board,
    SearchDepth max_depth,
    int max_time,
    uint64_t max_nodes
) {
    stop();
    reset();

    best_result_.move = find_book_move(board);
    if (best_result_.move != NULL_MOVE) {
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
    main_ctx.max_time = max_time;
    main_ctx.max_nodes = max_nodes;

    for (int i = 0; i < num_threads_; i++) {
        // Stagger start depths across threads for better lazy SMP diversity
        SearchDepth start_depth = 1 + i % 2;
        Board thread_board = board;
        auto worker = [this, i, b = std::move(thread_board), start_depth]() mutable {
            SearchResult result = iterative_deepening(b, contexts_[i], start_depth);
            if (contexts_[i].is_main_thread) {
                best_result_ = result;
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

Engine::SearchResult Engine::finish() {
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
    return best_result_;
}

void Engine::reset() {
    best_result_ = {};
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

    if (now >= ctx.soft_deadline) {
        ctx.search_interrupted = true;
        external_stop_ = true;
        return true;
    }

    if (ctx.max_time != -1 && now >= ctx.hard_deadline) {
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

    std::string pv_str;
    for (int i = 0; i < ctx.pv_length[0]; i++) {
        pv_str += " " + decode_move_to_uci(ctx.pv[0][i]);
    }

    uci_print(
        "info"
        " depth " + std::to_string(depth) +
        " score " + score_str +
        " nodes " + std::to_string(nodes) +
        " nps " + std::to_string(nps) +
        " time " + std::to_string(elapsed_ms) +
        (pv_str.empty() ? "" : " pv" + pv_str)
    );
}

/** Print UCI bestmove with a ponder move if one exists in the TT. */
void Engine::emit_best_move(Board& board) {
    std::string ponder_str;
    if (best_result_.move != NULL_MOVE) {
        // Make the best move on the board to look up the opponent's expected reply in the TT
        board.make_move(best_result_.move);
        TTEntry* tt_entry = g_tt.get_entry(board.position_hash());
        if (tt_entry && tt_entry->move() != NULL_MOVE && board.is_legal_move(tt_entry->move())) {
            ponder_str = " ponder " + decode_move_to_uci(tt_entry->move());
        }
        board.unmake_move(best_result_.move);
    }

    uci_print("bestmove " + decode_move_to_uci(best_result_.move) + ponder_str);
}

// --- Search ---

struct Engine::TTProbeResult {
    Move tt_move;
    bool has_cutoff;
    PositionScore tt_score;
    SearchDepth tt_depth;
    TTNode tt_node;
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
            return {tt_move, true, tt_score, tt_entry->depth(), tt_entry->node()};
        }

        // No cutoff, but still use the TT move for move ordering
        return {tt_move, false, tt_score, tt_entry->depth(), tt_entry->node()};
    }

    // No TT entry on a deep PV node, so do a shallow search to find a move for ordering
    if (is_pv_node && depth >= prm.minimum_iid_depth) {
        SearchDepth iid_depth = std::max<SearchDepth>(0, depth / prm.iid_depth_divisor);
        negamax(board, ctx, iid_depth, alpha, beta);

        tt_entry = g_tt.get_entry(board.position_hash());
        if (tt_entry) {
            PositionScore tt_score = denormalize_tt_score(tt_entry->score(), ctx.search_ply(board.ply()));
            return {tt_entry->move(), false, tt_score, tt_entry->depth(), tt_entry->node()};
        }
    }

    return {NULL_MOVE, false, 0, 0, TT_FAIL_LOW};
}

void Engine::update_pv(Context& ctx, int ply, Move move) {
    ctx.pv[ply][0] = move;
    for (int i = 0; i < ctx.pv_length[ply + 1]; i++) {
        ctx.pv[ply][i + 1] = ctx.pv[ply + 1][i];
    }
    ctx.pv_length[ply] = 1 + ctx.pv_length[ply + 1];
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
    if (cutoff_move.type() != MT_QUIET) {
        return;
    }

    Move prev = b.previous_move();
    Piece prev_piece = prev != NULL_MOVE ? b.piece_map()[prev.to()] : NO_PIECE;
    Square prev_to = prev.to();

    // Blend the bonus toward the current score so history values stay bounded
    auto apply_history_bonus = [](MoveScore& score, MoveScore bonus) {
        bonus = std::clamp(bonus, MIN_MOVE_SCORE, MAX_MOVE_SCORE);
        score += bonus - score * std::abs(bonus) / MAX_MOVE_SCORE;
    };

    auto update_history_tables = [&](Move move, MoveScore bonus) {
        Piece moving_piece = b.piece_map()[move.from()];

        apply_history_bonus(ctx.side_piece_to_history[b.to_move()][moving_piece][move.to()], bonus);
        apply_history_bonus(ctx.from_to_history[move.from()][move.to()], bonus);
        if (prev != NULL_MOVE) {
            apply_history_bonus(
                ctx.continuation_history[prev_piece][prev_to][moving_piece][move.to()],
                bonus
            );
        }
    };

    // Remove the cutoff move itself from the quiet list before applying malus
    searched_quiet_moves.pop();

    int ply = ctx.search_ply(b.ply());
    MoveScore bonus = depth * depth;

    update_killer_table(ctx, cutoff_move, ply);
    update_history_tables(cutoff_move, bonus);

    // Record this move as the refutation of the opponent's last move
    if (prev != NULL_MOVE) {
        ctx.countermoves[prev_piece][prev_to] = cutoff_move;
    }

    // Penalise quiet moves that were tried before the cutoff move (they failed)
    MoveScore malus = -(bonus / prm.history_malus_divisor);
    for (const Move move : searched_quiet_moves) {
        update_history_tables(move, malus);
    }
}

PositionScore Engine::quiescence_search(
    Board& board,
    Context& ctx,
    PositionScore alpha,
    PositionScore beta
) {
    ctx.nodes++;

    if (should_stop_search(ctx)) {
        ctx.search_interrupted = true;
        return SEARCH_INTERRUPTED;
    }

    if (is_engine_draw(board)) {
        return STALEMATE_SCORE;
    }

    bool in_check = board.in_check();

    // If not in check, the side to move can always stand pat with the static eval
    if (!in_check) {
        PositionScore static_eval = board.nnue_evaluate();
        alpha = std::max(alpha, static_eval);
        if (alpha >= beta) {
            return alpha;
        }
    }

    // In check we must search all moves (evasions); otherwise only captures/promotions
    MoveGenerator qmg(board);
    MoveList moves = in_check ? qmg.generate_all() : qmg.generate_tacticals();

    // Sort captures by MVV-LVA (descending) to try the most promising ones first
    std::sort(moves.begin(), moves.end(), [&board](Move a, Move b) {
        return get_tactical_score(board, a) > get_tactical_score(board, b);
    });

    if (moves.is_empty()) {
        if (in_check) {
            return -CHECKMATE_SCORE + ctx.search_ply(board.ply());
        }
        return alpha;
    }

    for (Move move : moves) {
        // Skip captures that lose too much material
        if (
            !in_check
            && move.type() == MT_CAPTURE
            && see(board, move) < prm.see_cutoff
        ) {
            continue;
        }

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
    bool allow_null_move,
    Move excluded_move
) {
    PositionScore original_alpha = alpha;
    int ply = ctx.search_ply(board.ply());
    ctx.nodes++;

    if (ctx.is_main_thread) {
        ctx.pv_length[ply] = 0;
    }

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
    if (tt_result.has_cutoff && excluded_move == NULL_MOVE) {
        return tt_result.tt_score;
    }

    bool in_check = board.in_check();

    // If we skip our turn and the opponent still can't beat beta, the position
    // is likely so good we can prune.
    if (can_apply_null_move(
        in_check,
        depth,
        is_pv_node,
        allow_null_move,
        board.has_non_pawn_material(board.to_move())
    )) {
        int reduction = (depth >= prm.null_move_deeper_threshold)
            ? prm.null_move_deep_reduction
            : prm.null_move_base_reduction;
        SearchDepth null_depth = std::max(0, static_cast<int>(depth) - reduction);
        board.make_null_move();
        PositionScore score = -negamax(board, ctx, null_depth, -beta, -beta + 1, false);
        board.unmake_null_move();

        if (should_stop_search(ctx)) {
            return SEARCH_INTERRUPTED;
        }

        if (score >= beta) {
            return score;
        }
    }

    // Computed once on first use, then cached for all the pruning decisions below.
    PositionScore static_eval = DUMMY_SCORE;
    auto eval = [&]() {
        if (static_eval == DUMMY_SCORE) {
            static_eval = board.nnue_evaluate();
        } 
        return static_eval;
    };

    // If the static eval is so far above beta that even after subtracting a
    // depth-scaled margin the opponent still can't beat it, cut the node early.
    if (can_apply_pruning(
            alpha,
            beta,
            is_pv_node,
            in_check,
            depth,
            prm.reverse_futility_max_depth
    )) {
        PositionScore rfp_margin = 
            prm.reverse_futility_margin_per_depth * depth + prm.reverse_futility_margin_base;
        if (eval() - rfp_margin >= beta) {
            return beta;
        }
    }

    // When the static eval is far below alpha at shallow depth, only tactics can
    // save us so drop straight into quiescence search.
    if (can_apply_pruning(
        alpha,
        beta,
        is_pv_node,
        in_check,
        depth,
        prm.razoring_max_depth
    )) {
        if (eval() + prm.razoring_margin < alpha) {
            return quiescence_search(board, ctx, alpha, beta);
        }
    }

    // At shallow depths, if the static eval is far below alpha, quiet moves are
    // unlikely to raise it enough — we'll skip them in the move loop below.
    bool can_use_futility_pruning = can_apply_pruning(
        alpha,
        beta,
        is_pv_node,
        in_check,
        depth,
        prm.futility_max_depth
    );
    PositionScore futility_margin = prm.futility_margin_per_depth * depth + prm.futility_margin_base;

    // At shallow depths, skip quiet moves once we've searched enough of them.
    bool can_use_lmp = can_apply_pruning(
        alpha,
        beta,
        is_pv_node,
        in_check,
        depth,
        prm.lmp_max_depth
    );
    int lmp_threshold = prm.lmp_base + depth * depth;

    // At shallow depths, skip losing captures entirely.
    bool can_use_see_pruning = can_apply_pruning(
        alpha,
        beta,
        is_pv_node,
        in_check,
        depth,
        prm.see_pruning_max_depth
    );

    // Re-search at reduced depth with the TT move excluded to see how the
    // position holds up without the best known move.
    int singular_extension = 0;
    if (
        depth >= prm.reduced_search_min_depth
        && tt_result.tt_move != NULL_MOVE
        && excluded_move == NULL_MOVE
        && tt_result.tt_depth >= depth - prm.reduced_search_tt_depth_margin
        && (tt_result.tt_node == TT_FAIL_HIGH || tt_result.tt_node == TT_EXACT)
        && !in_check
    ) {
        PositionScore rs_beta = tt_result.tt_score - prm.reduced_search_margin_multiplier * depth;
        SearchDepth rs_depth = std::max<SearchDepth>(0, depth / prm.reduced_search_depth_divisor);
        PositionScore rs_score = negamax(
            board,
            ctx,
            rs_depth,
            rs_beta - 1,
            rs_beta,
            false,
            tt_result.tt_move
        );

        // No alternative came close to the TT move, so extend it.
        if (rs_score < rs_beta) {
            singular_extension = 1;
        }
        // Even without the TT move, alternatives beat beta — prune the node.
        else if (rs_score >= beta) {
            return beta;
        }
    }

    PositionScore best_score = DUMMY_SCORE;
    Move best_move;
    MoveList searched_quiet_moves;

    MoveSelector move_selector(board, tt_result.tt_move);
    bool has_moves = false;
    bool is_first_move = true;
    int num_moves = 0;

    while (true) {
        Move move = move_selector.next_move(ctx);
        if (move == NULL_MOVE) {
            break;
        }
        if (move == excluded_move) {
            continue;
        }
        has_moves = true;

        bool quiet_gives_check = false;
        bool quiet_gives_check_known = false;
        auto gives_check = [&]() {
            if (!quiet_gives_check_known) {
                quiet_gives_check = quiet_move_gives_check(board, move);
                quiet_gives_check_known = true;
            }
            return quiet_gives_check;
        };

        // Futility pruning
        if (
            can_use_futility_pruning
            && num_moves > 0
            && move_selector.in_quiet_phase()
            && eval() + futility_margin < alpha
            && !gives_check()
        ) {
            continue;
        }

        // Late move pruning
        if (
            can_use_lmp
            && num_moves >= lmp_threshold
            && move_selector.in_quiet_phase()
            && !gives_check()
        ) {
            continue;
        }

        // SEE pruning
        if (can_use_see_pruning && num_moves > 0 && move_selector.in_bad_capture_phase()) {
            continue;
        }

        board.make_move(move);
        num_moves++;

        // Search later moves at reduced depth since good move ordering means
        // they're unlikely to be best.
        int reduction = LMR_TABLE[depth][num_moves];
        if (is_pv_node) reduction -= prm.lmr_pv_reduction;
        if (in_check) reduction = 0;       // don't reduce when in check
        if (move_selector.before_quiet_phase()) reduction = 0;  // don't reduce tacticals/killers

        // Extensions
        int extension = board.in_check() ? 1 : 0; // check extension
        if (move == tt_result.tt_move) {
            extension += singular_extension;
        }

        reduction = std::clamp(reduction, 0, depth - 1);

        // Principal Variation Search
        PositionScore score;
        if (is_first_move) {
            // Search the first move with a full window.
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

            if (ctx.is_main_thread) {
                update_pv(ctx, ply, move);
            }
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
    int ply = ctx.search_ply(board.ply());
    if (ctx.is_main_thread) {
        ctx.pv_length[ply] = 0;
    }

    SearchResult best_result{NULL_MOVE, DUMMY_SCORE};
    MoveList searched_quiet_moves;
    TTEntry* tt_entry = g_tt.get_entry(board.position_hash());
    Move tt_move = tt_entry ? tt_entry->move() : NULL_MOVE;
    MoveSelector move_selector(board, tt_move, prev_best_move);
    bool is_first_move = true;

    while (true) {
        Move move = move_selector.next_move(ctx);
        if (move == NULL_MOVE) {
            break;
        }

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
            return {NULL_MOVE, DUMMY_SCORE};
        }

        if (score > best_result.score) {
            best_result = {move, score};
        }

        if (score > alpha) {
            alpha = score;

            if (ctx.is_main_thread) {
                update_pv(ctx, ply, move);
            }
        }

        if (move.type() == MT_QUIET) {
            searched_quiet_moves.add(move);
        }

        if (alpha >= beta) {
            handle_beta_cutoff(board, ctx, move, depth, searched_quiet_moves);
            break;
        }
    }

    return best_result;
}

Engine::SearchResult Engine::iterative_deepening(Board& board, Context& ctx, SearchDepth depth) {
    MoveGenerator move_generator(board);
    MoveList moves = move_generator.generate_all();
    if (moves.is_empty()) {
        signal_stop(ctx);
        return {NULL_MOVE, 0};
    }

    SearchResult best_result{NULL_MOVE, 0};
    Move last_best_move = NULL_MOVE;

    ctx.reset(board.ply());
    ctx.search_start = std::chrono::steady_clock::now();
    ctx.set_deadlines_from(ctx.search_start);

    if (ctx.is_main_thread) {
        g_tm().init_search(moves.size());
    }

    while (true) {
        if (should_stop_search(ctx)) {
            break;
        }

        if (ctx.is_main_thread && depth > ctx.max_depth) {
            break;
        }

        // Start with a narrow window around the previous score, widening on
        // fail-high/fail-low. Use full window at depth 1 since there's no prior score.
        int alpha;
        int beta;
        int alpha_delta = prm.aspiration_window;
        int beta_delta = prm.aspiration_window;
        if (depth == 1) {
            alpha = -CHECKMATE_SCORE;
            beta = CHECKMATE_SCORE;
        } else {
            alpha = std::max(best_result.score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            beta = std::min(best_result.score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
        }

        // Re-search with exponentially wider windows on fail-high/fail-low
        SearchResult depth_result;
        while (true) {
            if (should_stop_search(ctx)) {
                break;
            }

            depth_result = search_at_depth(board, ctx, depth, best_result.move, alpha, beta);
            if (depth_result.score <= alpha) {
                alpha_delta *= 2;
                alpha = std::max(best_result.score - alpha_delta, -static_cast<int>(CHECKMATE_SCORE));
            } else if (depth_result.score >= beta) {
                beta_delta *= 2;
                beta = std::min(best_result.score + beta_delta, static_cast<int>(CHECKMATE_SCORE));
            } else {
                break;
            }
        }

        if (should_stop_search(ctx)) {
            break;
        }

        if (depth_result.move != NULL_MOVE) {
            // Track cross-depth best-move changes for instability signal
            if (last_best_move != NULL_MOVE
                && depth_result.move != last_best_move
                && ctx.is_main_thread
            ) {
                g_tm().on_root_best_move_change();
            }
            last_best_move = depth_result.move;
            best_result = depth_result;
        }

        // Soft time management check
        if (ctx.is_main_thread && ctx.has_runtime_limits()) {
            if (g_tm().should_stop_after_depth(
                depth,
                best_result.score,
                ctx.elapsed_ms()
            )) {
                break;
            }
        }

        emit_search_info(ctx, depth, best_result.score);

        depth++;
    }

    signal_stop(ctx);

    // Fall back to the first legal move if search was interrupted before completing depth 1
    if (best_result.move == NULL_MOVE && !moves.is_empty()) {
        best_result.move = moves[0];
    }
    return best_result;
}
