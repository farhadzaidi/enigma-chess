#include <iostream>

#include "types.hpp"
#include "board.hpp"
#include "helpers.hpp"

// make/unmake null move should restore board state exactly
static bool test_null_move_restore(Board& b) {
    struct TestCase {
        std::string fen;
        std::string description;
    };

    TestCase test_cases[] = {
        // Starting position (white to move, EP clear)
        {START_POS_FEN, "starting position"},
        // Position with en passant target set (generated from python-chess: after 1.e4)
        {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "position with en passant target"},
        // Position with partial castling rights
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w Kk - 0 1", "partial castling rights"},
        // Position with no castling rights
        {"4k3/8/8/8/8/8/8/4K3 w - - 0 1", "bare kings, no castling"},
        // Middlegame position (black to move)
        {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 4 4", "Italian game (black to move)"},
        // Position with high halfmove clock
        {"4k3/8/8/8/8/8/8/4K3 w - - 48 50", "high halfmove clock"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        Board before = b;

        b.make_null_move();
        b.unmake_null_move();

        if (!board_position_equal(b, before, true)) {
            std::clog << "[FAILURE] 'null_move_restore' - Board state not restored after make/unmake null move\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            return false;
        }
    }

    return true;
}

// Null move should toggle side to move
static bool test_null_move_toggles_side(Board& b) {
    b.load_from_fen(START_POS_FEN);
    Color original_side = b.to_move;
    b.make_null_move();

    if (b.to_move != (original_side ^ 1)) {
        std::clog << "[FAILURE] 'null_move_toggles_side' - Side to move not toggled\n";
        std::clog << "Expected: " << (original_side ^ 1) << " Got: " << b.to_move << "\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();
    return true;
}

// Null move should clear en passant target
static bool test_null_move_clears_en_passant(Board& b) {
    // After 1.e4 (en passant target = e3)
    b.load_from_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");

    if (b.en_passant_target == NO_SQUARE) {
        std::clog << "[FAILURE] 'null_move_clears_en_passant' - Test precondition failed: EP target not set\n";
        return false;
    }

    b.make_null_move();

    if (b.en_passant_target != NO_SQUARE) {
        std::clog << "[FAILURE] 'null_move_clears_en_passant' - EP target not cleared after null move\n";
        std::clog << "Expected: NO_SQUARE Got: " << b.en_passant_target << "\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();
    return true;
}

// Null move should not modify piece positions
static bool test_null_move_preserves_pieces(Board& b) {
    b.load_from_fen(KIWIPETE_FEN);
    Board before = b;

    b.make_null_move();

    // Check that all piece bitboards are unchanged
    for (int c = 0; c < NUM_COLORS; c++) {
        for (int p = 0; p < NUM_PIECES; p++) {
            if (b.pieces[c][p] != before.pieces[c][p]) {
                std::clog << "[FAILURE] 'null_move_preserves_pieces' - Piece bitboard changed\n";
                std::clog << "Color: " << c << " Piece: " << p << "\n";
                b.unmake_null_move();
                return false;
            }
        }

        if (b.colors[c] != before.colors[c]) {
            std::clog << "[FAILURE] 'null_move_preserves_pieces' - Color bitboard changed\n";
            b.unmake_null_move();
            return false;
        }
    }

    if (b.occupied != before.occupied) {
        std::clog << "[FAILURE] 'null_move_preserves_pieces' - Occupied bitboard changed\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();
    return true;
}

// Null move should not modify castling rights
static bool test_null_move_preserves_castling(Board& b) {
    b.load_from_fen("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
    CastlingRights original_rights = b.castling_rights;

    b.make_null_move();

    if (b.castling_rights != original_rights) {
        std::clog << "[FAILURE] 'null_move_preserves_castling' - Castling rights changed\n";
        std::clog << "Expected: " << original_rights << " Got: " << b.castling_rights << "\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();
    return true;
}

// Zobrist hash should differ after null move (side to move changes) and restore after unmake
static bool test_null_move_zobrist(Board& b) {
    struct TestCase {
        std::string fen;
        std::string description;
    };

    TestCase test_cases[] = {
        {START_POS_FEN, "starting position"},
        {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "position with EP"},
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "castling position"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        ZobristHash original_hash = b.zobrist_hash;

        b.make_null_move();

        if (b.zobrist_hash == original_hash) {
            std::clog << "[FAILURE] 'null_move_zobrist' - Hash unchanged after null move\n";
            std::clog << "Case: " << tc.description << "\n";
            b.unmake_null_move();
            return false;
        }

        b.unmake_null_move();

        if (b.zobrist_hash != original_hash) {
            std::clog << "[FAILURE] 'null_move_zobrist' - Hash not restored after unmake\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "Expected: " << original_hash << " Got: " << b.zobrist_hash << "\n";
            return false;
        }
    }

    return true;
}

// Null move should increment ply and restore it on unmake
static bool test_null_move_ply(Board& b) {
    b.load_from_fen(START_POS_FEN);
    int original_ply = b.ply;

    b.make_null_move();

    if (b.ply != original_ply + 1) {
        std::clog << "[FAILURE] 'null_move_ply' - Ply not incremented\n";
        std::clog << "Expected: " << original_ply + 1 << " Got: " << b.ply << "\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();

    if (b.ply != original_ply) {
        std::clog << "[FAILURE] 'null_move_ply' - Ply not restored\n";
        std::clog << "Expected: " << original_ply << " Got: " << b.ply << "\n";
        return false;
    }

    return true;
}

// A real move after a null move should work correctly
static bool test_null_move_then_real_move(Board& b) {
    // After 1.e4 d5 (black to move... but let's null move, then play as white again)
    // FEN: rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2
    b.load_from_fen("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
    Board before = b;

    // Null move (white passes, now black to move)
    b.make_null_move();

    // Black plays Nf6
    Move nf6 = encode_move_from_uci(b, "g8f6");
    b.make_move(nf6);

    // Unmake Nf6, then unmake null move - should restore original position
    b.unmake_move(nf6);
    b.unmake_null_move();

    if (!board_position_equal(b, before, true)) {
        std::clog << "[FAILURE] 'null_move_then_real_move' - Board state not restored\n";
        return false;
    }

    return true;
}

bool test_null_move(Board& b) {
    if (!test_null_move_restore(b)) return false;
    if (!test_null_move_toggles_side(b)) return false;
    if (!test_null_move_clears_en_passant(b)) return false;
    if (!test_null_move_preserves_pieces(b)) return false;
    if (!test_null_move_preserves_castling(b)) return false;
    if (!test_null_move_zobrist(b)) return false;
    if (!test_null_move_ply(b)) return false;
    if (!test_null_move_then_real_move(b)) return false;
    return true;
}
