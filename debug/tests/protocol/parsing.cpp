#include <string>
#include <iostream>
#include <iterator>

#include "core/types.hpp"
#include "board/board.hpp"
#include "utils/notation.hpp"
#include "move_generator/move_generator.hpp"

namespace {

struct SanTestCase {
    std::string_view fen;
    std::string_view san;
    std::string_view expected_uci;
    std::string_view description;
};

const SanTestCase SAN_TEST_CASES[] = {
    {"rnb1kbnr/pppp1ppp/4p3/6q1/4P3/5K2/PPPP1PPP/RNBQ1BNR b kq - 3 3", "Qg3+", "g5g3", "queen check"},
    {"1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - -", "Qd1+", "d6d1", "queen check on back rank"},
    {"3r1k2/4npp1/1ppr3p/p6P/P2PPPP1/1NR5/5K2/2R5 w - -", "d5", "d4d5", "quiet pawn push"},
    {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "e5", "e7e5", "black pawn push"},
    {"rnbqkb1r/pppp1ppp/5n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3", "Nxe4", "f6e4", "knight capture"},
    {"rnbqkb1r/pppp1ppp/8/4p3/2B1n3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4", "O-O", "e1g1", "white kingside castle"},
    {"rnb1kb1r/pppp1ppp/3n4/4p3/2B4q/5NP1/PPPP1P1P/RNBQ1RK1 w kq - 1 6", "gxh4", "g3h4", "pawn capture"},
    {"rnb1k2r/1p1pbppp/8/pPp1pn2/2B4P/P4N2/2PP1P1P/RNBQ1RK1 w kq a6 0 10", "bxa6", "b5a6", "en passant style SAN capture"},
    {"rn2k2r/P2pbppp/bp6/2p1pn2/2B4P/P4N2/2PP1P1P/RNBQ1RK1 w kq - 1 12", "axb8=Q+", "a7b8q", "capture promotion with check"},
    {"r1b1k2r/1Q2bppp/1p6/2pp1n2/2B1Q2P/P4N2/2Pp1P1P/RNB2RK1 b kq - 1 17", "d1=N", "d2d1n", "underpromotion"},
    {"r1b1k2r/1Q2bppp/1p6/2pp1n2/2B1Q2P/P4N2/2P2P1P/RNBn1RK1 w kq - 0 18", "Nc3", "b1c3", "knight quiet move"},
    {"r3k2r/1Q1bbppp/1p6/1Bpp1n2/1R2Q2P/2N2N2/2Pn1P1P/R1B3K1 w k - 2 24", "Rba4", "b4a4", "file disambiguation"},
    {"4k2r/rQ1bbppp/1p6/1Bpp1n2/R3Q2P/2N2N2/2Pn1P1P/R1B3K1 w k - 4 25", "R4a3", "a4a3", "rank disambiguation"},
    {"4k2r/rQ1bbppp/1p6/1Bpp1n2/R3Q2P/2N2N2/2Pn1P1P/R1B3K1 w k - 4 25", "R1a2", "a1a2", "full-square disambiguation"},
    {"r6r/Q2bbppp/1p1k4/1Bpp1n2/R3Q2P/2N2N2/R1Pn1P1P/2B3K1 b - - 13 29", "Rad8", "a8d8", "rook file disambiguation"},
    {"3r3r/Q2bbppp/1p1k4/1Bpp1n2/R3Q2P/2N2N2/R1Pn1P1P/2B3K1 w - - 14 30", "Kg2", "g1g2", "king move"},
    {"3r3r/Q2bbppp/1p1k4/1Bpp1n2/R3Q2P/2N2N2/R1Pn1PKP/2B5 b - - 15 30", "Rhf8", "h8f8", "rook rank disambiguation"},
    {"3r1r2/Q2bbppp/1p1k4/1Bpp1n2/R3Q2P/2N2N2/R1Pn1PKP/2B5 w - - 16 31", "Qxd5#", "e4d5", "capture checkmate"},
    {"2br3r/Q3bppp/1p1k4/1Bp2n2/3p1R1P/2N3Q1/R1Pn1PKP/2B1N3 w - - 0 38", "Rxd4+", "f4d4", "rook capture check"},
    {"8/8/8/7k/5q2/8/8/7K b - - 3 69", "Qf2", "f4f2", "quiet queen move"},
    {"1q1r3k/3P1pp1/ppBR1n1p/4Q2P/P4P2/8/5PK1/8 w - -", "Rxf6", "d6f6", "rook capture"},
    {"2r3k1/1p2q1pp/2b1pr2/p1pp4/6Q1/1P1PP1R1/P1PN2PP/5RK1 w - - ", "Qxg7 + ", "g4g7", "capture with irregular spacing"},
    {"4r2r/pppkq1pp/2n1pn2/4p1B1/4N2Q/8/PPP3PP/4RRK1 w - -", "Nxf6 +", "e4f6", "capture with spaced check"},
    {"rnq1nrk1/pp3pbp/6p1/3p4/3P4/5N2/PP2BPPP/R1BQK2R w KQ -", "O-O", "e1g1", "castle from opening position"},
};

struct InvalidSanTestCase {
    std::string_view fen;
    std::string_view san;
    std::string_view description;
};

const SanTestCase SAN_EDGE_CASES[] = {
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "O-O-O", "e1c1", "white queenside castle"},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "0-0", "e8g8", "black kingside castle with zero notation"},
    {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", "exf6 e.p.", "e5f6", "en passant with suffix"},
};

const InvalidSanTestCase INVALID_SAN_CASES[] = {
    {START_POS_FEN, "Qh5", "illegal queen move from start position"},
    {"4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "Nd2", "ambiguous knight move without disambiguation"},
    {START_POS_FEN, "Nxf3", "capture marker when destination is empty"},
    {START_POS_FEN, "Qa9", "invalid destination square"},
};

bool test_valid_san_cases(Board& b, const SanTestCase* tests, size_t count, const std::string& section_name) {
    for (size_t i = 0; i < count; i++) {
        const SanTestCase& test = tests[i];
        // Load position
        b.reset();
        b.load_from_fen(test.fen);

        // Parse SAN
        Move move = parse_move_from_san(b, test.san);

        // Check if move is NULL
        if (move == NULL_MOVE) {
            std::clog << "[FAILURE] 'san_parsing' - Failed to parse SAN\n";
            std::clog << "Section: " << section_name << "\n";
            std::clog << "Case: " << test.description << "\n";
            std::clog << "FEN: " << test.fen << "\n";
            std::clog << "SAN: " << test.san << "\n";
            std::clog << "Expected UCI: " << test.expected_uci << "\n";
            return false;
        }

        // Convert to UCI
        std::string uci = decode_move_to_uci(move);

        // Compare with expected
        if (uci != test.expected_uci) {
            std::clog << "[FAILURE] 'san_parsing' - UCI mismatch\n";
            std::clog << "Section: " << section_name << "\n";
            std::clog << "Case: " << test.description << "\n";
            std::clog << "FEN: " << test.fen << "\n";
            std::clog << "SAN: " << test.san << "\n";
            std::clog << "Expected UCI: " << test.expected_uci << "\n";
            std::clog << "Got UCI: " << uci << "\n";
            return false;
        }

        // SAN parser should always return a legal move in the given position.
        MoveList legal_moves = generate_moves<MoveGenMode::All>(b);
        bool is_legal = false;
        for (const Move& legal : legal_moves) {
            if (move == legal) {
                is_legal = true;
                break;
            }
        }
        if (!is_legal) {
            std::clog << "[FAILURE] 'san_parsing' - Parsed move is illegal\n";
            std::clog << "Section: " << section_name << "\n";
            std::clog << "Case: " << test.description << "\n";
            std::clog << "FEN: " << test.fen << "\n";
            std::clog << "SAN: " << test.san << "\n";
            std::clog << "Move: " << decode_move_to_uci(move) << "\n";
            return false;
        }
    }

    return true;
}

bool test_invalid_san_cases(Board& b) {
    for (const auto& test : INVALID_SAN_CASES) {
        b.reset();
        b.load_from_fen(test.fen);

        Move move = parse_move_from_san(b, test.san);
        if (move != NULL_MOVE) {
            std::clog << "[FAILURE] 'san_parsing' - Invalid SAN should return NULL_MOVE\n";
            std::clog << "Case: " << test.description << "\n";
            std::clog << "FEN: " << test.fen << "\n";
            std::clog << "SAN: " << test.san << "\n";
            std::clog << "Got: " << decode_move_to_uci(move) << "\n";
            return false;
        }
    }

    return true;
}

} // namespace

bool test_san_parsing(Board& b) {
    if (!test_valid_san_cases(b, SAN_TEST_CASES, std::size(SAN_TEST_CASES), "core")) return false;
    if (!test_valid_san_cases(b, SAN_EDGE_CASES, std::size(SAN_EDGE_CASES), "edge")) return false;
    if (!test_invalid_san_cases(b)) return false;

    // All tests passed
    return true;
}
