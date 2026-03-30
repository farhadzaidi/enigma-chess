#include <iostream>
#include <set>
#include <string_view>

#include "types.hpp"
#include "board.hpp"
#include "notation.hpp"
#include "move_generator.hpp"

namespace {

// Make/unmake should restore exact hash for all move types
bool test_zobrist_make_unmake(Board& b) {
    struct TestCase {
        std::string_view fen;
        std::string_view uci;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {START_POS_FEN, "e2e4", "quiet pawn push"},
        {START_POS_FEN, "g1f3", "quiet knight move"},
        {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "d7d5", "quiet pawn push (black)"},
        {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2", "b8c6", "quiet knight (black)"},
        {"r1bqkbnr/pppppppp/2n5/8/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", "f1b5", "quiet bishop"},
        // Captures
        {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", "e4d5", "pawn capture"},
        {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4", "f3e5", "knight capture"},
        // Castling
        {"r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4", "e1g1", "white short castle"},
        {"r3kbnr/pppqpppp/2n1b3/3p4/3P4/2N1BN2/PPPQPPPP/R3KB1R w KQkq - 6 5", "e1c1", "white long castle"},
        {"r1bqk2r/ppppbppp/2n2n2/4p3/2B1P3/3P1N2/PPP2PPP/RNBQ1RK1 b kq - 0 5", "e8g8", "black short castle"},
        // En passant
        {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", "e5f6", "en passant capture"},
        {"rnbqkbnr/pppp1ppp/8/8/3PpP2/8/PPP1P1PP/RNBQKBNR b KQkq d3 0 3", "e4d3", "en passant capture (black)"},
        // Promotions
        {"8/P7/8/8/8/8/1k4K1/8 w - - 0 1", "a7a8q", "queen promotion"},
        {"8/P7/8/8/8/8/1k4K1/8 w - - 0 1", "a7a8n", "knight promotion"},
        // Capture + promotion
        {"1n6/P7/8/8/8/8/1k4K1/8 w - - 0 1", "a7b8q", "capture + queen promotion"},
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen);
        ZobristHash original_hash = b.position_hash();

        Move move = encode_move_from_uci(b, tc.uci);
        b.make_move(move);
        b.unmake_move(move);

        if (b.position_hash() != original_hash) {
            std::clog << "[FAILURE] 'zobrist_make_unmake' - Hash mismatch after make/unmake\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.fen << " Move: " << tc.uci << "\n";
            std::clog << "Zobrist expected: " << original_hash << " Got: " << b.position_hash() << "\n";
            return false;
        }
    }

    return true;
}

// Same position reached by different move orders should have the same hash
bool test_zobrist_transposition(Board& b) {
    // Path A: 1.e4 d5 2.Nf3
    b.load_from_fen();
    b.make_move(encode_move_from_uci(b, "e2e4"));
    b.make_move(encode_move_from_uci(b, "d7d5"));
    b.make_move(encode_move_from_uci(b, "g1f3"));
    ZobristHash hash_a = b.position_hash();

    // Path B: 1.Nf3 d5 2.e4
    b.load_from_fen();
    b.make_move(encode_move_from_uci(b, "g1f3"));
    b.make_move(encode_move_from_uci(b, "d7d5"));
    b.make_move(encode_move_from_uci(b, "e2e4"));
    ZobristHash hash_b = b.position_hash();

    if (hash_a != hash_b) {
        std::clog << "[FAILURE] 'zobrist_transposition' - Same position has different hashes\n";
        std::clog << "Path A (1.e4 d5 2.Nf3): " << hash_a << "\n";
        std::clog << "Path B (1.Nf3 d5 2.e4): " << hash_b << "\n";
        return false;
    }

    // Path C: 1.d4 Nf6 2.c4 e6 3.Nf3
    b.load_from_fen();
    b.make_move(encode_move_from_uci(b, "d2d4"));
    b.make_move(encode_move_from_uci(b, "g8f6"));
    b.make_move(encode_move_from_uci(b, "c2c4"));
    b.make_move(encode_move_from_uci(b, "e7e6"));
    b.make_move(encode_move_from_uci(b, "g1f3"));
    ZobristHash hash_c = b.position_hash();

    // Path D: 1.Nf3 Nf6 2.c4 e6 3.d4
    b.load_from_fen();
    b.make_move(encode_move_from_uci(b, "g1f3"));
    b.make_move(encode_move_from_uci(b, "g8f6"));
    b.make_move(encode_move_from_uci(b, "c2c4"));
    b.make_move(encode_move_from_uci(b, "e7e6"));
    b.make_move(encode_move_from_uci(b, "d2d4"));
    ZobristHash hash_d = b.position_hash();

    if (hash_c != hash_d) {
        std::clog << "[FAILURE] 'zobrist_transposition' - Same position has different hashes (case 2)\n";
        std::clog << "Path C (1.d4 Nf6 2.c4 e6 3.Nf3): " << hash_c << "\n";
        std::clog << "Path D (1.Nf3 Nf6 2.c4 e6 3.d4): " << hash_d << "\n";
        return false;
    }

    return true;
}

// Hash should encode state components correctly (side, castling rights, en passant)
bool test_zobrist_state_components(Board& b) {
    struct TestCase {
        std::string_view fen_a;
        std::string_view fen_b;
        bool expect_zobrist_equal;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {
            "8/8/8/8/8/8/8/4K2k w - - 0 1",
            "8/8/8/8/8/8/8/4K2k b - - 0 1",
            false,
            "side to move changes hash"
        },
        {
            "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1",
            false,
            "castling rights change hash"
        },
        {
            "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
            "4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1",
            false,
            "capturable en passant changes hash"
        },
        {
            "4k3/8/8/P2p4/8/8/8/4K3 w - d6 0 1",
            "4k3/8/8/P2p4/8/8/8/4K3 w - - 0 1",
            true,
            "non-capturable en passant should not change hash"
        },
        {
            "8/8/8/8/8/8/8/4K2k w - - 0 1",
            "8/8/8/8/8/8/8/4K2k w - - 99 50",
            true,
            "move clocks should not change hash"
        },
        {
            "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
            "4k3/8/8/8/8/4P3/8/4K3 w - - 0 1",
            false,
            "pawn placement changes hash"
        },
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.fen_a);
        ZobristHash hash_a = b.position_hash();

        b.load_from_fen(tc.fen_b);
        ZobristHash hash_b = b.position_hash();

        if ((hash_a == hash_b) != tc.expect_zobrist_equal) {
            std::clog << "[FAILURE] 'zobrist_state_components' - Unexpected hash comparison result\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN A: " << tc.fen_a << "\n";
            std::clog << "FEN B: " << tc.fen_b << "\n";
            std::clog << "Zobrist A/B: " << hash_a << " / " << hash_b
                      << " (expected equal=" << tc.expect_zobrist_equal << ")\n";
            return false;
        }
    }

    return true;
}

// Incremental hash updates should match a fresh hash from loading the resulting FEN
bool test_zobrist_incremental_matches_reloaded(Board& b) {
    struct TestCase {
        std::string_view pre_fen;
        std::string_view uci;
        std::string_view post_fen;
        std::string_view description;
    };

    TestCase test_cases[] = {
        {
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
            "e1d1",
            "4k3/8/8/8/8/8/8/3K4 b - - 1 1",
            "quiet king move"
        },
        {
            "4k3/8/8/8/2p5/8/3P4/4K3 w - - 0 1",
            "d2d4",
            "4k3/8/8/8/2pP4/8/8/4K3 b - d3 0 1",
            "double pawn push sets capturable en passant"
        },
        {
            "4k3/8/8/8/8/8/3P4/4K3 w - - 0 1",
            "d2d4",
            "4k3/8/8/8/3P4/8/8/4K3 b - - 0 1",
            "double pawn push with no capturable en passant"
        },
        {
            "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "e1f1",
            "r3k2r/8/8/8/8/8/8/R4K1R b kq - 1 1",
            "king move updates castling rights"
        },
        {
            "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "a1a8",
            "R3k2r/8/8/8/8/8/8/4K2R b Kk - 0 1",
            "rook capture updates castling rights"
        },
        {
            "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
            "e5d6",
            "4k3/8/3P4/8/8/8/8/4K3 b - - 0 1",
            "en passant capture"
        },
        {
            "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "e1g1",
            "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1",
            "white short castle"
        },
        {
            "1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1",
            "a7b8q",
            "1Q2k3/8/8/8/8/8/8/4K3 b - - 0 1",
            "capture + promotion"
        },
    };

    for (const auto& tc : test_cases) {
        b.load_from_fen(tc.pre_fen);
        Move move = encode_move_from_uci(b, tc.uci);

        MoveGenerator move_generator(b);
        MoveList legal_moves = move_generator.generate_all();
        bool is_legal = false;
        for (const Move& legal : legal_moves) {
            if (legal == move) {
                is_legal = true;
                break;
            }
        }

        if (!is_legal) {
            std::clog << "[FAILURE] 'zobrist_incremental_matches_reloaded' - Illegal move test fixture\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "FEN: " << tc.pre_fen << " Move: " << tc.uci << "\n";
            return false;
        }

        b.make_move(move);
        ZobristHash incremental_hash = b.position_hash();

        Board rebuilt;
        rebuilt.load_from_fen(tc.post_fen);
        ZobristHash rebuilt_hash = rebuilt.position_hash();

        if (incremental_hash != rebuilt_hash) {
            std::clog << "[FAILURE] 'zobrist_incremental_matches_reloaded' - Hash mismatch\n";
            std::clog << "Case: " << tc.description << "\n";
            std::clog << "Pre FEN: " << tc.pre_fen << "\n";
            std::clog << "Move: " << tc.uci << "\n";
            std::clog << "Post FEN: " << tc.post_fen << "\n";
            std::clog << "Zobrist incremental/rebuilt: " << incremental_hash << " / " << rebuilt_hash << "\n";
            return false;
        }
    }

    return true;
}

// Distinct positions should produce distinct hashes (smoke test)
bool test_zobrist_uniqueness(Board& b) {
    std::string_view fens[] = {
        START_POS_FEN,
        KIWIPETE_FEN,
        POSITION_3_FEN,
        POSITION_4_FEN,
        POSITION_5_FEN,
        POSITION_6_FEN,
        "8/8/8/8/8/8/8/4K2k w - - 0 1",
        "8/8/8/8/8/8/8/4K2k b - - 0 1",
    };

    std::set<ZobristHash> hashes;
    for (std::string_view fen : fens) {
        b.load_from_fen(fen);

        if (hashes.count(b.position_hash())) {
            std::clog << "[FAILURE] 'zobrist_uniqueness' - Hash collision detected\n";
            std::clog << "FEN: " << fen << "\n";
            std::clog << "Hash: " << b.position_hash() << "\n";
            return false;
        }

        hashes.insert(b.position_hash());
    }

    return true;
}

} // namespace

bool test_zobrist(Board& b) {
    if (!test_zobrist_make_unmake(b)) return false;
    if (!test_zobrist_transposition(b)) return false;
    if (!test_zobrist_state_components(b)) return false;
    if (!test_zobrist_incremental_matches_reloaded(b)) return false;
    if (!test_zobrist_uniqueness(b)) return false;
    return true;
}
