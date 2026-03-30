#pragma once

#include <array>
#include <string_view>

#include "move.hpp"
#include "nnue.hpp"
#include "square.hpp"

// --- Board Constants ---

constexpr int MAX_GAME_PLY = 2048;
constexpr int FIFTY_MOVE_PLY_LIMIT = 100;

// --- FEN Strings ---

constexpr std::string_view START_POS_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr std::string_view KIWIPETE_FEN =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
constexpr std::string_view POSITION_3_FEN =
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
constexpr std::string_view POSITION_4_FEN =
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
constexpr std::string_view POSITION_5_FEN =
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8  ";
constexpr std::string_view POSITION_6_FEN =
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10 ";

class Board {
public:
    // --- Lifecycle ---

    Board();
    void reset();

    // --- Moves ---

    /** Parse a FEN string and initialize the board state from it. */
    void load_from_fen(std::string_view fen = START_POS_FEN);
    /** Apply a move, updating all board state and recording undo information. */
    void make_move(Move move);
    /** Undo the last move applied by make_move, restoring previous state. */
    void unmake_move(Move move);
    /** Apply a null (pass) move, toggling side to move without moving a piece. */
    void make_null_move();
    /** Undo the last null move, restoring the previous side and en passant state. */
    void unmake_null_move();

    // --- Query ---

    /** Check if the given side (or side to move) is in check. */
    bool in_check(Side side = NO_SIDE) const;
    /** Return true if the side has any knights, bishops, rooks, or queens. */
    bool has_non_pawn_material(Side side) const;
    /** Return true if the square is attacked by the given side. */
    bool is_attacked(Square sq, Side by) const;
    /** Return a bitboard of all pieces in the occupied set that attack the square. */
    Bitboard attackers_to(Square sq, Bitboard occupied) const;
    /** Validate a move for full legality, including leaving the king safe. */
    bool is_legal_move(Move move);
    /** Return true if the current position has occurred before in the game. */
    bool has_repeated() const;

    // --- Accessors ---

    const std::array<std::array<Bitboard, NUM_PIECES>, NUM_SIDES>& pieces() const { return pieces_; }
    const std::array<Bitboard, NUM_SIDES>& sides() const { return sides_; }
    const std::array<Piece, NUM_SQUARES>& piece_map() const { return piece_map_; }
    const std::array<Square, NUM_SIDES>& king_squares() const { return king_squares_; }
    const std::array<int, NUM_SIDES>& early_scores() const { return early_score_; }
    const std::array<int, NUM_SIDES>& late_scores() const { return late_score_; }

    Bitboard piece_bitboard(Side side, Piece piece) const { return pieces_[side][piece]; }
    Bitboard side_bitboard(Side side) const { return sides_[side]; }
    Piece piece_at(Square square) const { return piece_map_[square]; }

    ZobristHash position_hash() const { return position_hash_; }
    ZobristHash pawn_hash() const { return pawn_hash_; }

    Square king_square(Side side) const { return king_squares_[side]; }
    Bitboard occupied() const { return occupied_; }

    int early_score(Side side) const { return early_score_[side]; }
    int late_score(Side side) const { return late_score_[side]; }
    int game_phase() const { return game_phase_; }

    Side to_move() const { return to_move_; }
    CastlingRights castling_rights() const { return castling_rights_; }
    Square en_passant_target() const { return en_passant_target_; }
    int halfmoves() const { return halfmoves_; }
    int fullmoves() const { return fullmoves_; }
    int ply() const { return ply_; }
    Move previous_move() const { return ply_ > 0 ? move_history_[ply_ - 1] : NULL_MOVE; }

    /** Evaluate the current position using NNUE */
    PositionScore nnue_evaluate() { return nnue_.evaluate(to_move_); }

private:
    // This record contains important board state information which is useful for undoing moves.
    // These attributes are overwritten when making a move and unable to be restored from the move encoding.
    struct UndoState {
        Square en_passant_target = NO_SQUARE;
        CastlingRights castling_rights = NO_CASTLING_RIGHTS;
        uint8_t halfmoves = 0; // Truncating from int to U8 to save space
        Piece captured_piece = NO_PIECE;

        UndoState() = default;

        UndoState(Square ep, CastlingRights cr, uint8_t hm, Piece cp) :
            en_passant_target(ep),
            castling_rights(cr),
            halfmoves(hm),
            captured_piece(cp) {}
    };

    // --- Representation ---

    std::array<std::array<Bitboard, NUM_PIECES>, NUM_SIDES> pieces_;
    std::array<Bitboard, NUM_SIDES> sides_;
    std::array<Piece, NUM_SQUARES> piece_map_;
    ZobristHash position_hash_;
    ZobristHash pawn_hash_;
    std::array<Square, NUM_SIDES> king_squares_;
    Bitboard occupied_;

    // --- Score ---

    std::array<int, NUM_SIDES> early_score_;
    std::array<int, NUM_SIDES> late_score_;
    int game_phase_;

    // --- State ---

    Side to_move_;
    CastlingRights castling_rights_;
    Square en_passant_target_;
    int halfmoves_;
    int fullmoves_;

    // --- History ---

    int ply_;
    std::array<Move, MAX_GAME_PLY> move_history_;
    std::array<UndoState, MAX_GAME_PLY> state_history_;
    std::array<ZobristHash, MAX_GAME_PLY + 1> position_hashes_;
    std::array<ZobristHash, MAX_GAME_PLY + 1> pawn_hashes_;

    // --- NNUE ---
    NNUE nnue_;

    // --- Helpers ---

    Side get_side(Square square) const;
    void place_piece(Side side, Piece piece, Square square, bool update_nnue);
    void remove_piece(Side side, Piece piece, Square square, bool update_nnue);
    void xor_en_passant();
    void xor_castling_rights();
    void xor_side_to_move();
    void toggle_side_to_move();
    void set_en_passant_target(Side side, Piece piece, Square from, Square to);
    Piece handle_capture(Square capture_square, Side moving_side, MoveFlag move_flag, bool update_nnue);
    void handle_castle(Square castle_square);
    void undo_castle(Square castle_square);
    void update_castling_rights(Square from, Square to);
};
