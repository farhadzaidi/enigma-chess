// Move making/unmaking: make_move, unmake_move, null moves, and their helpers

inline void Board::set_en_passant_target(Side side, Piece piece, Square from, Square to) {

    // XOR out previous EP file (if any)
    xor_en_passant();

    // Reset EP target square (will be set below if needed)
    en_passant_target = NO_SQUARE;

    // White moves a pawn 2 squares north
    if (
        side == WHITE
        && piece == PAWN
        && get_rank(from) == RANK_2
        && get_rank(to) == RANK_4
    ) {
        en_passant_target = to - 8; // Directly behind the white pawn (south)
    }

    // Black moves a pawn 2 squares south
    else if (
        side == BLACK
        && piece == PAWN
        && get_rank(from) == RANK_7
        && get_rank(to) == RANK_5
    ) {
        en_passant_target = to + 8; // Directly behind the black pawn (north)
    }

    // XOR in new EP file (if any)
    xor_en_passant();
}


inline Piece Board::handle_capture(Square capture_square, Side moving_side, MoveFlag move_flag) {
    halfmoves = 0;
    Side captured_side = opposite_side(moving_side);

    if (move_flag == MoveFlag::EnPassant) {
        capture_square = en_passant_capture_square(capture_square, moving_side);
    }

    Piece captured_piece = piece_map[capture_square];
    remove_piece(captured_side, captured_piece, capture_square);
    return captured_piece;
}

inline void Board::handle_castle(Square castle_square) {
    switch (castle_square) {
        case C1: // White long castle
            remove_piece(WHITE, ROOK, A1);
            place_piece(WHITE, ROOK, D1);
            break;
        case G1: // White short castle
            remove_piece(WHITE, ROOK, H1);
            place_piece(WHITE, ROOK, F1);
            break;
        case C8: // Black long castle
            remove_piece(BLACK, ROOK, A8);
            place_piece(BLACK, ROOK, D8);
            break;
        case G8: // Black short castle
            remove_piece(BLACK, ROOK, H8);
            place_piece(BLACK, ROOK, F8);
            break;
    }
}

inline void Board::undo_castle(Square castle_square) {
    switch (castle_square) {
        case C1: remove_piece(WHITE, ROOK, D1); place_piece(WHITE, ROOK, A1); break;
        case G1: remove_piece(WHITE, ROOK, F1); place_piece(WHITE, ROOK, H1); break;
        case C8: remove_piece(BLACK, ROOK, D8); place_piece(BLACK, ROOK, A8); break;
        case G8: remove_piece(BLACK, ROOK, F8); place_piece(BLACK, ROOK, H8); break;
    }
}

inline void Board::update_castling_rights(Square from, Square to) {
    // XOR out old castling rights
    xor_castling_rights();

    // Use precomputed lookup table to update castling rights
    castling_rights &= ~castling_rights_updates[from];
    castling_rights &= ~castling_rights_updates[to];

    // XOR in new castling rights
    xor_castling_rights();
}

inline void Board::make_move(Move move) {
    // Preserve irreversible board state before making the move
    UndoState state(en_passant_target, castling_rights, halfmoves, NO_PIECE);

    Square from     = move.from();
    Square to       = move.to();
    MoveType move_type  = move.type();
    MoveFlag move_flag  = move.flag();

    Piece moving_piece = piece_map[from];
    Side moving_side = to_move;

    // Update move clocks
    halfmoves++;
    if (moving_piece == PAWN) halfmoves = 0;
    if (moving_side == BLACK) fullmoves++;

    set_en_passant_target(moving_side, moving_piece, from, to);
    remove_piece(moving_side, moving_piece, from);

    // Handle capture logic including en passant
    if (move_type == MoveType::Capture) {
        state.captured_piece = handle_capture(to, moving_side, move_flag);
    }

    // If the move is a promotion, update the moving piece to the promoted type
    if (move.is_promotion()) {
        moving_piece = get_promoted_piece(move_flag);
    }

    // After changing moving_piece (in the case of a promotion), we can now
    // place the piece on the "to" square
    place_piece(moving_side, moving_piece, to);

    if (move_flag == MoveFlag::Castle) {
        handle_castle(to);
    }

    update_castling_rights(from, to);

    toggle_side_to_move();

    // Update stacks and increment ply
    move_history[ply] = move;
    state_history[ply] = state;
    ply += 1;
    position_hashes[ply] = position_hash;
    pawn_hashes[ply] = pawn_hash;
}

inline void Board::unmake_move(Move move) {
    Square from     = move.from();
    Square to       = move.to();
    MoveType move_type  = move.type();
    MoveFlag move_flag  = move.flag();

    // The side that moved on this move is the opposite of the current side
    Side moving_side = opposite_side(to_move);

    // Decrement ply (simulate popping from top of moves and states stacks)
    ply -= 1;

    // Restore state
    const UndoState& prev_state = state_history[ply];
    en_passant_target = prev_state.en_passant_target;
    castling_rights = prev_state.castling_rights;
    halfmoves = prev_state.halfmoves;

    // Fullmoves is only incremented if black moves, so we decrement it if we
    // are undoing a black move
    if (moving_side == BLACK) {
        fullmoves--;
    }

    // Remove the piece from "to"
    Piece moving_piece = piece_map[to];
    remove_piece(moving_side, moving_piece, to);

    // In the case of a promotion, we change the moving piece to pawn so we place
    // the correct piece back on "from"
    if (move.is_promotion()) {
        moving_piece = PAWN;
    }

    // Put the moving piece back on "from"
    place_piece(moving_side, moving_piece, from);

    // Restore the captured piece
    if (move_type == MoveType::Capture) {
        Square capture_sq = move_flag == MoveFlag::EnPassant
            ? en_passant_capture_square(to, moving_side)
            : to;
        place_piece(opposite_side(moving_side), prev_state.captured_piece, capture_sq);
    }

    if (move_flag == MoveFlag::Castle) {
        undo_castle(to);
    }

    toggle_side_to_move();

    // Restore hash from history
    // Some of the functions above will modify the hash but it
    // doesn't matter since we'll overwrite it here anyway
    position_hash = position_hashes[ply];
    pawn_hash = pawn_hashes[ply];
}

// Skips the current side's turn and updates board state accordingly
// Useful for implementing null move reductions in search
inline void Board::make_null_move() {
    // Preserve irreversible board state for unmake_null_move
    state_history[ply] = UndoState(en_passant_target, castling_rights, halfmoves, NO_PIECE);

    // Clear en passant
    xor_en_passant();
    en_passant_target = NO_SQUARE;

    // Toggle side to move and increment ply
    toggle_side_to_move();
    ply++;

    // Update hash stacks
    position_hashes[ply] = position_hash;
    pawn_hashes[ply] = pawn_hash;
}

inline void Board::unmake_null_move() {
    ply -= 1;

    // We only need to bring back the en passant target from the previous
    // state since we didn't change anything else
    const UndoState& prev_state = state_history[ply];
    en_passant_target = prev_state.en_passant_target;

    // Toggle side back and revert hashes
    toggle_side_to_move();
    position_hash = position_hashes[ply];
    pawn_hash = pawn_hashes[ply];
}
