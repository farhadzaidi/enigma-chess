#pragma once

#include <algorithm>
#include <array>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "core/move.hpp"
#include "move_generator/check_info.hpp"
#include "move_generator/move_generator.hpp"
#include "search/search_state.hpp"
#include "search/see.hpp"

namespace {

// constants
constexpr std::array<std::array<MoveScore, NUM_PIECES>, NUM_PIECES> MVV_LVA_TABLE = {{
    {106, 206, 306, 406, 506, 0},
    {105, 205, 305, 405, 505, 0},
    {104, 204, 304, 404, 504, 0},
    {103, 203, 303, 403, 503, 0},
    {102, 202, 302, 402, 502, 0},
    {101, 201, 301, 401, 501, 0},
}};

constexpr int NUM_MOVE_FLAGS = 7;
constexpr std::array<MoveScore, NUM_MOVE_FLAGS> PROMOTION_BONUS = {
    0, 0, 0, // Non-promotion flags
    200, // Bishop
    300, // Knight
    200, // Rook
    600, // Queen
};

} // namespace


struct MoveSelector {
    MoveSelPhase phase;
    CheckInfo check_info;
    MoveList tactical_moves;
    MoveList bad_captures;
    MoveList quiet_moves;
    Move prev_best_move;
    Move tt_move;
    Move returned_killer_1;
    Move returned_killer_2;
    bool tacticals_generated = false;
    bool quiets_generated = false;
    bool bad_captures_sorted = false;

    MoveSelector(Board& b, Move tt_move, Move prev_best_move = NULL_MOVE)
        : phase(MoveSelPhase::PrevBest), prev_best_move(prev_best_move), tt_move(tt_move) {
        if (b.to_move == WHITE) check_info.compute_check_info<WHITE>(b);
        else                    check_info.compute_check_info<BLACK>(b);
    }

    inline bool in_tactical_phase() const {
        return phase <= MoveSelPhase::Killer;
    }

    Move next_move(Board& b, SearchState& ss) {
        switch (phase) {
            case MoveSelPhase::PrevBest: {
                if (prev_best_move != NULL_MOVE) {
                    phase = MoveSelPhase::TT;
                    return prev_best_move;
                }

                phase = MoveSelPhase::TT;
                [[fallthrough]];
            }
            case MoveSelPhase::TT: {
                // Return the TT move if we have one
                if (
                    tt_move != NULL_MOVE &&
                    tt_move != prev_best_move &&
                    b.is_legal_move(tt_move)
                ) {
                    phase = MoveSelPhase::Tactical;
                    return tt_move;
                }

                // If no TT moves, change phase and fall through
                phase = MoveSelPhase::Tactical;
                [[fallthrough]];
            }
            case MoveSelPhase::Tactical: {
                // If we just entered the tactical phase, generate all tactical moves
                if (!tacticals_generated) generate_tactical_moves(b);

                // Moves are already sorted by score when generated, so we can pop the next best move
                Move next_tactical = pop_next(tactical_moves);
                if (next_tactical != NULL_MOVE) return next_tactical;

                // If we don't have anymore tactical moves, change phase and fall through
                phase = MoveSelPhase::Killer;
                [[fallthrough]];
            }
            case MoveSelPhase::Killer: {
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
                phase = MoveSelPhase::Quiet;
                [[fallthrough]];
            }
            case MoveSelPhase::Quiet: {
                if (!quiets_generated) generate_quiet_moves(b, ss);

                Move next_quiet = pop_next(quiet_moves);
                if (next_quiet != NULL_MOVE) return next_quiet;

                phase = MoveSelPhase::BadCapture;
                [[fallthrough]];
            }
            case MoveSelPhase::BadCapture: {
                // We lazy sort bad captures only once we're at this phase
                if (!bad_captures_sorted) {
                    sort_tactical_moves(b, bad_captures);
                    bad_captures_sorted = true;
                }

                Move next_bad = pop_next(bad_captures);
                if (next_bad != NULL_MOVE) return next_bad;
                return NULL_MOVE;
            }
            default:
                return NULL_MOVE;
        }
    }

private:

    inline void generate_tactical_moves(Board& b) {
        MoveList all_tacticals;
        if (b.to_move == WHITE) generate_moves_impl<WHITE, MoveGenMode::TacticalOnly>(b, all_tacticals, check_info);
        else                    generate_moves_impl<BLACK, MoveGenMode::TacticalOnly>(b, all_tacticals, check_info);

        // Split captures into good tacticals and bad captures based on SEE
        // Promotions without capture are always good (free material)
        for (const Move move : all_tacticals) {
            if (move.type() == MoveType::Capture && see(b, move) < 0) {
                bad_captures.add(move);
            } else {
                tactical_moves.add(move);
            }
        }

        sort_tactical_moves(b, tactical_moves);
        tacticals_generated = true;
    }

    inline void generate_quiet_moves(Board& b, SearchState& ss) {
        if (b.to_move == WHITE) generate_moves_impl<WHITE, MoveGenMode::QuietOnly>(b, quiet_moves, check_info);
        else                    generate_moves_impl<BLACK, MoveGenMode::QuietOnly>(b, quiet_moves, check_info);

        sort_quiet_moves(b, ss);
        quiets_generated = true;
    }

    inline MoveScore get_tactical_score(const Board& b, Move move) {
        MoveScore score = 0;

        if (move.type() == MoveType::Capture) {
            Piece attacker = b.piece_map[move.from()];
            Piece victim = move.flag() == MoveFlag::EnPassant ? PAWN : b.piece_map[move.to()];
            score += MVV_LVA_TABLE[attacker][victim];
        }

        if (move.is_promotion()) {
            score += PROMOTION_BONUS[static_cast<int>(move.flag())];
        }

        return score;
    }

    inline void sort_tactical_moves(Board& b, MoveList& moves) {
        std::sort(moves.begin(), moves.end(), [&](Move m1, Move m2) {
            return get_tactical_score(b, m1) < get_tactical_score(b, m2);
        });
    }

    inline void sort_quiet_moves(Board& b, SearchState& ss) {
        std::sort(quiet_moves.begin(), quiet_moves.end(), [&b, &ss](Move m1, Move m2) {
            Square m1_from = m1.from();
            Square m1_to = m1.to();
            Piece m1_piece = b.piece_map[m1_from];
            MoveScore m1_score = ss.side_piece_to_history[b.to_move][m1_piece][m1_to] + ss.from_to_history[m1_from][m1_to];

            Square m2_from = m2.from();
            Square m2_to = m2.to();
            Piece m2_piece = b.piece_map[m2_from];
            MoveScore m2_score = ss.side_piece_to_history[b.to_move][m2_piece][m2_to] + ss.from_to_history[m2_from][m2_to];

            return m1_score < m2_score;
        });
    }

    inline Move pop_next(MoveList& moves) {
        Move next = moves.pop();
        while (next != NULL_MOVE && is_already_returned(next)) {
            next = moves.pop();
        }
        return next;
    }

    inline bool is_already_returned(Move move) {
        return move == prev_best_move || move == tt_move || move == returned_killer_1 || move == returned_killer_2;
    }
};
