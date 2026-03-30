#include <iostream>
#include <string_view>

#include "types.hpp"
#include "board.hpp"
#include "notation.hpp"
#include "move_generator.hpp"
#include "tests/helpers.hpp"

namespace {

// Incremental accumulator updates should produce the same eval as a full refresh.
// After making a move (incremental), compare eval to a board loaded from the resulting FEN (full refresh).
bool test_nnue_incremental_vs_refresh(Board& b) {
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
            "quiet pawn double push"
        },
        {
            START_POS_FEN,
            "g1f3",
            "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1",
            "quiet knight move"
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
            "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1",
            "e1c1",
            "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/2KR3R b kq - 1 1",
            "white long castle"
        },
        {
            "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R4RK1 b kq - 1 1",
            "e8g8",
            "r4rk1/pppppppp/8/8/8/8/PPPPPPPP/R4RK1 w - - 2 2",
            "black short castle"
        },
        {
            "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
            "e5d6",
            "4k3/8/3P4/8/8/8/8/4K3 b - - 0 1",
            "en passant capture"
        },
        {
            "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
            "a7a8q",
            "Q3k3/8/8/8/8/8/8/4K3 b - - 0 1",
            "queen promotion"
        },
        {
            "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
            "a7a8n",
            "N3k3/8/8/8/8/8/8/4K3 b - - 0 1",
            "knight underpromotion"
        },
        {
            "1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1",
            "a7b8q",
            "1Q2k3/8/8/8/8/8/8/4K3 b - - 0 1",
            "capture + queen promotion"
        },
        {
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
            "e1d1",
            "4k3/8/8/8/8/8/8/3K4 b - - 1 1",
            "quiet king move"
        },
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.pre_fen);
        Move move = encode_move_from_uci(b, tc.uci);
        b.make_move(move);
        PositionScore incremental_eval = b.nnue_evaluate();

        Board rebuilt;
        rebuilt.load_from_fen(tc.post_fen);
        PositionScore refresh_eval = rebuilt.nnue_evaluate();

        ASSERT_EQ(incremental_eval, refresh_eval, "nnue_incremental_vs_refresh",
            "Eval mismatch after " << tc.description << "\n"
            << "  Pre FEN: " << tc.pre_fen << " Move: " << tc.uci << "\n"
            << "  Post FEN: " << tc.post_fen)
    }

    return true;
}

// Make/unmake should restore NNUE eval to the exact same value
bool test_nnue_make_unmake_eval(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view uci;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {START_POS_FEN, "e2e4", "quiet pawn push"},
        {START_POS_FEN, "g1f3", "quiet knight move"},
        {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", "e4d5", "pawn capture"},
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "e1g1", "white short castle"},
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", "en passant"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", "queen promotion"},
        {"1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q", "capture + promotion"},
        {"4k3/8/8/8/8/8/8/4K3 w - - 0 1", "e1d1", "king move"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        PositionScore before_eval = b.nnue_evaluate();

        Move move = encode_move_from_uci(b, tc.uci);
        b.make_move(move);
        b.unmake_move(move);

        PositionScore after_eval = b.nnue_evaluate();

        ASSERT_EQ(after_eval, before_eval, "nnue_make_unmake_eval",
            "Eval not restored after make/unmake\n"
            << "  Case: " << tc.description << "\n"
            << "  FEN: " << tc.fen << " Move: " << tc.uci)
    }

    return true;
}

// Make/unmake across all legal moves from diverse positions should preserve eval
bool test_nnue_make_unmake_all_legal(Board& b) {
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
    };

    for (std::string_view fen : positions) {
        b.load_from_fen(fen);
        PositionScore original_eval = b.nnue_evaluate();

        MoveGenerator move_generator(b);
        MoveList moves = move_generator.generate_all();
        for (const Move& move : moves) {
            b.make_move(move);
            b.unmake_move(move);

            PositionScore restored_eval = b.nnue_evaluate();
            if (restored_eval != original_eval) {
                std::clog << "[FAILURE] 'nnue_make_unmake_all_legal' - Eval not restored\n";
                std::clog << "  FEN: " << fen << "\n";
                std::clog << "  Move: " << decode_move_to_uci(move) << "\n";
                std::clog << "  Expected: " << original_eval << "  Got: " << restored_eval << "\n";
                return false;
            }
        }
    }

    return true;
}

// Same FEN loaded twice should produce identical eval
bool test_nnue_determinism(Board& b) {
    std::string_view positions[] = {
        START_POS_FEN,
        KIWIPETE_FEN,
        POSITION_3_FEN,
        POSITION_4_FEN,
        "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
    };

    for (std::string_view fen : positions) {
        b.load_from_fen(fen);
        PositionScore eval1 = b.nnue_evaluate();

        b.load_from_fen(fen);
        PositionScore eval2 = b.nnue_evaluate();

        ASSERT_EQ(eval1, eval2, "nnue_determinism",
            "Different eval for same FEN: " << fen)
    }

    return true;
}

// Eval should stay within reasonable bounds for non-decisive positions
bool test_nnue_sanity_bounds(Board& b) {
    struct TestCase {
        std::string_view fen;
        PositionScore max_abs;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {START_POS_FEN, 300, "starting position"},
        {KIWIPETE_FEN, 1000, "complex middlegame"},
        {POSITION_6_FEN, 1000, "symmetric middlegame"},
        {"4k3/8/8/8/8/8/8/4K3 w - - 0 1", 300, "bare kings"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        PositionScore eval = b.nnue_evaluate();

        ASSERT(eval > -tc.max_abs && eval < tc.max_abs, "nnue_sanity_bounds",
            tc.description << ": eval " << eval << " exceeds +/-" << tc.max_abs)
    }

    return true;
}

// Multi-move sequence: incremental updates through a game should match fresh load
bool test_nnue_incremental_sequence(Board& b) {
    // Play the Italian Game: 1.e4 e5 2.Nf3 Nc6 3.Bc4
    b.load_from_fen(START_POS_FEN);
    std::string_view moves[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4"};
    for (std::string_view uci : moves) {
        Move move = encode_move_from_uci(b, uci);
        b.make_move(move);
    }
    PositionScore incremental_eval = b.nnue_evaluate();

    Board rebuilt;
    rebuilt.load_from_fen("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3");
    PositionScore refresh_eval = rebuilt.nnue_evaluate();

    ASSERT_EQ(incremental_eval, refresh_eval, "nnue_incremental_sequence",
        "Eval mismatch after 5-move Italian Game sequence")

    return true;
}

} // namespace

bool test_nnue(Board& b) {
    if (!test_nnue_incremental_vs_refresh(b)) return false;
    if (!test_nnue_make_unmake_eval(b)) return false;
    if (!test_nnue_make_unmake_all_legal(b)) return false;
    if (!test_nnue_determinism(b)) return false;
    if (!test_nnue_sanity_bounds(b)) return false;
    if (!test_nnue_incremental_sequence(b)) return false;
    return true;
}
