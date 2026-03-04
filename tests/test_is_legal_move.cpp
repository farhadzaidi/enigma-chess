#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <filesystem>

#include "core/types.hpp"
#include "board/board.hpp"
#include "core/move.hpp"
#include "move_generator/move_generator.hpp"
#include "utils/notation.hpp"
#include "utils/file_io.hpp"
#include "helpers.hpp"

static bool assert_legal_result(Board& b, Move move, bool expected, const std::string& test_name, const std::string& fen, const std::string& move_uci, const std::string& context) {
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

static bool test_is_legal_move_targeted(Board& b) {
    struct TestCase {
        std::string fen;
        std::string uci;
        bool expected;
        std::string description;
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
        {"4k3/N7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q", false, "promotion flag rejected for non-pawn move"},
        {"r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7a8q", true,  "capture promotion legal"},
        {"4k3/1P6/8/8/8/8/8/4K3 w - - 0 1",  "b7a8q", false, "diagonal quiet promotion illegal"},
        {"4k3/8/8/8/8/8/p7/4K3 b - - 0 1",  "a2a1q", true,  "black quiet promotion legal"},
    };

    for (const auto& tc : tests) {
        b.reset();
        b.load_from_fen(tc.fen);
        Move move = encode_move_from_uci(b, tc.uci);

        if (!assert_legal_result(b, move, tc.expected, "is_legal_move", tc.fen, tc.uci, tc.description)) {
            return false;
        }
    }

    return true;
}

static bool test_is_legal_move_stale_encoded_branches(Board& b) {
    // Stale EN_PASSANT move: encoded in a position with EP target, validated in one without.
    const std::string ep_source_fen = "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1";
    const std::string ep_target_missing_fen = "4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1";
    const std::string ep_target_occupied_fen = "4k3/8/3n4/3pP3/8/8/8/4K3 w - d6 0 1";

    b.reset();
    b.load_from_fen(ep_source_fen);
    Move stale_ep = encode_move_from_uci(b, "e5d6"); // Encoded as MoveType::Capture + MoveFlag::EnPassant.

    b.reset();
    b.load_from_fen(ep_target_missing_fen);
    if (!assert_legal_result(
        b,
        stale_ep,
        false,
        "is_legal_move",
        ep_target_missing_fen,
        "e5d6(ep)",
        "stale EN_PASSANT move rejected when EP target is gone"
    )) {
        return false;
    }

    b.reset();
    b.load_from_fen(ep_target_occupied_fen);
    if (!assert_legal_result(
        b,
        stale_ep,
        false,
        "is_legal_move",
        ep_target_occupied_fen,
        "e5d6(ep)",
        "stale EN_PASSANT move rejected when target square is occupied"
    )) {
        return false;
    }

    // Stale promotion move: encoded for pawn promotion in one position, reused where from-square is non-pawn.
    const std::string promo_source_fen = "4k3/P7/8/8/8/8/8/4K3 w - - 0 1";
    const std::string promo_target_fen = "4k3/N7/8/8/8/8/8/4K3 w - - 0 1";

    b.reset();
    b.load_from_fen(promo_source_fen);
    Move stale_promo = encode_move_from_uci(b, "a7a8q"); // Encoded as MoveType::Quiet + MoveFlag::PromoQueen.

    b.reset();
    b.load_from_fen(promo_target_fen);
    if (!assert_legal_result(
        b,
        stale_promo,
        false,
        "is_legal_move",
        promo_target_fen,
        "a7a8q(stale)",
        "stale promotion move rejected when from-square piece is not pawn"
    )) {
        return false;
    }

    return true;
}

static void append_fens(std::vector<std::string>& out, const std::vector<std::string>& in) {
    for (const std::string& fen : in) {
        if (!fen.empty()) out.push_back(fen);
    }
}

static void append_fens_from_perft_epd(std::vector<std::string>& out, std::filesystem::path path, int max_lines) {
    std::vector<std::string> lines;
    read_file(lines, path, max_lines);
    for (const std::string& line : lines) {
        if (line.empty()) continue;
        out.push_back(parse_perft_epd_line(line).fen);
    }
}

static bool test_is_legal_move_accepts_generated_legal_moves(Board& b) {
    std::vector<std::string> positions = {
        START_POS_FEN,
        KIWIPETE_FEN,
        POSITION_3_FEN,
        POSITION_4_FEN,
        POSITION_5_FEN,
        POSITION_6_FEN,
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1",
        "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
        "4k3/8/8/8/8/8/p7/4K3 b - - 0 1",
    };

    std::vector<std::string> fen_file_lines;
    read_file(fen_file_lines, NOT_IN_CHECK_FEN, 20);
    append_fens(positions, fen_file_lines);

    append_fens_from_perft_epd(positions, SINGLE_CHECK_EPD, 20);
    append_fens_from_perft_epd(positions, DOUBLE_CHECK_EPD, 20);
    append_fens_from_perft_epd(positions, EN_PASSANT_EPD, 20);
    append_fens_from_perft_epd(positions, MIXED_EPD, 20);

    std::unordered_set<std::string> unique_fens;
    for (const std::string& fen : positions) {
        if (!unique_fens.insert(fen).second) continue;

        b.reset();
        b.load_from_fen(fen);

        MoveList legal_moves = generate_moves<MoveGenMode::All>(b);
        for (const Move& move : legal_moves) {
            std::string uci = decode_move_to_uci(move);
            if (!assert_legal_result(b, move, true, "is_legal_move", fen, uci, "generated legal move must be legal")) {
                return false;
            }
        }
    }

    return true;
}

static bool test_is_legal_move_cross_position_rejects_stale_moves(Board& b) {
    // Generate legal white moves from the standard start position.
    // Then validate those same encoded moves in the start position with black to move.
    // Under valid-encoding assumptions this models TT/killer stale-move reuse across positions.
    b.reset();
    b.load_from_fen(START_POS_FEN);
    MoveList stale_moves = generate_moves<MoveGenMode::All>(b);

    const std::string black_to_move_start =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1";
    b.reset();
    b.load_from_fen(black_to_move_start);

    for (const Move& move : stale_moves) {
        std::string uci = decode_move_to_uci(move);
        if (!assert_legal_result(b, move, false, "is_legal_move", black_to_move_start, uci, "stale move from other position should be rejected")) {
            return false;
        }
    }

    return true;
}

bool test_is_legal_move(Board& b) {
    if (!test_is_legal_move_targeted(b)) return false;
    if (!test_is_legal_move_stale_encoded_branches(b)) return false;
    if (!test_is_legal_move_accepts_generated_legal_moves(b)) return false;
    if (!test_is_legal_move_cross_position_rejects_stale_moves(b)) return false;
    return true;
}
