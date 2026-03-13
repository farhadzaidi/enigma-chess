#pragma once

#include <string>
#include <array>
#include <vector>
#include <cctype>
#include <sstream>
#include <iostream>
#include <algorithm>

#include "move.hpp"
#include "move_generator/attacks.hpp"
#include "eval/constants.hpp"
#include "precompute/tables.hpp"
#include "precompute/zobrist.hpp"

// This struct contains important board state information which is useful for undoing moves.
// These attributes are overwritten when making a move and unable to be restored from the move encoding.
struct UndoState {
    Square en_passant_target;
    CastlingRights castling_rights;
    uint8_t halfmoves; // Truncating from int to U8 to save space
    Piece captured_piece;

    UndoState() :
        en_passant_target(NO_SQUARE),
        castling_rights(NO_CASTLING_RIGHTS),
        halfmoves(0),
        captured_piece(NO_PIECE) {}

    UndoState(Square ep, CastlingRights cr, uint8_t hm, Piece cp) :
        en_passant_target(ep),
        castling_rights(cr),
        halfmoves(hm),
        captured_piece(cp) {}
};

struct Board {

    // --- Board Representation ---
    std::array<std::array<Bitboard, NUM_PIECES>, NUM_SIDES> pieces;
    std::array<Bitboard, NUM_SIDES> sides;
    std::array<Piece, NUM_SQUARES> piece_map;

    ZobristHash position_hash;
    ZobristHash pawn_hash;

    // Additional information
    std::array<Square, NUM_SIDES> king_squares;
    Bitboard occupied;

    // Score
    std::array<int, NUM_SIDES> early_score;
    std::array<int, NUM_SIDES> late_score;
    int game_phase;

    // Board state information
    Side to_move;
    CastlingRights castling_rights;
    Square en_passant_target;
    int halfmoves;
    int fullmoves;

    // These stacks are implemented as arrays using ply as a pointer to the top
    // They are useful for undoing moves
    int ply;
    std::array<Move, MAX_GAME_PLY> move_history; // Keeps track of made moves
    std::array<UndoState, MAX_GAME_PLY> state_history; // Keeps track of irreversible board state
    std::array<ZobristHash, MAX_GAME_PLY + 1> position_hashes; // Position hash for each ply (used for repetition detection)
    std::array<ZobristHash, MAX_GAME_PLY + 1> pawn_hashes; // Hash for pawn configuration

    // ### PUBLIC API

    inline void load_from_fen(std::string_view fen = START_POS_FEN);
    inline void make_move(Move move);
    inline void unmake_move(Move move);
    inline void make_null_move();
    inline void unmake_null_move();
    inline bool in_check(Side side = NO_SIDE) const;
    inline bool has_non_pawn_material(Side side) const;
    inline bool is_attacked(Square sq, Side by) const;
    inline Bitboard attackers_to(Square sq, Bitboard occupied) const;
    inline bool is_legal_move(Move move);
    inline bool has_repeated() const;

    Board() { reset(); }

    void reset() {
        pieces[WHITE].fill(EMPTY_BITBOARD);
        pieces[BLACK].fill(EMPTY_BITBOARD);
        sides.fill(EMPTY_BITBOARD);

        piece_map.fill(NO_PIECE);
        king_squares.fill(NO_SQUARE);
        position_hashes.fill(0);
        pawn_hashes.fill(0);

        early_score.fill(0);
        late_score.fill(0);
        game_phase = 0;

        occupied = EMPTY_BITBOARD;
        to_move = NO_SIDE;
        castling_rights = NO_CASTLING_RIGHTS;
        en_passant_target = NO_SQUARE;
        halfmoves = 0;
        fullmoves = 0;
        ply = 0;
        position_hash = 0;
        pawn_hash = 0;
    }

private:

    // ### HELPERS

    inline Side get_side(Square square) const;
    inline void place_piece(Side side, Piece piece, Square square);
    inline void remove_piece(Side side, Piece piece, Square square);
    inline void xor_en_passant();
    inline void xor_castling_rights();
    inline void xor_side_to_move();
    inline void toggle_side_to_move();
    inline void set_en_passant_target(Side side, Piece piece, Square from, Square to);
    inline Piece handle_capture(Square capture_square, Side moving_side, MoveFlag mflag);
    inline void handle_castle(Square castle_square);
    inline void undo_castle(Square castle_square);
    inline void update_castling_rights(Square from, Square to);
};

#include "board/utils.ipp"
#include "board/fen.ipp"
#include "board/moves.ipp"
