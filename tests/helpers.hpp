#pragma once

#include "core/types.hpp"
#include "board/board.hpp"

// Compare two board states for test assertions.
// include_eval_state=false preserves older tests that only validated core state;
// include_eval_state=true also checks incremental eval bookkeeping fields.
inline bool board_position_equal(const Board& a, const Board& b, bool include_eval_state = false) {
    if (a.position_hash != b.position_hash) return false;
    if (a.pawn_hash != b.pawn_hash) return false;
    if (a.occupied != b.occupied) return false;
    if (a.to_move != b.to_move) return false;
    if (a.castling_rights != b.castling_rights) return false;
    if (a.en_passant_target != b.en_passant_target) return false;
    if (a.halfmoves != b.halfmoves) return false;
    if (a.fullmoves != b.fullmoves) return false;
    if (a.ply != b.ply) return false;

    if (include_eval_state) {
        if (a.game_phase != b.game_phase) return false;
    }

    for (int c = 0; c < NUM_COLORS; c++) {
        if (a.colors[c] != b.colors[c]) return false;
        if (a.king_squares[c] != b.king_squares[c]) return false;

        if (include_eval_state) {
            if (a.early_score[c] != b.early_score[c]) return false;
            if (a.late_score[c] != b.late_score[c]) return false;
        }

        for (int p = 0; p < NUM_PIECES; p++) {
            if (a.pieces[c][p] != b.pieces[c][p]) return false;
        }
    }

    for (int sq = 0; sq < NUM_SQUARES; sq++) {
        if (a.piece_map[sq] != b.piece_map[sq]) return false;
    }

    return true;
}
