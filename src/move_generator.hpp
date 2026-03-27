#pragma once

#include <array>

#include "types.hpp"
#include "bitboard.hpp"
#include "move.hpp"
#include "board.hpp"

/** Return the pawn attack bitboard for a given side and square */
Bitboard get_pawn_attacks(Side side, Square square);

/** Return the attack bitboard for piece type P from a square, considering occupied squares */
template <Piece P>
Bitboard get_piece_attacks(Square from, Bitboard occupied);

/** Runtime-dispatched version of get_piece_attacks */
Bitboard get_piece_attacks(Piece piece, Square from, Bitboard occupied);

class MoveGenerator {
public:
    /** Construct from a board, computing check info for the side to move */
    explicit MoveGenerator(Board& board);

    /** Generate all quiet (non-capture, non-promotion) moves */
    MoveList generate_quiets();

    /** Generate all tactical (capture + promotion) moves */
    MoveList generate_tacticals();

    /** Generate all legal moves */
    MoveList generate_all();

private:
    // --- Types ---

    struct CheckInfo {
        Bitboard pinned = 0ULL;
        std::array<Bitboard, NUM_SQUARES> pins{};
        Bitboard checkers = 0ULL;
        Bitboard must_cover = ~0ULL;
        Bitboard unsafe = 0ULL;
    };

    // --- State ---

    Board& board_;
    CheckInfo check_info_;

    /** Populate check_info_ with pins, checkers, and unsafe squares for side S */
    template <Side S>
    void compute_check_info();

    /** Detect a sliding check or pin along direction D from the king */
    template <Side S, Direction D>
    void compute_sliding_checks_and_pins(Square king_sq);

    /** Compute the combined attack mask of all enemy pieces of type P */
    template <Side S, Piece P>
    Bitboard compute_attack_mask();

    template <Side S, Direction D>
    bool is_attacked_by_slider(Square square, Bitboard occupied);

    template <Side S>
    bool is_attacked_by_slider(Square square, Bitboard occupied);

    /** Generate moves for all pieces of type P according to generation mode M */
    template <Side S, Piece P, MoveGenMode M>
    void generate_piece_moves(MoveList& moves);

    /** Decode a pawn move bitboard into individual moves, handling pins and promotions */
    template <Side S, Direction D, MoveType MT, bool IS_PROMOTION = false, bool IS_EN_PASSANT = false>
    void encode_pawn_moves(MoveList& moves, Bitboard move_mask);

    /** Generate all pawn moves (pushes, captures, en passant, promotions) for mode M */
    template <Side S, MoveGenMode M>
    void generate_pawn_moves(MoveList& moves);

    /** Generate kingside and queenside castling moves if legal */
    template <Side S>
    void generate_castling_moves(MoveList& moves);

    /** Top-level dispatch: generate all move types for side S in mode M */
    template <Side S, MoveGenMode M>
    void generate_moves_impl(MoveList& moves);
};
