#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>

#include "types.hpp"
#include "move.hpp"
#include "board.hpp"
#include "move_generator.hpp"
#include "notation.hpp"
#include "tests/helpers.hpp"

namespace {

bool assert_legal_result(Board& b, Move move, bool expected, std::string_view test_name, std::string_view fen, std::string_view move_uci, std::string_view context) {
    Board before = b;
    bool actual = b.is_legal_move(move);

    if (!board_position_equal(before, b)) {
        std::clog << "[FAILURE] '" << test_name << "' - Board mutated during legality check\n";
        std::clog << "Case: " << context << "\n";
        std::clog << "FEN: " << fen << "\n";
        std::clog << "Move: " << move_uci << "\n";
        return false;
    }

    if (actual != expected) {
        std::clog << "[FAILURE] '" << test_name << "' - Wrong legality result\n";
        std::clog << "Case: " << context << "\n";
        std::clog << "FEN: " << fen << "\n";
        std::clog << "Move: " << move_uci << "\n";
        std::clog << "Expected: " << (expected ? "legal" : "illegal")
                  << " Got: " << (actual ? "legal" : "illegal") << "\n";
        return false;
    }

    return true;
}

bool test_legal_targeted(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view uci;
        bool expected;
        std::string_view description;
    };

    TestCase tests[] = {
        // Basic piece geometry and occupancy
        {START_POS_FEN, "e2e4", true,  "start: pawn double push legal"},
        {START_POS_FEN, "e2e5", false, "start: illegal pawn jump"},
        {START_POS_FEN, "e2f3", false, "start: pawn cannot capture empty square"},
        {"4k3/8/8/8/8/4B3/4P3/4K3 w - - 0 1", "e2e4", false, "double pawn push blocked on intermediate square"},
        {START_POS_FEN, "b1c3", true,  "start: knight legal"},
        {START_POS_FEN, "b1b3", false, "start: knight illegal geometry"},
        {"4k3/8/8/8/8/2B5/8/4K3 w - - 0 1", "c3b5", false, "encoded knight move rejected when from has bishop"},
        {START_POS_FEN, "c1h6", false, "start: bishop blocked by own pawn"},
        {START_POS_FEN, "a1a3", false, "start: rook blocked by own pawn"},
        {START_POS_FEN, "d1h5", false, "start: queen blocked by own pawn"},

        // Pins and king safety
        {"4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1", "e2d2", false, "pinned rook cannot expose king"},
        {"4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1", "e2e8", true,  "pinned rook can capture checking rook"},
        {"4k3/8/8/8/8/8/4r3/4K3 w - - 0 1",   "e1d1", true,  "king can move out of check"},
        {"4k3/8/8/8/8/8/8/4K1r1 w - - 0 1",   "e1f1", false, "king cannot move into check"},

        // Castling legality
        {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", true,  "white short castle legal"},
        {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1", true,  "white long castle legal"},
        {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8", true,  "black short castle legal"},
        {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8", true,  "black long castle legal"},
        {"r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1",    "e1g1", false, "castle blocked by missing rights"},
        {"4k3/8/8/8/8/8/8/4KB1r w K - 0 1",      "e1g1", false, "castle rejected when corner rook is enemy"},
        {"4r1k1/8/8/8/8/8/8/R3K2R w KQ - 0 1",   "e1g1", false, "castle illegal while in check"},
        {"4k3/8/8/8/8/8/5r2/R3K2R w KQ - 0 1",   "e1g1", false, "castle through attacked square"},
        {"4k3/8/8/8/8/8/6r1/R3K2R w KQ - 0 1",   "e1g1", false, "castle into attacked square"},
        {"4k3/8/8/8/8/8/3r4/R3K2R w KQ - 0 1",   "e1c1", false, "long castle through attacked square"},

        // En passant legality
        {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", true,  "en passant legal"},
        {"4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1",  "e5d6", false, "en passant missing target"},
        {"4k3/8/3n4/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", false, "en passant target occupied"},
        {"k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", false, "en passant exposes own king"},
        {"4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1", "e4d3", true,  "black en passant legal"},

        // Promotions
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", true,  "quiet promotion legal"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8n", true,  "underpromotion legal"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8", false, "promotion without suffix rejected"},
        {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a6q", false, "promotion flag rejected off back rank"},
        {"4k3/N7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", false, "promotion flag rejected for non-pawn move"},
        {"r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7a8q", true,  "capture promotion legal"},
        {"r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7a8", false, "capture promotion without suffix rejected"},
        {"4k3/1P6/8/8/8/8/8/4K3 w - - 0 1",  "b7a8q", false, "diagonal quiet promotion illegal"},
        {"4k3/8/8/8/8/8/p7/4K3 b - - 0 1",  "a2a1q", true,  "black quiet promotion legal"},
        {"4k3/8/8/8/8/8/p7/4K3 b - - 0 1",  "a2a1", false, "black promotion without suffix rejected"},
        {"4k3/8/8/8/8/8/p7/4K3 b - - 0 1",  "a2a3q", false, "black promotion flag rejected off back rank"},
    };

    for (const auto& tc : tests) {
        b.reset();
        b.load_from_fen(tc.fen);
        Move move = encode_move_from_uci(b, tc.uci);

        if (!assert_legal_result(b, move, tc.expected, "legal", tc.fen, tc.uci, tc.description)) {
            return false;
        }
    }

    return true;
}

bool test_legal_stale_encoded_branches(Board& b) {
    // Stale EN_PASSANT move: encoded in a position with EP target, validated in one without.
    const std::string ep_source_fen = "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1";
    const std::string ep_target_missing_fen = "4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1";
    const std::string ep_target_occupied_fen = "4k3/8/3n4/3pP3/8/8/8/4K3 w - d6 0 1";

    b.reset();
    b.load_from_fen(ep_source_fen);
    Move stale_ep = encode_move_from_uci(b, "e5d6");

    b.reset();
    b.load_from_fen(ep_target_missing_fen);
    if (!assert_legal_result(
        b, stale_ep, false, "legal",
        ep_target_missing_fen, "e5d6(ep)",
        "stale EN_PASSANT move rejected when EP target is gone"
    )) {
        return false;
    }

    b.reset();
    b.load_from_fen(ep_target_occupied_fen);
    if (!assert_legal_result(
        b, stale_ep, false, "legal",
        ep_target_occupied_fen, "e5d6(ep)",
        "stale EN_PASSANT move rejected when target square is occupied"
    )) {
        return false;
    }

    // Stale promotion move: encoded for pawn promotion in one position, reused where from-square is non-pawn.
    const std::string promo_source_fen = "4k3/P7/8/8/8/8/8/4K3 w - - 0 1";
    const std::string promo_target_fen = "4k3/N7/8/8/8/8/8/4K3 w - - 0 1";

    b.reset();
    b.load_from_fen(promo_source_fen);
    Move stale_promo = encode_move_from_uci(b, "a7a8q");

    b.reset();
    b.load_from_fen(promo_target_fen);
    if (!assert_legal_result(
        b, stale_promo, false, "legal",
        promo_target_fen, "a7a8q(stale)",
        "stale promotion move rejected when from-square piece is not pawn"
    )) {
        return false;
    }

    const std::string quiet_source_fen = "4k3/8/8/8/8/8/2k5/4K3 b - - 0 1";
    const std::string quiet_target_fen = "8/8/5k2/7p/8/5K2/2p5/8 b - - 1 55";

    b.reset();
    b.load_from_fen(quiet_source_fen);
    Move stale_quiet = encode_move_from_uci(b, "c2c1");

    b.reset();
    b.load_from_fen(quiet_target_fen);
    if (!assert_legal_result(
        b, stale_quiet, false, "legal",
        quiet_target_fen, "c2c1(stale)",
        "stale quiet move rejected when destination requires promotion"
    )) {
        return false;
    }

    return true;
}

bool test_legal_accepts_generated_legal_moves(Board& b) {
    std::string_view positions[] = {
        START_POS_FEN,
        KIWIPETE_FEN,
        POSITION_3_FEN,
        POSITION_4_FEN,
        POSITION_5_FEN,
        POSITION_6_FEN,

        // Castling, en passant, promotion
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1",
        "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
        "4k3/8/8/8/8/8/p7/4K3 b - - 0 1",

        // Check evasions
        "rnb2rk1/pp2bppp/4pn2/2P1N3/2p5/2N3P1/PP2PPBP/R1BqK2R w KQ - 0 1",
        "3Rb3/4r3/5k2/1p3p1p/2p2N2/P3K1P1/1P5P/8 w - - 0 1",
        "8/5kpp/8/8/1p3P2/6PP/r3KP2/1R1q4 w - - 0 1",

        // En passant positions
        "r7/pp1k1pp1/2nPp3/6q1/2Pp1N2/5b2/PP1Q1P2/2K1RB2 b - c3 0 1",
        "1rbq1rk1/1p2ppbp/p2p1np1/n1pP2B1/2P5/1PN2NP1/P3PPBP/R2Q1RK1 w - c6 0 1",

        // Complex positions
        "3k1b2/p4r2/1P6/P1p1pnpP/2P5/R4B1R/6N1/4K3 b - - 0 1",
        "4r3/5b2/p3Pn2/P1kP4/2p4p/7N/2BK2P1/3N2R1 w - - 0 1",
    };

    for (std::string_view fen : positions) {
        b.reset();
        b.load_from_fen(fen);

        MoveGenerator move_generator(b);
        MoveList legal_moves = move_generator.generate_all();
        for (const Move& move : legal_moves) {
            std::string uci = decode_move_to_uci(move);
            if (!assert_legal_result(b, move, true, "legal", fen, uci, "generated legal move must be accepted")) {
                return false;
            }
        }
    }

    return true;
}

bool test_legal_cross_position_rejects_stale_moves(Board& b) {
    b.reset();
    b.load_from_fen(START_POS_FEN);
    MoveGenerator move_generator(b);
    MoveList stale_moves = move_generator.generate_all();

    const std::string black_to_move_start =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1";
    b.reset();
    b.load_from_fen(black_to_move_start);

    for (const Move& move : stale_moves) {
        std::string uci = decode_move_to_uci(move);
        if (!assert_legal_result(b, move, false, "legal", black_to_move_start, uci, "stale move from other position should be rejected")) {
            return false;
        }
    }

    return true;
}

} // namespace

bool test_legal(Board& b) {
    if (!test_legal_targeted(b)) return false;
    if (!test_legal_stale_encoded_branches(b)) return false;
    if (!test_legal_accepts_generated_legal_moves(b)) return false;
    if (!test_legal_cross_position_rejects_stale_moves(b)) return false;
    return true;
}
