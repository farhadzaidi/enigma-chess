#include "utils/notation.hpp"

#include <cctype>
#include <cstdlib>

#include "bitboard.hpp"
#include "move_generator.hpp"

class ParsedSan {
public:
    bool is_castling_kingside = false;
    bool is_castling_queenside = false;
    Piece piece_type = PAWN;
    Square to_square = NO_SQUARE;
    bool is_capture = false;
    MoveFlag promotion_flag = MF_NORMAL;
    int from_file = -1;
    int from_rank = -1;
};

namespace {

// --- SAN Helpers ---

/** Strip annotations/whitespace and normalize castling notation */
std::string normalize_san(std::string_view san) {
    std::string result;

    for (char c : san) {
        if (std::isspace(c) || c == '+' || c == '#' || c == '!' || c == '?') {
            continue;
        }
        result += c;
    }

    std::string lower = result;
    for (char& c : lower) c = std::tolower(c);

    size_t ep_pos = lower.find("e.p.");
    if (ep_pos == std::string::npos) {
        ep_pos = lower.find("ep");
    }
    if (ep_pos != std::string::npos) {
        result = result.substr(0, ep_pos);
    }

    if (lower == "o-o" || lower == "0-0") {
        result = "O-O";
    } else if (lower == "o-o-o" || lower == "0-0-0") {
        result = "O-O-O";
    }

    return result;
}

/** Decompose a normalized SAN string into its structural components */
ParsedSan parse_san_components(const std::string& san) {
    ParsedSan parsed;

    if (san.empty()) {
        return parsed;
    }

    if (san == "O-O") {
        parsed.is_castling_kingside = true;
        parsed.piece_type = KING;
        return parsed;
    }
    if (san == "O-O-O") {
        parsed.is_castling_queenside = true;
        parsed.piece_type = KING;
        return parsed;
    }

    size_t index = 0;

    if (index < san.length() && std::isupper(san[index])) {
        switch (std::toupper(san[index])) {
            case 'K': parsed.piece_type = KING; break;
            case 'Q': parsed.piece_type = QUEEN; break;
            case 'R': parsed.piece_type = ROOK; break;
            case 'B': parsed.piece_type = BISHOP; break;
            case 'N': parsed.piece_type = KNIGHT; break;
        }
        index++;
    }

    size_t promotion_pos = san.find('=');
    size_t dest_start = (promotion_pos != std::string::npos) ? promotion_pos - 2 : san.length() - 2;

    if (san.length() < 2 || dest_start >= san.length() || dest_start + 2 > san.length()) {
        return parsed;
    }

    std::string dest_square = san.substr(dest_start, 2);
    if (dest_square.length() == 2 && dest_square[0] >= 'a' && dest_square[0] <= 'h' &&
        dest_square[1] >= '1' && dest_square[1] <= '8') {
        parsed.to_square = uci_to_index(dest_square);
    } else {
        return parsed;
    }

    if (promotion_pos != std::string::npos && promotion_pos + 1 < san.length()) {
        char promo_piece = std::toupper(san[promotion_pos + 1]);
        switch (promo_piece) {
            case 'Q': parsed.promotion_flag = MF_PROMO_QUEEN; break;
            case 'R': parsed.promotion_flag = MF_PROMO_ROOK; break;
            case 'B': parsed.promotion_flag = MF_PROMO_BISHOP; break;
            case 'N': parsed.promotion_flag = MF_PROMO_KNIGHT; break;
        }
    }

    if (san.find('x') != std::string::npos) {
        parsed.is_capture = true;
    }

    std::string middle;
    if (index < dest_start) {
        for (size_t i = index; i < dest_start && i < san.length(); i++) {
            if (san[i] != 'x') {
                middle += san[i];
            }
        }
    }

    if (!middle.empty()) {
        if (middle.length() == 1 && middle[0] >= 'a' && middle[0] <= 'h') {
            parsed.from_file = middle[0] - 'a';
        } else if (middle.length() == 1 && middle[0] >= '1' && middle[0] <= '8') {
            parsed.from_rank = middle[0] - '1';
        } else if (middle.length() == 2) {
            if (middle[0] >= 'a' && middle[0] <= 'h') {
                parsed.from_file = middle[0] - 'a';
            }
            if (middle[1] >= '1' && middle[1] <= '8') {
                parsed.from_rank = middle[1] - '1';
            }
        }
    }

    return parsed;
}

} // namespace

// --- Notation Helpers ---

/** Convert a UCI square string (e.g. "e4") to a square index */
Square uci_to_index(std::string_view square) {
    int rank = square[1] - '1';
    int file = square[0] - 'a';

    return get_square(rank, file);
}

/** Convert a square index to a UCI square string (e.g. "e4") */
std::string index_to_uci(Square square) {
    char rank = get_rank(square) + '1';
    char file = get_file(square) + 'a';

    return std::string{file, rank};
}

// --- UCI Move Conversion ---

/** Build a Move from a UCI string, inferring castling/en-passant from board state */
Move encode_move_from_uci(const Board& b, std::string_view uci_move) {
    Square from = uci_to_index(uci_move.substr(0, 2));
    Square to = uci_to_index(uci_move.substr(2, 2));

    MoveType move_type = MT_QUIET;
    if (b.piece_map()[to] != NO_PIECE) {
        move_type = MT_CAPTURE;
    }

    MoveFlag move_flag = MF_NORMAL;

    if (uci_move.length() == 5) {
        switch (uci_move[4]) {
            case 'b': move_flag = MF_PROMO_BISHOP; break;
            case 'n': move_flag = MF_PROMO_KNIGHT; break;
            case 'r': move_flag = MF_PROMO_ROOK; break;
            case 'q': move_flag = MF_PROMO_QUEEN; break;
        }
    } else if (b.piece_map()[from] == KING && std::abs(from - to) == 2) {
        move_flag = MF_CASTLE;
    } else if (b.piece_map()[from] == PAWN && to == b.en_passant_target()) {
        move_flag = MF_EN_PASSANT;
        move_type = MT_CAPTURE;
    }

    return Move(from, to, move_type, move_flag);
}

/** Convert a Move to its UCI string representation */
std::string decode_move_to_uci(Move move) {
    if (move == NULL_MOVE) return "0000";

    std::string from = index_to_uci(move.from());
    std::string to = index_to_uci(move.to());

    std::string promotion = "";
    switch (move.flag()) {
        case MF_PROMO_BISHOP: promotion = "b"; break;
        case MF_PROMO_KNIGHT: promotion = "n"; break;
        case MF_PROMO_ROOK: promotion = "r"; break;
        case MF_PROMO_QUEEN: promotion = "q"; break;
        default: break;
    }

    return from + to + promotion;
}

// --- SAN Move Parsing ---

/** Parse a SAN string into a Move by generating all legal moves and matching */
Move parse_move_from_san(Board& b, std::string_view san) {
    std::string normalized = normalize_san(san);
    ParsedSan parsed = parse_san_components(normalized);

    if (parsed.is_castling_kingside || parsed.is_castling_queenside) {
        Square king_to;

        if (parsed.is_castling_kingside) {
            king_to = b.to_move() == WHITE ? G1 : G8;
        } else {
            king_to = b.to_move() == WHITE ? C1 : C8;
        }

        parsed.to_square = king_to;
    }

    MoveGenerator move_generator(b);
    MoveList legal_moves = move_generator.generate_all();

    Move candidate = NULL_MOVE;
    int num_matches = 0;

    for (const Move& move : legal_moves) {
        Piece moving_piece = b.piece_map()[move.from()];
        if (moving_piece != parsed.piece_type) {
            continue;
        }

        if (move.to() != parsed.to_square) {
            continue;
        }

        if (parsed.is_capture && move.type() != MT_CAPTURE) {
            continue;
        }
        if (!parsed.is_capture && move.type() == MT_CAPTURE) {
            continue;
        }

        if (parsed.promotion_flag != MF_NORMAL && move.flag() != parsed.promotion_flag) {
            continue;
        }
        if (parsed.promotion_flag == MF_NORMAL && move.is_promotion()) {
            continue;
        }

        if ((parsed.is_castling_kingside || parsed.is_castling_queenside) && move.flag() != MF_CASTLE) {
            continue;
        }

        if (parsed.from_file != -1 && get_file(move.from()) != parsed.from_file) {
            continue;
        }
        if (parsed.from_rank != -1 && get_rank(move.from()) != parsed.from_rank) {
            continue;
        }

        candidate = move;
        num_matches++;
    }

    if (num_matches != 1) {
        return NULL_MOVE;
    }

    return candidate;
}
