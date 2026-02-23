#include <iostream>
#include <string>

#include "types.hpp"
#include "board.hpp"
#include "utils.hpp"
#include "move_generator.hpp"

// Computes expected material for each side by counting pieces in a FEN string
static void compute_material_from_fen(const std::string& fen, int& white_material, int& black_material) {
    white_material = 0;
    black_material = 0;

    // Only parse the position part (before first space)
    for (char c : fen) {
        if (c == ' ') break;
        if (c == '/' || std::isdigit(c)) continue;

        int value = 0;
        switch (std::toupper(c)) {
            case 'P': value = PIECE_VALUE[PAWN]; break;
            case 'N': value = PIECE_VALUE[KNIGHT]; break;
            case 'B': value = PIECE_VALUE[BISHOP]; break;
            case 'R': value = PIECE_VALUE[ROOK]; break;
            case 'Q': value = PIECE_VALUE[QUEEN]; break;
            case 'K': value = 0; break;
        }

        if (std::isupper(c)) white_material += value;
        else black_material += value;
    }
}

// Verify material is correct after loading known positions
static bool test_material_positions(Board& b) {
    const char* positions[] = {
        START_POS_FEN,
        KIWIPETE_FEN,
        POSITION_3_FEN,
        POSITION_4_FEN,
        POSITION_5_FEN,
        POSITION_6_FEN,
    };

    for (const char* fen : positions) {
        b.load_from_fen(fen);

        int expected_white, expected_black;
        compute_material_from_fen(fen, expected_white, expected_black);

        if (b.material[WHITE] != expected_white || b.material[BLACK] != expected_black) {
            std::clog << "[FAILURE] 'material_positions' - Material mismatch\n";
            std::clog << "FEN: " << fen << "\n";
            std::clog << "White: " << b.material[WHITE] << " (expected " << expected_white << ")\n";
            std::clog << "Black: " << b.material[BLACK] << " (expected " << expected_black << ")\n";
            return false;
        }
    }

    return true;
}

// Captures of different piece types should subtract the correct value
static bool test_material_captures(Board& b) {
    struct TestCase {
        std::string fen;
        std::string uci;
        bool expect_capture;
        Piece expected_captured_piece;
        Color expected_captured_color;
        std::string description;
    };

    TestCase test_cases[] = {
        // Pawn capture
        {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2",
         "e4d5", true, PAWN, BLACK, "pawn captures pawn"},
        // Knight capture
        {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
         "f3e5", true, PAWN, BLACK, "knight captures pawn"},
        // Bishop capture
        {"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/8/PPPP1PPP/RNBQK1NR w KQkq - 2 3",
         "c4f7", true, PAWN, BLACK, "bishop captures pawn"},
        // Rook captures rook
        {"r3k3/8/8/8/8/8/8/R3K3 w - - 0 1",
         "a1a8", true, ROOK, BLACK, "rook captures rook"},
        // Quiet move control case
        {"rnbqkbnr/ppppp1pp/5p2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
         "d1h5", false, NO_PIECE, NO_COLOR, "queen moves (no capture, control)"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        Move move = encode_move_from_uci(b, tc.uci);

        bool is_legal = false;
        MoveList legal_moves = generate_moves<ALL>(b);
        for (const Move& legal : legal_moves) {
            if (legal == move) {
                is_legal = true;
                break;
            }
        }
        if (!is_legal) {
            std::clog << "[FAILURE] 'material_captures' - Illegal fixture move\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << " Move: " << tc.uci << "\n";
            return false;
        }

        if ((move.type() == CAPTURE) != tc.expect_capture) {
            std::clog << "[FAILURE] 'material_captures' - Move capture classification mismatch\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "Move: " << tc.uci << " Encoded capture: " << (move.type() == CAPTURE) << "\n";
            return false;
        }

        int white_before = b.material[WHITE];
        int black_before = b.material[BLACK];

        Piece captured_piece = move.type() == CAPTURE ? b.piece_map[move.to()] : NO_PIECE;
        if (move.type() == CAPTURE && captured_piece != tc.expected_captured_piece) {
            std::clog << "[FAILURE] 'material_captures' - Wrong captured piece type in fixture\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "Expected captured piece: " << static_cast<int>(tc.expected_captured_piece)
                      << " Got: " << static_cast<int>(captured_piece) << "\n";
            return false;
        }

        b.make_move(move);

        int expected_white = white_before;
        int expected_black = black_before;
        if (move.type() == CAPTURE) {
            if (tc.expected_captured_color == WHITE) {
                expected_white -= PIECE_VALUE[captured_piece];
            } else {
                expected_black -= PIECE_VALUE[captured_piece];
            }
        }

        if (b.material[WHITE] != expected_white || b.material[BLACK] != expected_black) {
            std::clog << "[FAILURE] 'material_captures' - Wrong material after " << tc.description << "\n";
            std::clog << "White: " << b.material[WHITE] << " (expected " << expected_white << ")\n";
            std::clog << "Black: " << b.material[BLACK] << " (expected " << expected_black << ")\n";
            return false;
        }
    }

    return true;
}

// Unmake capture should restore both sides' material
static bool test_material_unmake(Board& b) {
    struct TestCase {
        std::string fen;
        std::string uci;
        std::string description;
    };

    TestCase test_cases[] = {
        {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2",
         "e4d5", "pawn capture unmake"},
        {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
         "f3e5", "knight capture unmake"},
        {"r3k3/8/8/8/8/8/8/R3K3 w - - 0 1",
         "a1a8", "rook capture unmake"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        int white_before = b.material[WHITE];
        int black_before = b.material[BLACK];

        Move move = encode_move_from_uci(b, tc.uci);
        b.make_move(move);
        b.unmake_move(move);

        if (b.material[WHITE] != white_before || b.material[BLACK] != black_before) {
            std::clog << "[FAILURE] 'material_unmake' - Material not restored after " << tc.description << "\n";
            std::clog << "White: " << b.material[WHITE] << " (expected " << white_before << ")\n";
            std::clog << "Black: " << b.material[BLACK] << " (expected " << black_before << ")\n";
            return false;
        }
    }

    return true;
}

// All promotion types should update material correctly
static bool test_material_promotions(Board& b) {
    struct TestCase {
        std::string fen;
        std::string uci;
        Piece promoted_to;
        std::string description;
    };

    TestCase test_cases[] = {
        {"8/P7/8/8/8/8/1k4K1/8 w - - 0 1", "a7a8q", QUEEN, "queen promotion"},
        {"8/P7/8/8/8/8/1k4K1/8 w - - 0 1", "a7a8r", ROOK, "rook promotion"},
        {"8/P7/8/8/8/8/1k4K1/8 w - - 0 1", "a7a8b", BISHOP, "bishop promotion"},
        {"8/P7/8/8/8/8/1k4K1/8 w - - 0 1", "a7a8n", KNIGHT, "knight promotion"},
        // Black promotion
        {"8/1k4K1/8/8/8/8/p7/8 b - - 0 1", "a2a1q", QUEEN, "black queen promotion"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        Color mover = b.to_move;
        int before = b.material[mover];

        Move move = encode_move_from_uci(b, tc.uci);
        b.make_move(move);

        int expected = before + PIECE_VALUE[tc.promoted_to] - PIECE_VALUE[PAWN];
        if (b.material[mover] != expected) {
            std::clog << "[FAILURE] 'material_promotions' - Wrong material after " << tc.description << "\n";
            std::clog << "Expected: " << expected << " Got: " << b.material[mover] << "\n";
            return false;
        }

        b.unmake_move(move);

        if (b.material[mover] != before) {
            std::clog << "[FAILURE] 'material_promotions' - Material not restored after unmake " << tc.description << "\n";
            return false;
        }
    }

    return true;
}

// Capture + promotion should reflect both changes
static bool test_material_capture_promotion(Board& b) {
    // White pawn on a7 captures black knight on b8 and promotes to queen
    b.load_from_fen("1n6/P7/8/8/8/8/1k4K1/8 w - - 0 1");

    int white_before = b.material[WHITE];
    int black_before = b.material[BLACK];

    Move move = encode_move_from_uci(b, "a7b8q");
    b.make_move(move);

    int expected_white = white_before + PIECE_VALUE[QUEEN] - PIECE_VALUE[PAWN];
    int expected_black = black_before - PIECE_VALUE[KNIGHT];

    if (b.material[WHITE] != expected_white || b.material[BLACK] != expected_black) {
        std::clog << "[FAILURE] 'material_capture_promotion' - Wrong material\n";
        std::clog << "White: " << b.material[WHITE] << " (expected " << expected_white << ")\n";
        std::clog << "Black: " << b.material[BLACK] << " (expected " << expected_black << ")\n";
        return false;
    }

    b.unmake_move(move);

    if (b.material[WHITE] != white_before || b.material[BLACK] != black_before) {
        std::clog << "[FAILURE] 'material_capture_promotion' - Material not restored after unmake\n";
        return false;
    }

    return true;
}

// En passant capture and unmake should update material correctly
static bool test_material_en_passant(Board& b) {
    b.load_from_fen("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");

    int white_before = b.material[WHITE];
    int black_before = b.material[BLACK];

    Move move = encode_move_from_uci(b, "e5f6");
    b.make_move(move);

    if (b.material[BLACK] != black_before - PIECE_VALUE[PAWN]) {
        std::clog << "[FAILURE] 'material_en_passant' - Material not updated for en passant capture\n";
        return false;
    }

    b.unmake_move(move);

    if (b.material[WHITE] != white_before || b.material[BLACK] != black_before) {
        std::clog << "[FAILURE] 'material_en_passant' - Material not restored after unmake en passant\n";
        return false;
    }

    return true;
}

bool test_material(Board& b) {
    if (!test_material_positions(b)) return false;
    if (!test_material_captures(b)) return false;
    if (!test_material_unmake(b)) return false;
    if (!test_material_promotions(b)) return false;
    if (!test_material_capture_promotion(b)) return false;
    if (!test_material_en_passant(b)) return false;
    return true;
}
