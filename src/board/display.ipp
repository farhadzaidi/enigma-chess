// Board display: print_board, print_board_state

inline void Board::print_board() const {
    std::string EMPTY_SYMBOL = ".";
    std::array<std::array<std::string, NUM_PIECES>, NUM_SIDES> PIECE_SYMBOLS = {{
        { "♟", "♝", "♞", "♜", "♛", "♚" },
        { "♙", "♗", "♘", "♖", "♕", "♔" }
    }};
    std::array<std::string, BOARD_SIZE> FILES = {
        "a", "b", "c", "d", "e", "f", "g", "h"
    };

    std::clog << "\n";

    // Loop through the board top to bottom, left to right
    for (int rank = BOARD_SIZE - 1; rank >= 0; rank--) {
        std::clog << "\t" << rank + 1 << "  "; // Print ranks on the side

        for (int file = 0; file < BOARD_SIZE; file++) {
            Square square = get_square(rank, file);
            Piece piece = piece_map[square];
            if (piece == NO_PIECE) {
                std::clog << EMPTY_SYMBOL << " ";
                continue;
            }

            Side side = get_side(square);
            std::clog << PIECE_SYMBOLS[side][piece] << " ";
        }

        // Move onto the next rank
        std::clog << "\n";
    }

    // Print files at the bottom
    std::clog << "\n\t   ";
    for (int file = 0; file < BOARD_SIZE; file++) {
        std::clog << FILES[file] << " ";
    }
    std::clog << "\n\n";
}

inline void Board::print_board_state() const {
    std::clog << "\t--- Board State ---\n";
    std::clog << "\tSide to move: " << (to_move == WHITE ? "White" : "Black") << "\n";

    // Castling rights
    std::clog << "\tCastling rights: ";
    if (castling_rights == NO_CASTLING_RIGHTS) {
        std::clog << "-";
    } else {
        if (castling_rights & WHITE_SHORT) std::clog << "K";
        if (castling_rights & WHITE_LONG) std::clog << "Q";
        if (castling_rights & BLACK_SHORT) std::clog << "k";
        if (castling_rights & BLACK_LONG) std::clog << "q";
    }
    std::clog << "\n";

    // En passant target
    std::clog << "\tEn passant: ";
    if (en_passant_target == NO_SQUARE) {
        std::clog << "-";
    } else {
        std::clog << square_to_notation(en_passant_target);
    }
    std::clog << "\n";

    // Move counters
    std::clog << "\tHalfmove clock: " << halfmoves << "\n";
    std::clog << "\tFullmove number: " << fullmoves << "\n";
    std::clog << "\tPly: " << ply << "\n";

    // King positions
    std::clog << "\tWhite king: " << square_to_notation(king_squares[WHITE]) << "\n";
    std::clog << "\tBlack king: " << square_to_notation(king_squares[BLACK]) << "\n";

    // Check status
    std::clog << "\tIn check: " << (in_check() ? "Yes" : "No") << "\n";
    std::clog << "\t-------------------\n\n";
}
