#include "board.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

#include "bitboard.hpp"
#include "evaluate.hpp"
#include "move_generator.hpp"
#include "notation.hpp"
#include "zobrist.hpp"

namespace {

/** Map a FEN character (case-insensitive) to the corresponding Piece enum. */
Piece char_to_piece(char c) {
    switch (std::toupper(c)) {
        case 'P': return PAWN;
        case 'B': return BISHOP;
        case 'N': return KNIGHT;
        case 'R': return ROOK;
        case 'Q': return QUEEN;
        case 'K': return KING;
        default: return NO_PIECE;
    }
}

/** Return the square of the pawn captured by an en passant move (one rank behind the target). */
Square en_passant_capture_square(Square to, Side moving_side) {
    return moving_side == WHITE ? to + SOUTH : to + NORTH;
}

/** Lookup table: castling rights invalidated when a piece moves from/to each square. */
constexpr auto castling_rights_updates = []() {
    std::array<CastlingRights, NUM_SQUARES> updates = {NO_CASTLING_RIGHTS};
    updates[E1] = WHITE_SHORT | WHITE_LONG;
    updates[H1] = WHITE_SHORT;
    updates[A1] = WHITE_LONG;
    updates[E8] = BLACK_SHORT | BLACK_LONG;
    updates[H8] = BLACK_SHORT;
    updates[A8] = BLACK_LONG;
    return updates;
}();

} // namespace

// --- Construction / Reset ---

Board::Board() {
    reset();
}

void Board::reset() {
    pieces_[WHITE].fill(EMPTY_BITBOARD);
    pieces_[BLACK].fill(EMPTY_BITBOARD);
    sides_.fill(EMPTY_BITBOARD);

    piece_map_.fill(NO_PIECE);
    king_squares_.fill(NO_SQUARE);
    position_hashes_.fill(0);
    pawn_hashes_.fill(0);

    early_score_.fill(0);
    late_score_.fill(0);
    game_phase_ = 0;

    occupied_ = EMPTY_BITBOARD;
    to_move_ = NO_SIDE;
    castling_rights_ = NO_CASTLING_RIGHTS;
    en_passant_target_ = NO_SQUARE;
    halfmoves_ = 0;
    fullmoves_ = 0;
    ply_ = 0;
    position_hash_ = 0;
    pawn_hash_ = 0;

    nnue_.clear_history();
}

// --- FEN ---

void Board::load_from_fen(std::string_view fen) {
    reset();

    std::vector<std::string> parts;
    std::istringstream iss{std::string{fen}};
    std::string item;

    while (std::getline(iss, item, ' ')) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }

    std::string position = parts[0];
    std::string to_move = parts.size() > 1 ? parts[1] : "w";
    std::string castling_rights = parts.size() > 2 ? parts[2] : "-";
    std::string en_passant_target = parts.size() > 3 ? parts[3] : "-";
    std::string halfmoves = parts.size() > 4 ? parts[4] : "0";
    std::string fullmoves = parts.size() > 5 ? parts[5] : "1";

    int rank = 7;
    int file = 0;
    for (char c : position) {
        if (c == '/') {
            rank -= 1;
            file = 0;
            continue;
        }

        if (std::isdigit(c)) {
            file += c - '0';
            continue;
        }

        Square square = get_square(rank, file);
        Side side = std::isupper(c) ? WHITE : BLACK;
        place_piece(side, char_to_piece(c), square, false);

        file++;
    }

    if (to_move == "w") {
        this->to_move_ = WHITE;
    } else {
        this->to_move_ = BLACK;
        xor_side_to_move();
    }

    for (char c : castling_rights) {
        switch (c) {
            case 'K': this->castling_rights_ |= WHITE_SHORT; break;
            case 'Q': this->castling_rights_ |= WHITE_LONG; break;
            case 'k': this->castling_rights_ |= BLACK_SHORT; break;
            case 'q': this->castling_rights_ |= BLACK_LONG; break;
        }
    }

    xor_castling_rights();

    if (en_passant_target != "-") {
        this->en_passant_target_ = uci_to_index(en_passant_target);
        xor_en_passant();
    }

    this->halfmoves_ = std::stoi(halfmoves);
    this->fullmoves_ = std::stoi(fullmoves);

    position_hashes_[0] = position_hash_;
    pawn_hashes_[0] = pawn_hash_;

    nnue_.refresh_features(king_squares_, pieces_);
}
// --- Move Helpers ---

/** Update the en passant target square after a move; only set on double pawn pushes. */
void Board::set_en_passant_target(Side side, Piece piece, Square from, Square to) {
    xor_en_passant();
    en_passant_target_ = NO_SQUARE;

    if (side == WHITE && piece == PAWN && get_rank(from) == RANK_2 && get_rank(to) == RANK_4) {
        en_passant_target_ = to - 8;
    } else if (side == BLACK && piece == PAWN && get_rank(from) == RANK_7 && get_rank(to) == RANK_5) {
        en_passant_target_ = to + 8;
    }

    xor_en_passant();
}

/** Remove the captured piece from the board, handling en passant offset. Returns the captured piece type. */
Piece Board::handle_capture(Square capture_square, Side moving_side, MoveFlag move_flag, bool update_nnue) {
    halfmoves_ = 0;
    Side captured_side = opposite_side(moving_side);

    if (move_flag == MF_EN_PASSANT) {
        capture_square = en_passant_capture_square(capture_square, moving_side);
    }

    Piece captured_piece = piece_map_[capture_square];
    remove_piece(captured_side, captured_piece, capture_square, update_nnue);
    return captured_piece;
}

/** Move the rook to its post-castling square based on the king's destination. */
void Board::handle_castle(Square castle_square) {
    // NNUE is refreshed from scratch after king moves, so skip incremental updates.
    switch (castle_square) {
        case C1: remove_piece(WHITE, ROOK, A1, false); place_piece(WHITE, ROOK, D1, false); break;
        case G1: remove_piece(WHITE, ROOK, H1, false); place_piece(WHITE, ROOK, F1, false); break;
        case C8: remove_piece(BLACK, ROOK, A8, false); place_piece(BLACK, ROOK, D8, false); break;
        case G8: remove_piece(BLACK, ROOK, H8, false); place_piece(BLACK, ROOK, F8, false); break;
    }
}

/** Reverse the rook movement from handle_castle, restoring the rook to its original square. */
void Board::undo_castle(Square castle_square) {
    switch (castle_square) {
        case C1: remove_piece(WHITE, ROOK, D1, false); place_piece(WHITE, ROOK, A1, false); break;
        case G1: remove_piece(WHITE, ROOK, F1, false); place_piece(WHITE, ROOK, H1, false); break;
        case C8: remove_piece(BLACK, ROOK, D8, false); place_piece(BLACK, ROOK, A8, false); break;
        case G8: remove_piece(BLACK, ROOK, F8, false); place_piece(BLACK, ROOK, H8, false); break;
    }
}

/** Revoke castling rights affected by a piece moving from/to king or rook squares. */
void Board::update_castling_rights(Square from, Square to) {
    xor_castling_rights();
    castling_rights_ &= ~castling_rights_updates[from];
    castling_rights_ &= ~castling_rights_updates[to];
    xor_castling_rights();
}

// --- Make / Unmake Move ---

void Board::make_move(Move move) {
    // Snapshot state that cannot be reconstructed from the move alone
    Board::UndoState state(en_passant_target_, castling_rights_, halfmoves_, NO_PIECE);

    Square from = move.from();
    Square to = move.to();
    MoveType move_type = move.type();
    MoveFlag move_flag = move.flag();

    Piece moving_piece = piece_map_[from];
    Side moving_side = to_move_;
    bool is_king_move = moving_piece == KING;

    // Update clocks and en passant before touching pieces
    halfmoves_++;
    if (moving_piece == PAWN) halfmoves_ = 0;
    if (moving_side == BLACK) fullmoves_++;

    set_en_passant_target(moving_side, moving_piece, from, to);

    // Move the piece — remove from origin, handle captures, promote, place on destination
    // King moves skip incremental NNUE updates since refresh_features recomputes everything.
    nnue_.push();
    bool update_nnue = !is_king_move;
    remove_piece(moving_side, moving_piece, from, update_nnue);

    if (move_type == MT_CAPTURE) {
        state.captured_piece = handle_capture(to, moving_side, move_flag, update_nnue);
    }

    if (move.is_promotion()) {
        moving_piece = move.promoted_piece();
    }

    place_piece(moving_side, moving_piece, to, update_nnue);

    // Handle castling rook and revoke affected castling rights
    if (move_flag == MF_CASTLE) {
        handle_castle(to);
    }

    update_castling_rights(from, to);

    // Refresh NNUE accumulators from scratch after king moves
    if (is_king_move) {
        nnue_.refresh_features(king_squares_, pieces_);
    }

    // Switch side and commit to history
    toggle_side_to_move();

    move_history_[ply_] = move;
    state_history_[ply_] = state;
    ply_ += 1;
    position_hashes_[ply_] = position_hash_;
    // pawn_hashes_[ply_] = pawn_hash_;
}

void Board::unmake_move(Move move) {
    Square from = move.from();
    Square to = move.to();
    MoveType move_type = move.type();
    MoveFlag move_flag = move.flag();

    // Restore saved state (en passant, castling, halfmove clock)
    Side moving_side = opposite_side(to_move_);
    ply_ -= 1;

    const Board::UndoState& prev_state = state_history_[ply_];
    en_passant_target_ = prev_state.en_passant_target;
    castling_rights_ = prev_state.castling_rights;
    halfmoves_ = prev_state.halfmoves;

    if (moving_side == BLACK) {
        fullmoves_--;
    }

    // Reverse the piece movement — remove from destination, demote if needed, place on origin
    Piece moving_piece = piece_map_[to];
    remove_piece(moving_side, moving_piece, to, false);

    if (move.is_promotion()) {
        moving_piece = PAWN;
    }

    place_piece(moving_side, moving_piece, from, false);

    // Restore captured piece and undo castling rook movement
    if (move_type == MT_CAPTURE) {
        Square capture_sq = move_flag == MF_EN_PASSANT ? en_passant_capture_square(to, moving_side) : to;
        place_piece(opposite_side(moving_side), prev_state.captured_piece, capture_sq, false);
    }

    if (move_flag == MF_CASTLE) {
        undo_castle(to);
    }

    // Switch side back and restore hashes and NNUE state from history
    toggle_side_to_move();
    position_hash_ = position_hashes_[ply_];
    // pawn_hash_ = pawn_hashes_[ply_];
    nnue_.pop();
}

// --- Null Move ---

void Board::make_null_move() {
    state_history_[ply_] = Board::UndoState(en_passant_target_, castling_rights_, halfmoves_, NO_PIECE);
    move_history_[ply_] = NULL_MOVE;
    xor_en_passant();
    en_passant_target_ = NO_SQUARE;
    toggle_side_to_move();
    ply_++;
    position_hashes_[ply_] = position_hash_;
    // pawn_hashes_[ply_] = pawn_hash_;
}

void Board::unmake_null_move() {
    ply_ -= 1;
    const Board::UndoState& prev_state = state_history_[ply_];
    en_passant_target_ = prev_state.en_passant_target;
    toggle_side_to_move();
    position_hash_ = position_hashes_[ply_];
    // pawn_hash_ = pawn_hashes_[ply_];
}

// --- Piece Mutation ---

void Board::place_piece(Side side, Piece piece, Square square, bool update_nnue) {
    // Set the bit for this square in the piece, side, and occupancy bitboards
    Bitboard mask = get_mask(square);
    pieces_[side][piece] |= mask;
    sides_[side] |= mask;
    occupied_ |= mask;

    // Update the square-to-piece lookup and track king position
    piece_map_[square] = piece;
    if (piece == KING) {
        king_squares_[side] = square;
    }

    // Toggle the Zobrist hash for this piece placement (XOR is its own inverse)
    uint64_t zobrist_number = ZOBRIST_PIECES[side][piece][square];
    position_hash_ ^= zobrist_number;
    // if (piece == PAWN) {
    //     pawn_hash_ ^= zobrist_number;
    // }

    // Incrementally update evaluation scores and game phase
    // early_score_[side] += EARLY_EVAL_TABLE[side][piece][square];
    // late_score_[side] += LATE_EVAL_TABLE[side][piece][square];
    game_phase_ += GAME_PHASE_INCREMENT[piece];

    // Incrementally update NNUE accumulator
    if (update_nnue) {
        nnue_.add_feature(king_squares_, side, piece, square);
    }
}

void Board::remove_piece(Side side, Piece piece, Square square, bool update_nnue) {
    // Clear the bit for this square from the piece, side, and occupancy bitboards
    Bitboard mask = ~get_mask(square);
    pieces_[side][piece] &= mask;
    sides_[side] &= mask;
    occupied_ &= mask;

    // Clear the square-to-piece lookup
    piece_map_[square] = NO_PIECE;

    // Toggle the Zobrist hash for this piece removal (XOR is its own inverse)
    uint64_t zobrist_number = ZOBRIST_PIECES[side][piece][square];
    position_hash_ ^= zobrist_number;
    // if (piece == PAWN) {
    //     pawn_hash_ ^= zobrist_number;
    // }

    // early_score_[side] -= EARLY_EVAL_TABLE[side][piece][square];
    // late_score_[side] -= LATE_EVAL_TABLE[side][piece][square];
    game_phase_ -= GAME_PHASE_INCREMENT[piece];

    // Incrementally update NNUE accumulator
    if (update_nnue) {
        nnue_.remove_feature(king_squares_, side, piece, square);
    }
}

// --- Hash ---

void Board::xor_en_passant() {
    if (en_passant_target_ != NO_SQUARE) {
        Side capturer = get_rank(en_passant_target_) == RANK_3 ? BLACK : WHITE;
        if (get_pawn_attacks(capturer, en_passant_target_) & pieces_[capturer][PAWN]) {
            position_hash_ ^= ZOBRIST_EN_PASSANT_TARGETS[get_file(en_passant_target_)];
        }
    }
}

void Board::xor_castling_rights() {
    position_hash_ ^= ZOBRIST_CASTLING_RIGHTS[castling_rights_];
}

void Board::xor_side_to_move() {
    position_hash_ ^= ZOBRIST_SIDE_TO_MOVE;
}

void Board::toggle_side_to_move() {
    to_move_ = opposite_side(to_move_);
    xor_side_to_move();
}

// --- Board Query ---

Side Board::get_side(Square square) const {
    return (sides_[BLACK] >> square) & uint64_t{1};
}

bool Board::has_non_pawn_material(Side side) const {
    return pieces_[side][KNIGHT] | pieces_[side][BISHOP] | pieces_[side][ROOK] | pieces_[side][QUEEN];
}

bool Board::has_repeated() const {
    if (ply_ < 4 || halfmoves_ < 4) {
        return false;
    }

    int last_reversible_ply = std::max(0, ply_ - halfmoves_);
    for (int past_ply = ply_ - 2; past_ply >= last_reversible_ply; past_ply -= 2) {
        if (position_hashes_[past_ply] == position_hash_) {
            return true;
        }
    }

    return false;
}

// --- Attack Detection ---

bool Board::in_check(Side side) const {
    Side checked_side = side == NO_SIDE ? to_move_ : side;
    return is_attacked(king_squares_[checked_side], opposite_side(checked_side));
}

bool Board::is_attacked(Square sq, Side by) const {
    const auto& attacker_pieces = pieces_[by];
    return (
        (get_pawn_attacks(by, sq) & attacker_pieces[PAWN]) |
        (get_piece_attacks(KNIGHT, sq, occupied_) & attacker_pieces[KNIGHT]) |
        (get_piece_attacks(KING, sq, occupied_) & attacker_pieces[KING]) |
        (get_piece_attacks(ROOK, sq, occupied_) & (attacker_pieces[ROOK] | attacker_pieces[QUEEN])) |
        (get_piece_attacks(BISHOP, sq, occupied_) & (attacker_pieces[BISHOP] | attacker_pieces[QUEEN]))
    );
}

Bitboard Board::attackers_to(Square sq, Bitboard occupied) const {
    Bitboard all_knights = pieces_[WHITE][KNIGHT] | pieces_[BLACK][KNIGHT];
    Bitboard all_bishops = pieces_[WHITE][BISHOP] | pieces_[BLACK][BISHOP];
    Bitboard all_rooks = pieces_[WHITE][ROOK] | pieces_[BLACK][ROOK];
    Bitboard all_queens = pieces_[WHITE][QUEEN] | pieces_[BLACK][QUEEN];
    Bitboard all_kings = pieces_[WHITE][KING] | pieces_[BLACK][KING];
    Bitboard attackers =
        (get_pawn_attacks(WHITE, sq) & pieces_[WHITE][PAWN]) |
        (get_pawn_attacks(BLACK, sq) & pieces_[BLACK][PAWN]) |
        (get_piece_attacks(KNIGHT, sq, occupied) & all_knights) |
        (get_piece_attacks(KING, sq, occupied) & all_kings) |
        (get_piece_attacks(ROOK, sq, occupied) & (all_rooks | all_queens)) |
        (get_piece_attacks(BISHOP, sq, occupied) & (all_bishops | all_queens));

    return attackers & occupied;
}

// --- Legality ---

bool Board::is_legal_move(Move move) {
    Side friendly_side = to_move_;
    Side enemy_side = opposite_side(friendly_side);
    Square from = move.from();
    Square to = move.to();
    MoveFlag flag = move.flag();
    Bitboard to_mask = get_mask(to);
    Piece piece = piece_map_[from];

    if (piece == NO_PIECE || get_side(from) != friendly_side) return false;
    if (piece_map_[to] == KING) return false;

    if (flag == MF_CASTLE) {
        if (piece != KING || in_check(friendly_side)) return false;

        if (friendly_side == WHITE && to == G1) {
            if (!(castling_rights_ & WHITE_SHORT)) return false;
            if (piece_map_[H1] != ROOK || get_side(H1) != friendly_side) return false;
            if (piece_map_[F1] != NO_PIECE || piece_map_[G1] != NO_PIECE) return false;
            if (is_attacked(F1, enemy_side)) return false;
        } else if (friendly_side == WHITE && to == C1) {
            if (!(castling_rights_ & WHITE_LONG)) return false;
            if (piece_map_[A1] != ROOK || get_side(A1) != friendly_side) return false;
            if (piece_map_[B1] != NO_PIECE || piece_map_[C1] != NO_PIECE || piece_map_[D1] != NO_PIECE) return false;
            if (is_attacked(D1, enemy_side)) return false;
        } else if (friendly_side == BLACK && to == G8) {
            if (!(castling_rights_ & BLACK_SHORT)) return false;
            if (piece_map_[H8] != ROOK || get_side(H8) != friendly_side) return false;
            if (piece_map_[F8] != NO_PIECE || piece_map_[G8] != NO_PIECE) return false;
            if (is_attacked(F8, enemy_side)) return false;
        } else if (friendly_side == BLACK && to == C8) {
            if (!(castling_rights_ & BLACK_LONG)) return false;
            if (piece_map_[A8] != ROOK || get_side(A8) != friendly_side) return false;
            if (piece_map_[B8] != NO_PIECE || piece_map_[C8] != NO_PIECE || piece_map_[D8] != NO_PIECE) return false;
            if (is_attacked(D8, enemy_side)) return false;
        } else {
            return false;
        }
    } else if (flag == MF_EN_PASSANT) {
        if (piece != PAWN) return false;
        if (to != en_passant_target_) return false;
        if (piece_map_[to] != NO_PIECE) return false;

        Square cap_sq = en_passant_capture_square(to, friendly_side);
        bool enemy_pawn_behind = piece_map_[cap_sq] == PAWN && get_side(cap_sq) == enemy_side;
        if (!enemy_pawn_behind) return false;
    } else {
        if (move.type() == MT_CAPTURE) {
            if (piece_map_[to] == NO_PIECE || get_side(to) != enemy_side) return false;
        } else {
            if (piece_map_[to] != NO_PIECE) return false;
        }

        if (move.is_promotion() && piece != PAWN) return false;

        if (piece == PAWN) {
            if (move.type() == MT_CAPTURE) {
                bool valid_attack = get_pawn_attacks(enemy_side, from) & to_mask;
                if (!valid_attack) return false;
            } else {
                int dir = friendly_side == WHITE ? NORTH : SOUTH;
                Square single_push = from + dir;
                Square double_push = from + dir + dir;
                int start_rank = friendly_side == WHITE ? RANK_2 : RANK_7;

                bool is_single = to == single_push;
                bool is_double = to == double_push
                              && get_rank(from) == start_rank
                              && piece_map_[single_push] == NO_PIECE;

                if (!is_single && !is_double) return false;
            }
        } else {
            bool can_attack = ::get_piece_attacks(piece, from, occupied_) & to_mask;
            if (!can_attack) return false;
        }
    }

    make_move(move);
    bool leaves_king_attacked = in_check(friendly_side);
    unmake_move(move);

    return !leaves_king_attacked;
}
