#include <iostream>

#include "types.hpp"
#include "board.hpp"
#include "engine.hpp"
#include "notation.hpp"
#include "move_generator.hpp"
#include "tests/helpers.hpp"

namespace {

Engine g_test_engine;

// --- Game end tests ---

bool test_no_legal_moves(Board& b) {
    struct TestCase {
        std::string fen;
        std::string description;
    };

    TestCase test_cases[] = {
        // Checkmate positions
        {"rnb1kbnr/pppp1ppp/4p3/8/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3", "fool's mate (white checkmated)"},
        {"3k1R2/8/3K4/8/8/8/8/8 b - - 1 1", "back rank mate (black checkmated)"},
        {"7k/6Q1/6K1/8/8/8/8/8 b - - 0 1", "queen + king mate (black checkmated)"},
        // Stalemate positions
        {"k7/1R6/K7/8/8/8/8/8 b - - 0 1", "stalemate (black has no moves)"},
        {"8/8/8/8/8/5k2/5p2/5K2 w - - 0 1", "stalemate (white king trapped)"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        MoveGenerator move_generator(b);
        MoveList moves = move_generator.generate_all();
        if (!moves.is_empty()) {
            std::clog << "[FAILURE] 'no_legal_moves' - Position has legal moves but should have none\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            return false;
        }

        Move best = g_test_engine.sync_search_depth(b,1).move;

        if (best != NULL_MOVE) {
            std::clog << "[FAILURE] 'no_legal_moves' - Expected NULL_MOVE\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            std::clog << "Got: " << decode_move_to_uci(best) << "\n";
            return false;
        }
    }

    return true;
}

bool test_mate_in_1(Board& b) {
    struct TestCase {
        std::string fen;
        std::string expected_uci;
        std::string description;
    };

    TestCase test_cases[] = {
        // White Qe8#: king trapped behind own pawns
        {"6k1/5ppp/8/8/8/8/8/4Q2K w - - 0 1", "e1e8", "back rank mate"},
        // White Qxf7#: classic scholar's mate
        {"r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", "h5f7", "scholar's mate"},
        // Black Qb2#: black king a3 supports the queen on b2
        {"8/8/8/8/8/qk6/8/1K6 b - - 0 1", "a3b2", "queen + king mate"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        Move best = g_test_engine.sync_search_depth(b,2).move;
        std::string uci = decode_move_to_uci(best);

        if (uci != tc.expected_uci) {
            std::clog << "[FAILURE] 'mate_in_1' - Wrong move\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            std::clog << "Expected: " << tc.expected_uci << " Got: " << uci << "\n";
            return false;
        }
    }

    return true;
}

bool test_mate_in_2(Board& b) {
    struct TestCase {
        std::string fen;
        std::string expected_uci;
        std::string description;
    };

    TestCase test_cases[] = {
        // White Qf7+, black king forced to c8, then Qe8#/Qc7#
        {"3k4/8/1K6/7Q/8/8/8/8 w - - 0 1", "h5f7", "queen + king forced mate in 2"},
        // White Qg1+, black king forced to h4, then Qg4#/Qh2#
        {"8/8/1Q6/5K2/8/7k/8/8 w - - 0 1", "b6g1", "queen + king forced mate in 2"},
        // White Kf4: black pawn on a5 does not prevent the quiet mating net
        {"8/8/8/p7/7k/4K3/8/6Q1 w - - 0 1", "e3f4", "quiet mate in 2 against king + pawn"},
        // White Kg6: black pawn on g5 still leaves only one quiet mating net
        {"7k/8/8/5QpK/8/8/8/8 w - - 0 1", "h5g6", "quiet mate in 2 against king + pawn"},
        // White Kc5: defender pawn on e3 does not stop the quiet net
        {"8/8/3K4/k7/8/4p3/4Q3/8 w - - 0 1", "d6c5", "quiet mate in 2 with extra defender pawn"},
        // White Kf2: black pawn on b4 still allows a unique quiet mate in 2
        {"8/8/8/Q7/1p6/4K3/7k/8 w - - 0 1", "e3f2", "quiet mate in 2 with edge pawn defender"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        Move best = g_test_engine.sync_search_depth(b,4).move;
        std::string uci = decode_move_to_uci(best);

        if (uci != tc.expected_uci) {
            std::clog << "[FAILURE] 'mate_in_2' - Wrong move\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            std::clog << "Expected: " << tc.expected_uci << " Got: " << uci << "\n";
            return false;
        }
    }

    return true;
}

bool test_engine_finds_stalemate(Board& b) {
    b.load_from_fen("5r2/8/8/8/K7/3q4/k7/Q3b3 b - - 0 1");

    Move best = g_test_engine.sync_search_depth(b,2).move;
    std::string uci = decode_move_to_uci(best);

    if (uci != "a2a1") {
        std::clog << "[FAILURE] 'engine_finds_stalemate' - Engine failed to find forced stalemate move\n";
        std::clog << "FEN: 5r2/8/8/8/K7/3q4/k7/Q3b3 b - - 0 1\n";
        std::clog << "Expected: a2a1 Got: " << uci << "\n";
        return false;
    }

    b.make_move(best);
    MoveGenerator move_generator(b);
    MoveList moves = move_generator.generate_all();
    ASSERT(moves.is_empty() && !b.in_check(), "engine_finds_stalemate", "Expected stalemate after move a2a1");

    return true;
}

bool test_engine_avoids_stalemate(Board& b) {
    b.load_from_fen("8/8/k1K5/8/8/8/7Q/8 w - - 0 1");

    Move best = g_test_engine.sync_search_depth(b,2).move;
    std::string uci = decode_move_to_uci(best);

    if (uci != "h2a2") {
        std::clog << "[FAILURE] 'engine_avoids_stalemate' - Engine should play Qa2# not stalemate\n";
        std::clog << "FEN: 8/8/k1K5/8/8/8/7Q/8 w - - 0 1\n";
        std::clog << "Expected: h2a2 Got: " << uci << "\n";
        return false;
    }

    return true;
}

bool test_engine_captures_hanging_piece(Board& b) {
    struct TestCase {
        std::string fen;
        std::string expected_uci;
        std::string description;
    };

    TestCase test_cases[] = {
        // Queen captures hanging knight
        {"7k/8/8/3n4/8/8/8/3Q3K w - - 0 1", "d1d5", "queen captures hanging knight"},
        // Rook captures hanging rook
        {"4k3/8/8/r7/8/8/8/R3K3 w - - 0 1", "a1a5", "rook captures hanging rook"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        Move best = g_test_engine.sync_search_depth(b,2).move;
        std::string uci = decode_move_to_uci(best);

        if (uci != tc.expected_uci) {
            std::clog << "[FAILURE] 'engine_captures_hanging_piece' - Wrong move\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            std::clog << "Expected: " << tc.expected_uci << " Got: " << uci << "\n";
            return false;
        }
    }

    return true;
}

bool test_engine_promotes(Board& b) {
    // Pawn on e7 vs black rook — promoting to queen is clearly winning
    b.load_from_fen("7k/4P3/8/8/8/8/6r1/K7 w - - 0 1");

    Move best = g_test_engine.sync_search_depth(b,2).move;
    std::string uci = decode_move_to_uci(best);

    if (uci != "e7e8q") {
        std::clog << "[FAILURE] 'engine_promotes' - Engine should promote to queen\n";
        std::clog << "FEN: 7k/4P3/8/8/8/8/6r1/K7 w - - 0 1\n";
        std::clog << "Expected: e7e8q Got: " << uci << "\n";
        return false;
    }

    return true;
}

} // namespace

bool test_search(Board& b) {
    if (!test_no_legal_moves(b)) return false;
    if (!test_mate_in_1(b)) return false;
    if (!test_mate_in_2(b)) return false;
    if (!test_engine_finds_stalemate(b)) return false;
    if (!test_engine_avoids_stalemate(b)) return false;
    if (!test_engine_captures_hanging_piece(b)) return false;
    if (!test_engine_promotes(b)) return false;
    return true;
}
