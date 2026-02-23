#include <iostream>

#include "types.hpp"
#include "board.hpp"
#include "search.hpp"
#include "utils.hpp"
#include "move_generator.hpp"

// Positions that are already checkmate or stalemate (no legal moves)
// Engine should return NULL_MOVE without crashing
static bool test_no_legal_moves(Board& b) {
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

        MoveList moves = generate_moves<ALL>(b);
        if (!moves.is_empty()) {
            std::clog << "[FAILURE] 'no_legal_moves' - Position has legal moves but should have none\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            return false;
        }

        Move best = search_depth(b, 1);

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

// Engine should find checkmate in 1 move
static bool test_mate_in_1(Board& b) {
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

        Move best = search_depth(b, 2);
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

// Engine should find checkmate in 2 moves
static bool test_mate_in_2(Board& b) {
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
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);

        Move best = search_depth(b, 4);
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

// Engine should find stalemate when it's the only option
static bool test_engine_finds_stalemate(Board& b) {
    // Black king on a2 is in check from white queen on a1.
    // The only legal move is Kxa1, which stalemates white.
    b.load_from_fen("5r2/8/8/8/K7/3q4/k7/Q3b3 b - - 0 1");

    Move best = search_depth(b, 2);
    std::string uci = decode_move_to_uci(best);

    if (uci != "a2a1") {
        std::clog << "[FAILURE] 'engine_finds_stalemate' - Engine failed to find forced stalemate move\n";
        std::clog << "FEN: 5r2/8/8/8/K7/3q4/k7/Q3b3 b - - 0 1\n";
        std::clog << "Expected: a2a1 Got: " << uci << "\n";
        return false;
    }

    b.make_move(best);
    MoveList moves = generate_moves<ALL>(b);
    if (!moves.is_empty() || b.in_check()) {
        std::clog << "[FAILURE] 'engine_finds_stalemate' - Expected stalemate after move a2a1\n";
        return false;
    }

    return true;
}

// Engine should avoid stalemating the opponent when it has a winning position
static bool test_engine_avoids_stalemate(Board& b) {
    // White has a mate-in-1 with Qa2#, but Qc7 is stalemate.
    b.load_from_fen("8/8/k1K5/8/8/8/7Q/8 w - - 0 1");

    Move best = search_depth(b, 2);
    std::string uci = decode_move_to_uci(best);

    // Qa2# wins immediately; Qc7 is stalemate and should be avoided.
    if (uci != "h2a2") {
        std::clog << "[FAILURE] 'engine_avoids_stalemate' - Engine should play Qa2# not stalemate\n";
        std::clog << "FEN: 8/8/k1K5/8/8/8/7Q/8 w - - 0 1\n";
        std::clog << "Expected: h2a2 Got: " << uci << "\n";
        return false;
    }

    return true;
}

// Repetition should be detected
static bool test_repetition(Board& b) {
    b.load_from_fen();

    // Play Nf3 Nf6 Ng1 Ng8 - back to start
    Move m1 = encode_move_from_uci(b, "g1f3");
    b.make_move(m1);
    Move m2 = encode_move_from_uci(b, "g8f6");
    b.make_move(m2);
    Move m3 = encode_move_from_uci(b, "f3g1");
    b.make_move(m3);
    Move m4 = encode_move_from_uci(b, "f6g8");
    b.make_move(m4);

    if (!b.has_repeated()) {
        std::clog << "[FAILURE] 'repetition' - Expected has_repeated() to return true\n";
        return false;
    }

    // Unmake last move, repetition should no longer be detected
    b.unmake_move(m4);

    if (b.has_repeated()) {
        std::clog << "[FAILURE] 'repetition' - Expected has_repeated() to return false after unmake\n";
        return false;
    }

    return true;
}

// 50-move rule draw detection
static bool test_fifty_move_rule(Board& b) {
    b.load_from_fen("4k3/8/8/8/8/8/8/4K2R w - - 99 50");

    // After a quiet move (Rh2), halfmoves should hit 100 and trigger draw
    b.make_move(encode_move_from_uci(b, "h1h2"));

    if (b.halfmoves < FIFTY_MOVE_PLY_LIMIT) {
        std::clog << "[FAILURE] 'fifty_move_rule' - Expected halfmoves >= 100 after quiet move\n";
        std::clog << "Halfmoves: " << b.halfmoves << "\n";
        return false;
    }

    return true;
}

bool test_game_end(Board& b) {
    if (!test_no_legal_moves(b)) return false;
    if (!test_mate_in_1(b)) return false;
    if (!test_mate_in_2(b)) return false;
    if (!test_engine_finds_stalemate(b)) return false;
    if (!test_engine_avoids_stalemate(b)) return false;
    if (!test_repetition(b)) return false;
    if (!test_fifty_move_rule(b)) return false;
    return true;
}
