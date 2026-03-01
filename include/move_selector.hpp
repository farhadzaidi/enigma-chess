#pragma once

#include <algorithm>

#include "types.hpp"
#include "move.hpp"
#include "check_info.hpp"
#include "move_generator.hpp"
#include "search_state.hpp"
#include "transposition_table.hpp"

// Indexed like CAPTURE_SCORE[attacker][victim]
// Incentivizes capturing high value pieces with low value pieces
constexpr std::array<std::array<MoveScore, NUM_PIECES>, NUM_PIECES> CAPTURE_SCORE = {{
    {106, 206, 306, 406, 506, 000},
    {105, 205, 305, 405, 505, 000},
    {104, 204, 304, 404, 504, 000},
    {103, 203, 303, 403, 503, 000},
    {102, 202, 302, 402, 502, 000},
    {101, 201, 301, 401, 501, 000},
}};

// Indexed by move flag
constexpr int NUM_MOVE_FLAGS = 7;
constexpr std::array<MoveScore, NUM_MOVE_FLAGS> PROMOTION_BONUS = {
    0, 0, 0, // Non-promotion flags
    200, // Bishop
    300, // Knight
    200, // Rook
    600, // Queen
};

struct MoveSelector {
    MoveSelectorPhase phase;
    CheckInfo checkInfo;
    MoveList tactical_moves;
    MoveList quiet_moves;
    Move prev_best_move;
    Move tt_move;
    Move returned_killer_1;
    Move returned_killer_2;
    bool tacticals_generated = false;
    bool quiets_generated = false;

    MoveSelector(Board& b, Move tt_move, Move prev_best_move = NULL_MOVE)
        : phase(PREVIOUS_BEST), prev_best_move(prev_best_move), tt_move(tt_move) {
        if (b.to_move == WHITE) checkInfo.compute_check_info<WHITE>(b);
        else                    checkInfo.compute_check_info<BLACK>(b);
    }

    Move next_move(Board& b, SearchState& ss) {
        switch (phase) {
            case PREVIOUS_BEST: {
                if (prev_best_move != NULL_MOVE) {
                    phase = TRANSPOSITION;
                    return prev_best_move;
                }

                phase = TRANSPOSITION;
            }
            case TRANSPOSITION: {
                // Return the TT move if we have one
                if (
                    tt_move != NULL_MOVE &&
                    tt_move != prev_best_move &&
                    b.is_legal_move(tt_move)
                ) {
                    phase = TACTICAL_MOVE;
                    return tt_move;
                }

                // If no TT moves, change phase and fall through
                phase = TACTICAL_MOVE;
            }
            case TACTICAL_MOVE: {
                // If we just entered the tactical phase, generate all tactical moves
                if (!tacticals_generated) generate_tactical_moves(b);

                // Moves are already sorted by score when generated, so we can pop the next best move
                Move next_cap = tactical_moves.pop();
                while (next_cap != NULL_MOVE && is_already_returned(next_cap)) {
                    next_cap = tactical_moves.pop();
                }
                if (next_cap != NULL_MOVE) {
                    return next_cap;
                }

                // If we don't have anymore tactical moves, change phase and fall through
                phase = KILLER;
            }
            case KILLER: {
                int ply = ss.search_ply(b.ply);
                Move killer_move_1 = ss.killer_1[ply];
                Move killer_move_2 = ss.killer_2[ply];

                if (
                    killer_move_1 != NULL_MOVE &&
                    returned_killer_1 == NULL_MOVE && 
                    !is_already_returned(killer_move_1) && 
                    b.is_legal_move(killer_move_1)
                ) {
                    returned_killer_1 = killer_move_1;
                    return killer_move_1;
                }

                if (
                    killer_move_2 != NULL_MOVE &&
                    returned_killer_2 == NULL_MOVE && 
                    !is_already_returned(killer_move_2) && 
                    b.is_legal_move(killer_move_2)
                ) {
                    returned_killer_2 = killer_move_2;
                    return killer_move_2;
                }

                // If we've tried both killers already or don't have any, fall through to the next phase
                phase = QUIET_MOVE;
            }
            case QUIET_MOVE: {
                if (!quiets_generated) generate_quiet_moves(b, ss);

                Move next_quiet = quiet_moves.pop();
                while (next_quiet != NULL_MOVE && is_already_returned(next_quiet)) {
                    next_quiet = quiet_moves.pop();
                }

                if (next_quiet != NULL_MOVE) {
                    return next_quiet;
                }

                phase = BAD_CAPTURE;
            }
            case BAD_CAPTURE:
                // No SEE yet - TODO
            default:
                return NULL_MOVE;
        }
    }

private:

    inline void generate_tactical_moves(Board& b) {
        if (b.to_move == WHITE) generate_moves_impl<WHITE, CAPTURES_AND_PROMOTIONS>(b, tactical_moves, checkInfo);
        else                    generate_moves_impl<BLACK, CAPTURES_AND_PROMOTIONS>(b, tactical_moves, checkInfo);

        sort_tactical_moves(b);
        tacticals_generated = true;
    }

    inline void generate_quiet_moves(Board& b, SearchState& ss) {
        if (b.to_move == WHITE) generate_moves_impl<WHITE, QUIET_ONLY>(b, quiet_moves, checkInfo);
        else                    generate_moves_impl<BLACK, QUIET_ONLY>(b, quiet_moves, checkInfo);

        sort_quiet_moves(b, ss);
        quiets_generated = true;
    }

    inline MoveScore get_tactical_score(const Board& b, Move m) {
        MoveScore score = 0;

        if (m.type() == CAPTURE) {
            Piece attacker = b.piece_map[m.from()];
            Piece victim = m.flag() == EN_PASSANT ? PAWN : b.piece_map[m.to()];
            score += CAPTURE_SCORE[attacker][victim];
        }

        if (m.is_promotion()) {
            score += PROMOTION_BONUS[m.flag()];
        }

        return score;
    }

    inline void sort_tactical_moves(Board& b) {
        std::sort(tactical_moves.begin(), tactical_moves.end(), [&](Move m1, Move m2) {
            return get_tactical_score(b, m1) < get_tactical_score(b, m2);
        });
    }

    inline void sort_quiet_moves(Board& b, SearchState& ss) {
        std::sort(quiet_moves.begin(), quiet_moves.end(), [&b, &ss](Move m1, Move m2) {
            Square m1_from = m1.from();
            Square m1_to = m1.to();
            Piece m1_piece = b.piece_map[m1_from];
            MoveScore m1_score = ss.color_piece_to[b.to_move][m1_piece][m1_to] + ss.from_to[m1_from][m1_to];

            Square m2_from = m2.from();
            Square m2_to = m2.to();
            Piece m2_piece = b.piece_map[m2_from];
            MoveScore m2_score = ss.color_piece_to[b.to_move][m2_piece][m2_to] + ss.from_to[m2_from][m2_to];

            return m1_score < m2_score;
        });
    }

    inline bool is_already_returned(Move move) {
        return move == prev_best_move || move == tt_move || move == returned_killer_1 || move == returned_killer_2;
    }
};
