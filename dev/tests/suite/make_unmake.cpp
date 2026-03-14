#include <iostream>
#include <string_view>

#include "types.hpp"
#include "board.hpp"
#include "move_generator.hpp"
#include "utils/notation.hpp"
#include "tests/helpers.hpp"

namespace {

// make/unmake should restore full board state for all move types
bool test_make_unmake_restore(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view uci;
        std::string_view description;
    };

    TestCase test_cases[] = {
        // Quiet moves
        {START_POS_FEN, "e2e4", "quiet pawn double push"},
        {START_POS_FEN, "e2e3", "quiet pawn single push"},
        {START_POS_FEN, "g1f3", "quiet knight move"},
        {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "d7d5", "quiet pawn push (black)"},
        {"r1bqkbnr/pppppppp/2n5/8/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", "f1b5", "quiet bishop move"},

        // Captures
        {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", "e4d5", "pawn capture"},
        {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4", "f3e5", "knight captures pawn"},
        // Castling
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "e1g1", "white short castle"},
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "e1c1", "white long castle"},
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1", "e8g8", "black short castle"},
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R b KQkq - 0 1", "e8c8", "black long castle"},

        // En passant
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", "en passant (white)"},
        {"4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1", "e4d3", "en passant (black)"},

        // Promotions
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", "queen promotion"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8n", "knight underpromotion"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8r", "rook underpromotion"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8b", "bishop underpromotion"},

        // Capture + promotion
        {"1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q", "capture + queen promotion"},

        // Castling rights changes
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "a1b1", "rook move loses queenside castling"},
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "h1g1", "rook move loses kingside castling"},
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "e1d1", "king move loses all castling"},

        // Complex middlegame positions
        {KIWIPETE_FEN, "e2a6", "kiwipete bishop capture"},
        {POSITION_3_FEN, "b4b1", "position 3 rook move"},
        {POSITION_4_FEN, "c4c5", "position 4 pawn push"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        Board before = b;

        Move move = encode_move_from_uci(b, tc.uci);
        b.make_move(move);
        b.unmake_move(move);

        ASSERT_BOARD(b, before, "make_unmake_restore",
            "Board state not restored\n"
            << "Case: " << tc.description << "\n"
            << "FEN: " << tc.fen << " Move: " << tc.uci)
    }

    return true;
}

// make/unmake across all legal moves from several diverse positions
bool test_make_unmake_all_legal_moves(Board& b) {
    std::string_view positions[] = {
        START_POS_FEN,
        KIWIPETE_FEN,
        POSITION_3_FEN,
        POSITION_4_FEN,
        POSITION_5_FEN,
        POSITION_6_FEN,
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "4k3/P7/8/8/8/8/p7/4K3 w - - 0 1",
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
    };

    for (std::string_view fen : positions) {
        b.load_from_fen(fen);
        Board before = b;

        MoveGenerator move_generator(b);
        MoveList moves = move_generator.generate_all();
        for (const Move& move : moves) {
            b.make_move(move);
            b.unmake_move(move);

            if (!board_position_equal(b, before)) {
                std::clog << "[FAILURE] 'make_unmake_all_legal_moves' - Board state not restored\n";
                std::clog << "FEN: " << fen << "\n";
                std::clog << "Move: " << decode_move_to_uci(move) << "\n";
                return false;
            }
        }
    }

    return true;
}

// Incremental score updates should match a fresh load_from_fen of the resulting position
bool test_make_unmake_incremental_scores(Board& b) {
    struct TestCase {
        std::string_view pre_fen;
        std::string_view uci;
        std::string_view post_fen;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {
            START_POS_FEN,
            "e2e4",
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
            "quiet pawn push"
        },
        {
            "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2",
            "e4d5",
            "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2",
            "pawn capture"
        },
        {
            "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1",
            "e1g1",
            "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R4RK1 b kq - 1 1",
            "white short castle"
        },
        {
            "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
            "e5d6",
            "4k3/8/3P4/8/8/8/8/4K3 b - - 0 1",
            "en passant"
        },
        {
            "1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1",
            "a7b8q",
            "1Q2k3/8/8/8/8/8/8/4K3 b - - 0 1",
            "capture + queen promotion"
        },
        {
            "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
            "a7a8n",
            "N3k3/8/8/8/8/8/8/4K3 b - - 0 1",
            "knight underpromotion"
        },
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.pre_fen);
        Move move = encode_move_from_uci(b, tc.uci);
        b.make_move(move);

        Board rebuilt;
        rebuilt.load_from_fen(tc.post_fen);

        bool match = (
            b.early_scores()[WHITE] == rebuilt.early_scores()[WHITE]
            && b.early_scores()[BLACK] == rebuilt.early_scores()[BLACK]
            && b.late_scores()[WHITE] == rebuilt.late_scores()[WHITE]
            && b.late_scores()[BLACK] == rebuilt.late_scores()[BLACK]
            && b.game_phase() == rebuilt.game_phase()
        );

        if (!match) {
            std::clog << "[FAILURE] 'make_unmake_incremental_scores' - Score mismatch after make_move\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "Pre FEN: " << tc.pre_fen << " Move: " << tc.uci << "\n";
            std::clog << "Post FEN: " << tc.post_fen << "\n";
            std::clog << "Incremental early[W]/[B]: " << b.early_scores()[WHITE] << "/" << b.early_scores()[BLACK]
                      << " Rebuilt: " << rebuilt.early_scores()[WHITE] << "/" << rebuilt.early_scores()[BLACK] << "\n";
            std::clog << "Incremental late[W]/[B]: " << b.late_scores()[WHITE] << "/" << b.late_scores()[BLACK]
                      << " Rebuilt: " << rebuilt.late_scores()[WHITE] << "/" << rebuilt.late_scores()[BLACK] << "\n";
            std::clog << "Incremental game_phase: " << b.game_phase()
                      << " Rebuilt: " << rebuilt.game_phase() << "\n";
            return false;
        }
    }

    return true;
}

// Multi-move sequences should maintain consistent incremental state
bool test_make_unmake_sequence(Board& b) {
    b.load_from_fen(START_POS_FEN);
    Board original = b;

    // Play the Italian Game: 1.e4 e5 2.Nf3 Nc6 3.Bc4
    std::string_view moves[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4"};
    Move encoded[5];

    for (int i = 0; i < 5; i++) {
        encoded[i] = encode_move_from_uci(b, moves[i]);
        b.make_move(encoded[i]);
    }

    // Unmake all moves in reverse
    for (int i = 4; i >= 0; i--) {
        b.unmake_move(encoded[i]);
    }

    ASSERT_BOARD(b, original, "make_unmake_sequence",
        "Board not restored after 5-move sequence make/unmake")

    return true;
}

} // namespace

bool test_make_unmake(Board& b) {
    if (!test_make_unmake_restore(b)) return false;
    if (!test_make_unmake_all_legal_moves(b)) return false;
    if (!test_make_unmake_incremental_scores(b)) return false;
    if (!test_make_unmake_sequence(b)) return false;
    return true;
}
