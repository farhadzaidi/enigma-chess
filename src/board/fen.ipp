// FEN string loading

inline Piece char_to_piece(char c) {
    switch (std::toupper(c)) {
        case 'P': return PAWN;   case 'B': return BISHOP;
        case 'N': return KNIGHT; case 'R': return ROOK;
        case 'Q': return QUEEN;  case 'K': return KING;
        default:  return NO_PIECE;
    }
}

inline void Board::load_from_fen(const std::string& fen) {
    // Reset the board before loading from FEN
    reset();

    std::vector<std::string> parts;
    std::istringstream iss(fen);
    std::string item;

    // Split the fen string using a space as the delimiter
    while(std::getline(iss, item, ' ')) {
        // Skip empty strings caused by multiple spaces
        if (!item.empty()) {
            parts.push_back(item);
        }
    }

    std::string position            = parts[0];
    std::string to_move             = parts.size() > 1 ? parts[1] : "w";
    std::string castling_rights     = parts.size() > 2 ? parts[2] : "-";
    std::string en_passant_target   = parts.size() > 3 ? parts[3] : "-";
    std::string halfmoves           = parts.size() > 4 ? parts[4] : "0";
    std::string fullmoves           = parts.size() > 5 ? parts[5] : "1";

    // Set up position starting from top left
    int rank = 7;
    int file = 0;
    for (char c : position) {
        // End of rank; go down one
        if (c == '/') {
            rank -= 1;
            file = 0;
            continue;
        }

        // Number indicating how many empty squares in the file until the next piece
        if (std::isdigit(c)) {
            file += c - '0';
            continue;
        }

        // Must be a piece
        Square square = get_square(rank, file);
        Side side = std::isupper(c) ? WHITE : BLACK;
        place_piece(side, char_to_piece(c), square);

        file++;
    }

    // Side to move
    if (to_move == "w") {
        this->to_move = WHITE;
    } else {
        this->to_move = BLACK;
        xor_side_to_move(); // Toggle side to move in hash if black
    }

    // Castling rights
    for (char c : castling_rights) {
        switch (c) {
            case 'K':
                this->castling_rights |= WHITE_SHORT;
                break;
            case 'Q':
                this->castling_rights |= WHITE_LONG;
                break;
            case 'k':
                this->castling_rights |= BLACK_SHORT;
                break;
            case 'q':
                this->castling_rights |= BLACK_LONG;
                break;
        }
    }

    // Update hash with castling rights
    xor_castling_rights();

    // En passant target square
    if (en_passant_target != "-") {
        this->en_passant_target = notation_to_square(en_passant_target);
        xor_en_passant();
    }

    // Halfmoves
    this->halfmoves = std::stoi(halfmoves);

    // Fullmoves
    this->fullmoves = std::stoi(fullmoves);

    // Initialize root position hash
    position_hashes[0] = position_hash;
    pawn_hashes[0] = pawn_hash;
}
