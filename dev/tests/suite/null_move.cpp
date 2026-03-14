#include <iostream>
#include <string_view>

#include "types.hpp"
#include "board.hpp"
#include "utils/notation.hpp"
#include "tests/helpers.hpp"

namespace {

// make/unmake null move should restore board state exactly
bool test_null_move_restore(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view description;
    };

    TestCase test_cases[] = {
        // Starting position (white to move, EP clear)
        {START_POS_FEN, "starting position"},
        // Position with en passant target set (after 1.e4)
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

        if (!board_position_equal(b, before)) {
            std::clog << "[FAILURE] 'null_move_restore' - Board state not restored after make/unmake null move\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << "\n";
            return false;
        }
    }

    return true;
}

// Null move should toggle side to move
bool test_null_move_toggles_side(Board& b) {
    b.load_from_fen(START_POS_FEN);
    Side original_side = b.to_move();
    b.make_null_move();

    if (b.to_move() != (original_side ^ 1)) {
        std::clog << "[FAILURE] 'null_move_toggles_side' - Side to move not toggled\n";
        std::clog << "Expected: " << (original_side ^ 1) << " Got: " << b.to_move() << "\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();
    return true;
}

// Null move should clear en passant target
bool test_null_move_clears_en_passant(Board& b) {
    // After 1.e4 (en passant target = e3)
    b.load_from_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");

    ASSERT(b.en_passant_target() != NO_SQUARE, "null_move_clears_en_passant", "Test precondition failed: EP target not set");

    b.make_null_move();

    if (b.en_passant_target() != NO_SQUARE) {
        std::clog << "[FAILURE] 'null_move_clears_en_passant' - EP target not cleared after null move\n";
        std::clog << "Expected: NO_SQUARE Got: " << b.en_passant_target() << "\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();
    return true;
}

// Null move should not modify piece positions
bool test_null_move_preserves_pieces(Board& b) {
    b.load_from_fen(KIWIPETE_FEN);
    Board before = b;

    b.make_null_move();

    // Check that all piece bitboards are unchanged
    for (int side = 0; side < NUM_SIDES; side++) {
        for (int piece = 0; piece < NUM_PIECES; piece++) {
            if (b.pieces()[side][piece] != before.pieces()[side][piece]) {
                std::clog << "[FAILURE] 'null_move_preserves_pieces' - Piece bitboard changed\n";
                std::clog << "Side: " << side << " Piece: " << piece << "\n";
                b.unmake_null_move();
                return false;
            }
        }

        if (b.sides()[side] != before.sides()[side]) {
            std::clog << "[FAILURE] 'null_move_preserves_pieces' - Side bitboard changed\n";
            b.unmake_null_move();
            return false;
        }
    }

    if (b.occupied() != before.occupied()) {
        std::clog << "[FAILURE] 'null_move_preserves_pieces' - Occupied bitboard changed\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();
    return true;
}

// Null move should not modify castling rights
bool test_null_move_preserves_castling(Board& b) {
    b.load_from_fen("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1");
    CastlingRights original_rights = b.castling_rights();

    b.make_null_move();

    if (b.castling_rights() != original_rights) {
        std::clog << "[FAILURE] 'null_move_preserves_castling' - Castling rights changed\n";
        std::clog << "Expected: " << original_rights << " Got: " << b.castling_rights() << "\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();
    return true;
}

// Zobrist hash should differ after null move (side to move changes), while pawn hash
// should remain unchanged. Both should restore after unmake.
bool test_null_move_zobrist(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {START_POS_FEN, "starting position"},
        {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "position with EP"},
        {"r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1", "castling position"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        ZobristHash original_hash = b.position_hash();
        ZobristHash original_pawn_hash = b.pawn_hash();

        b.make_null_move();

        if (b.position_hash() == original_hash) {
            std::clog << "[FAILURE] 'null_move_zobrist' - Hash unchanged after null move\n";
            std::clog << "Case: " << tc.description << "\n";
            b.unmake_null_move();
            return false;
        }

        if (b.pawn_hash() != original_pawn_hash) {
            std::clog << "[FAILURE] 'null_move_zobrist' - Pawn hash changed after null move\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "Expected: " << original_pawn_hash << " Got: " << b.pawn_hash() << "\n";
            b.unmake_null_move();
            return false;
        }

        b.unmake_null_move();

        if (b.position_hash() != original_hash || b.pawn_hash() != original_pawn_hash) {
            std::clog << "[FAILURE] 'null_move_zobrist' - Hash not restored after unmake\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "Zobrist expected: " << original_hash << " Got: " << b.position_hash() << "\n";
            std::clog << "Pawn hash expected: " << original_pawn_hash << " Got: " << b.pawn_hash() << "\n";
            return false;
        }
    }

    return true;
}

// Null move should increment ply and restore it on unmake
bool test_null_move_ply(Board& b) {
    b.load_from_fen(START_POS_FEN);
    int original_ply = b.ply();

    b.make_null_move();

    if (b.ply() != original_ply + 1) {
        std::clog << "[FAILURE] 'null_move_ply' - Ply not incremented\n";
        std::clog << "Expected: " << original_ply + 1 << " Got: " << b.ply() << "\n";
        b.unmake_null_move();
        return false;
    }

    b.unmake_null_move();

    ASSERT_EQ(b.ply(), original_ply, "null_move_ply", "Ply not restored");

    return true;
}

// A real move after a null move should work correctly
bool test_null_move_then_real_move(Board& b) {
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

    ASSERT_BOARD(b, before, "null_move_then_real_move", "Board state not restored");

    return true;
}

} // namespace

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
