#include <iostream>
#include <string_view>

#include "types.hpp"
#include "board.hpp"
#include "move_generator.hpp"
#include "perft.hpp"
#include "tests/helpers.hpp"

namespace {

// Sanity check perft against well-known CPW positions at low depths.
// The proper perft test suite (higher depths, more positions) lives in the movegen bench.
bool test_perft_sanity(Board& b) {
    struct TestCase {
        std::string_view fen;
        int depth;
        uint64_t expected_nodes;
        std::string_view description;
    };

    TestCase test_cases[] = {
        // Start position
        {START_POS_FEN, 1, 20, "startpos depth 1"},
        {START_POS_FEN, 2, 400, "startpos depth 2"},
        {START_POS_FEN, 3, 8902, "startpos depth 3"},
        {START_POS_FEN, 4, 197281, "startpos depth 4"},

        // Kiwipete - heavy with castling, en passant, promotions
        {KIWIPETE_FEN, 1, 48, "kiwipete depth 1"},
        {KIWIPETE_FEN, 2, 2039, "kiwipete depth 2"},
        {KIWIPETE_FEN, 3, 97862, "kiwipete depth 3"},

        // Position 3 - pins and pawn structure
        {POSITION_3_FEN, 1, 14, "position 3 depth 1"},
        {POSITION_3_FEN, 2, 191, "position 3 depth 2"},
        {POSITION_3_FEN, 3, 2812, "position 3 depth 3"},

        // Position 4 - promotions and captures
        {POSITION_4_FEN, 1, 6, "position 4 depth 1"},
        {POSITION_4_FEN, 2, 264, "position 4 depth 2"},
        {POSITION_4_FEN, 3, 9467, "position 4 depth 3"},

        // Position 5
        {POSITION_5_FEN, 1, 44, "position 5 depth 1"},
        {POSITION_5_FEN, 2, 1486, "position 5 depth 2"},
        {POSITION_5_FEN, 3, 62379, "position 5 depth 3"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        uint64_t nodes = perft<false>(b, tc.depth);

        if (nodes != tc.expected_nodes) {
            std::clog << "[FAILURE] 'perft_sanity' - Node count mismatch\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            std::clog << "Expected: " << tc.expected_nodes << " Got: " << nodes << "\n";
            return false;
        }
    }

    return true;
}

// Phased move generation (TacticalOnly + QuietOnly) should produce the same
// move set as All mode across diverse positions.
bool test_perft_phased_consistency(Board& b) {
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
        "4k3/8/8/4Q3/8/8/8/4K3 b - - 0 1",
    };

    for (std::string_view fen : positions) {
        b.load_from_fen(fen);

        // Depth 3 is enough to exercise phased generation across several plies
        uint64_t all_nodes = perft<false>(b, 3);
        uint64_t phased_nodes = perft_phased(b, 3);

        if (all_nodes != phased_nodes) {
            std::clog << "[FAILURE] 'perft_phased_consistency' - Phased and all-mode perft disagree\n";
            std::clog << "FEN: " << fen << "\n";
            std::clog << "All: " << all_nodes << " Phased: " << phased_nodes << "\n";
            return false;
        }
    }

    return true;
}

} // namespace

bool test_perft(Board& b) {
    if (!test_perft_sanity(b)) return false;
    if (!test_perft_phased_consistency(b)) return false;
    return true;
}
