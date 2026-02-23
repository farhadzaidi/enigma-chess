#pragma once

#include <algorithm>

#include "types.hpp"
#include "move.hpp"
#include "check_info.hpp"
#include "move_generator.hpp"
#include "search_state.hpp"

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

struct MoveSelector {
    MoveSelectorPhase phase;
    CheckInfo checkInfo;
    MoveList captures;
    MoveList quiet_moves;
    int killer_count;

    MoveSelector(Board& b) {
        phase = TRANSPOSITION;
        killer_count = 0;

        if (b.to_move == WHITE) checkInfo.compute_check_info<WHITE>(b);
        else                    checkInfo.compute_check_info<BLACK>(b);
    }

    Move next_move(Board& b, SearchState& ss) {
        switch (phase) {
            case TRANSPOSITION:
                // No TT right now - TODO

                // If no TT moves, change phase and fall through
                phase = GOOD_CAPTURE;
            case GOOD_CAPTURE:
                // If we just entered the capture phase, generate all captures
                if (captures.size == 0) generate_captures(b);

                // Moves are already sorted by score when generated, so we can pop the next best move
                Move next_cap = captures.pop();
                if (next_cap != NULL_MOVE) {
                    return next_cap;
                }

                // If we don't have anymore captures, change phase and fall through
                phase = KILLER;
            case KILLER:
                // We can generate quiet moves in the killer phase
                // This will be used to determine if the killer move is legal in this position
                if (quiet_moves.size == 0) generate_quiet_moves(b, ss);

                int ply = ss.search_ply(b.ply);
                Move killer_move_1 = ss.killer_1[ply];
                Move killer_move_2 = ss.killer_2[ply];

                // Killer count is used to track which killer we've already tried, if any
                if (killer_count == 0 && mark_killer_if_legal(killer_move_1)) {
                    killer_count++;
                    return killer_move_1;
                } else if (killer_count == 1 && mark_killer_if_legal(killer_move_2)) {
                    killer_count++;
                    return killer_move_2;
                }

                // If we've tried both killers already or don't have any, fall through to the next phase
                phase = QUIET_MOVE;
            case QUIET_MOVE:
                Move next_quiet = quiet_moves.pop();
                while (next_quiet != NULL_MOVE && next_quiet.is_killer) {
                    next_quiet = quiet_moves.pop();
                }

                if (next_quiet != NULL_MOVE) {
                    return next_quiet;
                }

                phase = BAD_CAPTURE;
            case BAD_CAPTURE:
                // No SEE yet - TODO
            default: 
                return NULL_MOVE;
        }
    }

private:

    inline void generate_captures(Board& b) {
        if (b.to_move == WHITE) generate_moves_impl<WHITE, CAPTURES_AND_PROMOTIONS>(b, captures, checkInfo);
        else                    generate_moves_impl<BLACK, CAPTURES_AND_PROMOTIONS>(b, captures, checkInfo);

        sort_captures(b);
    }

    inline void generate_quiet_moves(Board& b, SearchState& ss) {
        if (b.to_move == WHITE) generate_moves_impl<WHITE, QUIET_ONLY>(b, quiet_moves, checkInfo);
        else                    generate_moves_impl<BLACK, QUIET_ONLY>(b, quiet_moves, checkInfo);

        sort_quiet_moves(b, ss);
    }

    inline void sort_captures(Board& b) {
        std::sort(captures.begin(), captures.end(), [&b](Move m1, Move m2) {
            Piece m1_from_piece = b.piece_map[m1.from()];
            Piece m1_to_piece = b.piece_map[m1.to()];
            MoveScore m1_score = CAPTURE_SCORE[m1_from_piece][m1_to_piece];

            Piece m2_from_piece = b.piece_map[m2.from()];
            Piece m2_to_piece = b.piece_map[m2.to()];
            MoveScore m2_score = CAPTURE_SCORE[m2_from_piece][m2_to_piece];

            return m1_score < m2_score;
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

    inline bool mark_killer_if_legal(Move killer_move) {
        for (Move& move : quiet_moves) {
            if (move == killer_move) {
                move.is_killer = true;
                return true;
            }
        }

        return false;
    }
};
