#include <cctype>
#include <sstream>
#include <iostream>
#include <string>
#include <array>

#include "board.hpp"
#include "types.hpp"
#include "move.hpp"
#include "utils.hpp"
#include "move_generator.hpp"

Board::Board() {
    reset();
}

void Board::reset() {
    pieces[WHITE].fill(EMPTY_BITBOARD);
    pieces[BLACK].fill(EMPTY_BITBOARD);
    colors.fill(EMPTY_BITBOARD);

    piece_map.fill(NO_PIECE);
    king_squares.fill(NO_SQUARE);
    positions.fill(0);

    early_score.fill(0);
    late_score.fill(0);
    game_phase = 0;

    occupied = EMPTY_BITBOARD;
    to_move = NO_COLOR;
    castling_rights = NO_CASTLING_RIGHTS;
    en_passant_target = NO_SQUARE;
    halfmoves = 0;
    fullmoves = 0;
    ply = 0;
    zobrist_hash = 0;
}

void Board::load_from_fen(const std::string& fen) {
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
        Color color = std::isupper(c) ? WHITE : BLACK;
        Piece piece;

        switch (std::toupper(c)) {
            case 'P':
                piece = PAWN;
                break;
            case 'B':
                piece = BISHOP;
                break;
            case 'N':
                piece = KNIGHT;
                break;
            case 'R':
                piece = ROOK; 
                break;
            case 'Q':
                piece = QUEEN;
                break;
            case 'K':
                piece = KING;
                break;
        }

        place_piece(color, piece, square);

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
        this->en_passant_target = uci_to_index(en_passant_target);
        xor_en_passant();
    }

    // Halfmoves
    this->halfmoves = std::stoi(halfmoves);

    // Fullmoves
    this->fullmoves = std::stoi(fullmoves);

    // Initialize root position hash
    positions[0] = zobrist_hash;
}

void Board::print_board() const {
    std::string EMPTY_SYMBOL = ".";
    std::array<std::array<std::string, NUM_PIECES>, NUM_COLORS> SYMBOLS = {{
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

            Color color = get_color(square);
            std::clog << SYMBOLS[color][piece] << " ";
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

void Board::print_board_state() const {
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
        std::clog << index_to_uci(en_passant_target);
    }
    std::clog << "\n";
    
    // Move counters
    std::clog << "\tHalfmove clock: " << halfmoves << "\n";
    std::clog << "\tFullmove number: " << fullmoves << "\n";
    std::clog << "\tPly: " << ply << "\n";

    // King positions
    std::clog << "\tWhite king: " << index_to_uci(king_squares[WHITE]) << "\n";
    std::clog << "\tBlack king: " << index_to_uci(king_squares[BLACK]) << "\n";

    // Check status
    std::clog << "\tIn check: " << (in_check() ? "Yes" : "No") << "\n";
    std::clog << "\t-------------------\n\n";
}

void Board::debug() {
    std::string input = "";
    while (true) {
        std::clog << "\n\n============================================================================\n";
        print_board();
        print_board_state();
        std::clog << "============================================================================\n";

        MoveList legal_moves = generate_moves<ALL>(*this);

        std::cin >> input;
        if (input == "quit") {
            break;
        } else if (input == "undo") {
            if (ply > 0) {
                unmake_move(moves[ply - 1]);
            } else {
                std::clog << "Error: Cannot undo move from starting positon.";
            }
        } else {
            Move move = encode_move_from_uci(*this, input);

            bool is_legal_move = false;
            for (Move legal_move : legal_moves) {
                if (move == legal_move) {
                    is_legal_move = true;
                    break;
                }
            }

            if (is_legal_move) {
                make_move(move);
            } else {
                std::clog << "Error: Invalid or illegal move '" << input <<"'";
            }
        }

    }
}

void Board::make_move(Move move) {
    // Preserve irreversible board state before making the move
    State state(en_passant_target, castling_rights, halfmoves, NO_PIECE);

    Square from     = move.from();
    Square to       = move.to();
    MoveType mtype  = move.type();
    MoveFlag mflag  = move.flag();

    int moving_piece = piece_map[from];
    int moving_color = to_move;

    // Update move clocks
    halfmoves++;
    if (moving_piece == PAWN) halfmoves = 0;
    if (moving_color == BLACK) fullmoves++;

    set_en_passant_target(moving_color, moving_piece, from, to);
    remove_piece(moving_color, moving_piece, from);

    // Handle capture logic including en passant
    if (mtype == CAPTURE) {
        state.captured_piece = handle_capture(to, moving_color, mflag);
    }

    // Check if the move is a promotion; if so, update the moving piece
    switch (mflag) {
        case PROMOTION_BISHOP: moving_piece = BISHOP; break;
        case PROMOTION_KNIGHT: moving_piece = KNIGHT; break;
        case PROMOTION_ROOK:   moving_piece = ROOK;   break;
        case PROMOTION_QUEEN:  moving_piece = QUEEN;  break;
    }

    // After changing moving_piece (in the case of a promotion), we can now
    // place the piece on the "to" square
    place_piece(moving_color, moving_piece, to);

    if (mflag == CASTLE) {
        handle_castle(to);
    }

    update_castling_rights(from, to);

    toggle_side_to_move();

    // Update stacks and increment ply
    moves[ply] = move;
    states[ply] = state;
    ply += 1;
    positions[ply] = zobrist_hash;
}

void Board::unmake_move(Move move) {
    Square from     = move.from();
    Square to       = move.to();
    MoveType mtype  = move.type();
    MoveFlag mflag  = move.flag();

    // The color that moved on this move is the opposite of the color that is
    // currently set to move
    Color moving_color = to_move ^ 1;

    // Decrement ply (simulate popping from top of moves and states stacks)
    ply -= 1;

    // Restore state
    const State& prev_state = states[ply];
    en_passant_target = prev_state.en_passant_target;
    castling_rights = prev_state.castling_rights;
    halfmoves = prev_state.halfmoves;

    // Fullmoves is only incremented if black moves, so we decrement it if we
    // are undoing a black move
    if (moving_color == BLACK) {
        fullmoves--;
    }

    // Remove the piece from "to"
    Piece moving_piece = piece_map[to];
    remove_piece(moving_color, moving_piece, to);

    // In the case of a promotion, we change the moving piece to pawn so we place
    // the correct piece back on "from"
    if (move.is_promotion()) {
        moving_piece = PAWN;
    }

    // Put the moving piece back on "from"
    place_piece(moving_color, moving_piece, from);

    // Restore the captured piece
    if (mtype == CAPTURE) {
        Square capture_square = to;
        Color captured_color = moving_color ^ 1;

        if (mflag == EN_PASSANT) {
            // Same logic as before
            capture_square = moving_color == WHITE
                ? capture_square - 8 
                : capture_square + 8;
        }

        place_piece(captured_color, prev_state.captured_piece, capture_square);
    }

    if (mflag == CASTLE) {
        // Determine the correct corner rook based on where the king moved and
        // move it back to its respective corner
        switch (to) {
            case C1: // White long
                remove_piece(moving_color, ROOK, D1);
                place_piece(moving_color, ROOK, A1);
                break;
            case G1: // White short
                remove_piece(moving_color, ROOK, F1);
                place_piece(moving_color, ROOK, H1);
                break;
            case C8: // Black long
                remove_piece(moving_color, ROOK, D8);
                place_piece(moving_color, ROOK, A8);
                break;
            case G8: // Black short
                remove_piece(moving_color, ROOK, F8);
                place_piece(moving_color, ROOK, H8);
                break;
        }
    }

    toggle_side_to_move();

    // Restore hash from history
    // Some of the functions above will modify the hash but it
    // doesn't matter since we'll overwrite it here anyway
    zobrist_hash = positions[ply];
}

bool Board::in_check(Color side) const {
    Color us = side == NO_COLOR ? to_move : side;
    return is_attacked(king_squares[us], us ^ 1);
}

// Uses piece attack masks to determine if a square is attacked by the given color.
// For non-sliding pieces, we use precomputed attack maps and for sliding pieces
// we generate attack masks. These masks are intersected with the attacker's piece
// bitboards. If there is an intersection (i.e. result is not 0), then the square
// is attacked. We collect all intersections using a union (faster than branching)
// and return the result which is implicitly cast to a boolean.
bool Board::is_attacked(Square sq, Color by) const {
    auto& their_pieces = pieces[by];
    return (
        // Non-sliding pieces
        (PAWN_ATTACK_MAPS[by][sq] & their_pieces[PAWN]) |
        (KNIGHT_ATTACK_MAP[sq] & their_pieces[KNIGHT]) |
        (KING_ATTACK_MAP[sq] & their_pieces[KING]) |

        // Sliding pieces
        (generate_sliding_attack_mask<ROOK>(*this, sq) & (their_pieces[ROOK] | their_pieces[QUEEN])) |
        (generate_sliding_attack_mask<BISHOP>(*this, sq) & (their_pieces[BISHOP] | their_pieces[QUEEN]))
    );
}

// Checks if a move is legal in the current position. Assumes the move was
// correctly encoded by the move generator from a valid position (e.g. a TT
// or killer move from a previous search). We don't re-validate encoding
// correctness (flag/type combos, etc.) - only whether the move is still
// pseudo-legal and legal given that the board state may have changed.
bool Board::is_legal_move(Move move) {
    const Color us = to_move;
    const Color them = us ^ 1;
    const Square from = move.from();
    const Square to = move.to();
    const MoveFlag flag = move.flag();
    const Bitboard to_mask = get_mask(to);
    const Piece piece = piece_map[from];

    // Must be our piece on the from square
    if (piece == NO_PIECE || get_color(from) != us) return false;

    // Can't capture a king (makes in_check meaningless)
    if (piece_map[to] == KING) return false;

    // --- Castling ---
    // Must have rights, rook in place and ours, path clear,
    // not currently in check, and transit square not attacked.
    // Destination safety is handled by make/unmake + in_check below.
    if (flag == CASTLE) {
        if (piece != KING || in_check(us)) return false;

        if (us == WHITE && to == G1) {
            if (!(castling_rights & WHITE_SHORT)) return false;
            if (piece_map[H1] != ROOK || get_color(H1) != us) return false;
            if (piece_map[F1] != NO_PIECE || piece_map[G1] != NO_PIECE) return false;
            if (is_attacked(F1, them)) return false;

        } else if (us == WHITE && to == C1) {
            if (!(castling_rights & WHITE_LONG)) return false;
            if (piece_map[A1] != ROOK || get_color(A1) != us) return false;
            if (piece_map[B1] != NO_PIECE || piece_map[C1] != NO_PIECE || piece_map[D1] != NO_PIECE) return false;
            if (is_attacked(D1, them)) return false;

        } else if (us == BLACK && to == G8) {
            if (!(castling_rights & BLACK_SHORT)) return false;
            if (piece_map[H8] != ROOK || get_color(H8) != us) return false;
            if (piece_map[F8] != NO_PIECE || piece_map[G8] != NO_PIECE) return false;
            if (is_attacked(F8, them)) return false;

        } else if (us == BLACK && to == C8) {
            if (!(castling_rights & BLACK_LONG)) return false;
            if (piece_map[A8] != ROOK || get_color(A8) != us) return false;
            if (piece_map[B8] != NO_PIECE || piece_map[C8] != NO_PIECE || piece_map[D8] != NO_PIECE) return false;
            if (is_attacked(D8, them)) return false;

        } else {
            return false;
        }

    // --- En passant ---
    // EP target must match, to square must be empty, and enemy pawn must be behind it.
    } else if (flag == EN_PASSANT) {
        if (piece != PAWN) return false;
        if (to != en_passant_target) return false;
        if (piece_map[to] != NO_PIECE) return false;
        Square cap_sq = us == WHITE ? to + SOUTH : to + NORTH;
        if (piece_map[cap_sq] != PAWN || get_color(cap_sq) != them) return false;

    // --- Normal moves and promotions ---
    } else {
        // Capture must target an enemy piece, quiet must target an empty square
        if (move.type() == CAPTURE) {
            if (piece_map[to] == NO_PIECE || get_color(to) != them) return false;
        } else {
            if (piece_map[to] != NO_PIECE) return false;
        }

        // Promotion must be a pawn (unmake_move assumes pawn)
        if (move.is_promotion() && piece != PAWN) return false;

        // Piece geometry - can the piece actually reach 'to' from 'from'?
        if (piece == PAWN) {
            if (move.type() == CAPTURE) {
                if (!(PAWN_ATTACK_MAPS[them][from] & to_mask)) return false;
            } else {
                int dir = us == WHITE ? NORTH : SOUTH;
                if (to == from + dir) {
                    // Single push - already verified to is empty above
                } else if (to == from + dir + dir) {
                    if (get_rank(from) != (us == WHITE ? RANK_2 : RANK_7)) return false;
                    if (piece_map[from + dir] != NO_PIECE) return false;
                } else {
                    return false;
                }
            }
        } else {
            Bitboard reachable;
            switch (piece) {
                case KNIGHT:
                    reachable = KNIGHT_ATTACK_MAP[from];
                    break;
                case KING:
                    reachable = KING_ATTACK_MAP[from];
                    break;
                case BISHOP:
                    reachable = generate_sliding_attack_mask<BISHOP>(*this, from);
                    break;
                case ROOK:
                    reachable = generate_sliding_attack_mask<ROOK>(*this, from);
                    break;
                case QUEEN:
                    reachable = generate_sliding_attack_mask<BISHOP>(*this, from) |
                                generate_sliding_attack_mask<ROOK>(*this, from);
                    break;
                default:
                    return false;
            }
            if (!(reachable & to_mask)) return false;
        }
    }

    // Final legality check: make the move and check if it leaves our king in check
    make_move(move);
    bool legal = !in_check(us);
    unmake_move(move);
    return legal;
}

bool Board::has_repeated() const {
    // We need at least 2 moves from both sides to get back to the same position
    if (ply < 4 || halfmoves < 4) {
        return false;
    }

    // Halfmove clock resets everytime we make move that permanently
    // alters the board position so we don't need to lock for repeition
    // before the ply that reset the clock
    int last_reversible_ply = std::max(0, ply - halfmoves);
    int previous_position_index = ply - 2;

    // Step back by 2 ply each time since we need at least two moves from both sides
    for (int i = previous_position_index; i >= last_reversible_ply; i -= 2) {
        if (positions[i] == zobrist_hash) {
            return true;
        }
    }

    return false;
}
