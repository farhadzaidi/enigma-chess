// --- Helpers ---

inline Square en_passant_capture_square(Square to, Side moving_side) {
    return moving_side == WHITE ? to + SOUTH : to + NORTH;
}

inline Side Board::get_side(Square square) const {
    return (sides[BLACK] >> square) & uint64_t{1};
}

inline bool Board::has_non_pawn_material(Side side) const {
    return pieces[side][KNIGHT] | pieces[side][BISHOP] |
           pieces[side][ROOK]   | pieces[side][QUEEN];
}

inline bool Board::has_repeated() const {
    // We need at least 2 moves from both sides to get back to the same position
    if (ply < 4 || halfmoves < 4) {
        return false;
    }

    // Halfmove clock resets everytime we make move that permanently
    // alters the board position so we don't need to lock for repeition
    // before the ply that reset the clock
    int last_reversible_ply = std::max(0, ply - halfmoves);

    // Step back by 2 ply each time since we need at least two moves from both sides
    for (int past_ply = ply - 2; past_ply >= last_reversible_ply; past_ply -= 2) {
        if (position_hashes[past_ply] == position_hash) {
            return true;
        }
    }

    return false;
}

// --- Piece Manipulation ---

inline void Board::place_piece(Side side, Piece piece, Square square) {
    // Create a mask based on the square of the piece and use bitwise OR to
    // place the piece on each respective bitboard
    Bitboard mask = get_mask(square);
    pieces[side][piece] |= mask;
    sides[side] |= mask;
    occupied |= mask;

    piece_map[square] = piece;
    if (piece == KING) {
        king_squares[side] = square;
    }

    // XOR piece into hash
    uint64_t zobrist_number = ZOBRIST_PIECES[side][piece][square];
    position_hash ^= zobrist_number;
    if (piece == PAWN) {
        pawn_hash ^= zobrist_number;
    }

    // Update scores
    early_score[side] += EARLY_EVAL_TABLE[side][piece][square];
    late_score[side] += LATE_EVAL_TABLE[side][piece][square];
    game_phase += GAME_PHASE_INCREMENT[piece];
}

inline void Board::remove_piece(Side side, Piece piece, Square square) {
    // Create a mask based on the square of the piece and use bitwise AND to
    // remove the piece from each respective bitboard
    Bitboard mask = ~get_mask(square);
    pieces[side][piece] &= mask;
    sides[side] &= mask;
    occupied &= mask;

    piece_map[square] = NO_PIECE;
    // No need to clear king square here as it will be updated in place_piece

    // XOR piece out of hash
    uint64_t zobrist_number = ZOBRIST_PIECES[side][piece][square];
    position_hash ^= zobrist_number;
    if (piece == PAWN) {
        pawn_hash ^= zobrist_number;
    }

    // Updates score
    early_score[side] -= EARLY_EVAL_TABLE[side][piece][square];
    late_score[side] -= LATE_EVAL_TABLE[side][piece][square];
    game_phase -= GAME_PHASE_INCREMENT[piece];
}

// --- Hash Helpers ---

inline void Board::xor_en_passant() {
    if (en_passant_target != NO_SQUARE) {
        // Only update the hash if we can actually capture on the target square
        // Note: this is a pseudo-legality check as it doesn't accounts for pins/x-rays
        // A strict legality check here would be too expensive and not worth the cost for
        // the engine so we accept occasional (extremely rare) false negatives
        Side capturer = get_rank(en_passant_target) == RANK_3 ? BLACK : WHITE;
        if (PAWN_ATTACK_MAPS[capturer][en_passant_target] & pieces[capturer][PAWN]) {
            position_hash ^= ZOBRIST_EN_PASSANT_TARGETS[get_file(en_passant_target)];
        }
    }
}

inline void Board::xor_castling_rights() {
    position_hash ^= ZOBRIST_CASTLING_RIGHTS[castling_rights];
}

inline void Board::xor_side_to_move() {
    position_hash ^= ZOBRIST_SIDE_TO_MOVE;
}

inline void Board::toggle_side_to_move() {
    to_move = opposite_side(to_move);
    xor_side_to_move();
}

// --- Attack Detection ---

inline bool Board::in_check(Side side) const {
    Side checked_side = side == NO_SIDE ? to_move : side;
    return is_attacked(king_squares[checked_side], opposite_side(checked_side));
}

// Uses piece attack masks to determine if a square is attacked by the given side.
// For non-sliding pieces, we use precomputed attack maps and for sliding pieces
// we generate attack masks. These masks are intersected with the attacker's piece
// bitboards. If there is an intersection (i.e. result is not 0), then the square
// is attacked. We collect all intersections using a union (faster than branching)
// and return the result which is implicitly cast to a boolean.
inline bool Board::is_attacked(Square sq, Side by) const {
    auto& attacker_pieces = pieces[by];
    return (
        // Non-sliding pieces
        (PAWN_ATTACK_MAPS[by][sq] & attacker_pieces[PAWN]) |
        (KNIGHT_ATTACK_MAP[sq] & attacker_pieces[KNIGHT]) |
        (KING_ATTACK_MAP[sq] & attacker_pieces[KING]) |

        // Sliding pieces
        (generate_sliding_attack_mask<ROOK>(sq, occupied) & (attacker_pieces[ROOK] | attacker_pieces[QUEEN])) |
        (generate_sliding_attack_mask<BISHOP>(sq, occupied) & (attacker_pieces[BISHOP] | attacker_pieces[QUEEN]))
    );
}

// Returns a bitboard of all pieces from both sides that attack the given square.
// Takes an explicit occupancy bitboard for use with modified occupancy (e.g. SEE).
inline Bitboard Board::attackers_to(Square sq, Bitboard occupied) const {
    Bitboard all_knights = pieces[WHITE][KNIGHT] | pieces[BLACK][KNIGHT];
    Bitboard all_bishops = pieces[WHITE][BISHOP] | pieces[BLACK][BISHOP];
    Bitboard all_rooks   = pieces[WHITE][ROOK]   | pieces[BLACK][ROOK];
    Bitboard all_queens  = pieces[WHITE][QUEEN]  | pieces[BLACK][QUEEN];
    Bitboard all_kings   = pieces[WHITE][KING]   | pieces[BLACK][KING];
    Bitboard attackers =
        // Non-sliding pieces
        (PAWN_ATTACK_MAPS[WHITE][sq] & pieces[WHITE][PAWN]) |
        (PAWN_ATTACK_MAPS[BLACK][sq] & pieces[BLACK][PAWN]) |
        (KNIGHT_ATTACK_MAP[sq] & all_knights) |
        (KING_ATTACK_MAP[sq]   & all_kings)   |

        // Sliding pieces
        (generate_sliding_attack_mask<ROOK>(sq, occupied)   & (all_rooks   | all_queens)) |
        (generate_sliding_attack_mask<BISHOP>(sq, occupied) & (all_bishops | all_queens));

    // Honor provided occupancy.
    return attackers & occupied;
}

// --- Legality ---

// Checks if a move is legal in the current position. Assumes the move was
// correctly encoded by the move generator from a valid position (e.g. a TT
// or killer move from a previous search). We don't re-validate encoding
// correctness (flag/type combos, etc.) - only whether the move is still
// pseudo-legal and legal given that the board state may have changed.
inline bool Board::is_legal_move(Move move) {
    Side friendly_side = to_move;
    Side enemy_side = opposite_side(friendly_side);
    Square from = move.from();
    Square to = move.to();
    MoveFlag flag = move.flag();
    Bitboard to_mask = get_mask(to);
    Piece piece = piece_map[from];

    // Must be our piece on the from square
    if (piece == NO_PIECE || get_side(from) != friendly_side) return false;

    // Can't capture a king (makes in_check meaningless)
    if (piece_map[to] == KING) return false;

    // --- Castling ---
    // Must have rights, rook in place and ours, path clear,
    // not currently in check, and transit square not attacked.
    // Destination safety is handled by make/unmake + in_check below.
    if (flag == MoveFlag::Castle) {
        if (piece != KING || in_check(friendly_side)) return false;

        if (friendly_side == WHITE && to == G1) {
            if (!(castling_rights & WHITE_SHORT)) return false;
            if (piece_map[H1] != ROOK || get_side(H1) != friendly_side) return false;
            if (piece_map[F1] != NO_PIECE || piece_map[G1] != NO_PIECE) return false;
            if (is_attacked(F1, enemy_side)) return false;

        } else if (friendly_side == WHITE && to == C1) {
            if (!(castling_rights & WHITE_LONG)) return false;
            if (piece_map[A1] != ROOK || get_side(A1) != friendly_side) return false;
            if (piece_map[B1] != NO_PIECE || piece_map[C1] != NO_PIECE || piece_map[D1] != NO_PIECE) return false;
            if (is_attacked(D1, enemy_side)) return false;

        } else if (friendly_side == BLACK && to == G8) {
            if (!(castling_rights & BLACK_SHORT)) return false;
            if (piece_map[H8] != ROOK || get_side(H8) != friendly_side) return false;
            if (piece_map[F8] != NO_PIECE || piece_map[G8] != NO_PIECE) return false;
            if (is_attacked(F8, enemy_side)) return false;

        } else if (friendly_side == BLACK && to == C8) {
            if (!(castling_rights & BLACK_LONG)) return false;
            if (piece_map[A8] != ROOK || get_side(A8) != friendly_side) return false;
            if (piece_map[B8] != NO_PIECE || piece_map[C8] != NO_PIECE || piece_map[D8] != NO_PIECE) return false;
            if (is_attacked(D8, enemy_side)) return false;

        } else {
            return false;
        }

    // --- En passant ---
    // EP target must match, to square must be empty, and enemy pawn must be behind it.
    } else if (flag == MoveFlag::EnPassant) {
        if (piece != PAWN) return false;
        if (to != en_passant_target) return false;
        if (piece_map[to] != NO_PIECE) return false;

        Square cap_sq = en_passant_capture_square(to, friendly_side);
        bool enemy_pawn_behind = piece_map[cap_sq] == PAWN && get_side(cap_sq) == enemy_side;
        if (!enemy_pawn_behind) return false;

    // --- Normal moves and promotions ---
    } else {
        // Capture must target an enemy piece, quiet must target an empty square
        if (move.type() == MoveType::Capture) {
            if (piece_map[to] == NO_PIECE || get_side(to) != enemy_side) return false;
        } else {
            if (piece_map[to] != NO_PIECE) return false;
        }

        // Promotion must be a pawn (unmake_move assumes pawn)
        if (move.is_promotion() && piece != PAWN) return false;

        // Piece geometry - can the piece actually reach 'to' from 'from'?
        if (piece == PAWN) {
            if (move.type() == MoveType::Capture) {
                bool valid_attack = PAWN_ATTACK_MAPS[enemy_side][from] & to_mask;
                if (!valid_attack) return false;
            } else {
                int dir = friendly_side == WHITE ? NORTH : SOUTH;
                Square single_push = from + dir;
                Square double_push = from + dir + dir;
                int start_rank = friendly_side == WHITE ? RANK_2 : RANK_7;

                bool is_single = to == single_push;
                bool is_double = to == double_push
                              && get_rank(from) == start_rank
                              && piece_map[single_push] == NO_PIECE;

                if (!is_single && !is_double) return false;
            }
        } else {
            Bitboard reachable = get_piece_attacks(piece, from, occupied);
            if (!(reachable & to_mask)) return false;
        }
    }

    // Final legality check: make the move and check if it leaves our king in check
    make_move(move);
    bool legal = !in_check(friendly_side);
    unmake_move(move);
    return legal;
}
