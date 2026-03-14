#include "tests/helpers.hpp"

bool board_position_equal(const Board& a, const Board& b) {
    if (a.position_hash() != b.position_hash()) return false;
    if (a.pawn_hash() != b.pawn_hash()) return false;
    if (a.occupied() != b.occupied()) return false;
    if (a.to_move() != b.to_move()) return false;
    if (a.castling_rights() != b.castling_rights()) return false;
    if (a.en_passant_target() != b.en_passant_target()) return false;
    if (a.halfmoves() != b.halfmoves()) return false;
    if (a.fullmoves() != b.fullmoves()) return false;
    if (a.ply() != b.ply()) return false;
    if (a.game_phase() != b.game_phase()) return false;

    for (int side = 0; side < NUM_SIDES; side++) {
        if (a.sides()[side] != b.sides()[side]) return false;
        if (a.king_squares()[side] != b.king_squares()[side]) return false;
        if (a.early_scores()[side] != b.early_scores()[side]) return false;
        if (a.late_scores()[side] != b.late_scores()[side]) return false;

        for (int piece = 0; piece < NUM_PIECES; piece++) {
            if (a.pieces()[side][piece] != b.pieces()[side][piece]) return false;
        }
    }

    for (int sq = 0; sq < NUM_SQUARES; sq++) {
        if (a.piece_map()[sq] != b.piece_map()[sq]) return false;
    }

    return true;
}

bool move_list_contains(const MoveList& moves, Move target) {
    for (const Move& move : moves) {
        if (move == target) return true;
    }
    return false;
}
