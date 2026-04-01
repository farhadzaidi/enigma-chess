#include "uci.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include "board.hpp"
#include "move.hpp"
#include "params.hpp"
#include "perft.hpp"
#include "print.hpp"
#include "engine.hpp"
#include "transposition_table.hpp"
#include "types.hpp"
#include "notation.hpp"

namespace {

Engine& engine() {
    static Engine instance;
    return instance;
}

struct PendingLimits {
    SearchDepth max_depth = MAX_SEARCH_PLY - 1;
    int soft_time = -1;
    int hard_time = -1;
    uint64_t max_nodes = 0;
};

PendingLimits pending_limits;
bool has_pending_limits = false;

struct TimeLimits {
    int soft_time;
    int hard_time;
};

/** Compute soft and hard time limits from clock state and game phase */
TimeLimits calc_time_limit(Board& b, int remaining, int increment, int movestogo) {
    int moves_left;
    if (movestogo > 0) {
        moves_left = movestogo;
    } else {
        // Estimate remaining moves based on game phase (more material = more moves left)
        float phase_ratio = static_cast<float>(b.game_phase()) / MAX_GAME_PHASE;
        moves_left = prm.moves_left_base + static_cast<int>(prm.moves_left_phase_scale * phase_ratio);
    }

    if (increment == 0) {
        moves_left = std::max(moves_left, prm.min_moves_no_increment);
    }

    int base = remaining / moves_left + static_cast<int>(increment * prm.increment_fraction);
    int soft = base * (increment == 0 ? prm.soft_factor_no_increment : prm.soft_factor_increment);
    int hard = std::min(static_cast<int>(base * prm.hard_factor), remaining / prm.hard_cap_divisor);

    // Emergency scaling: when time is critically low, play fast to avoid flagging
    if (remaining < base * prm.emergency_trigger) {
        soft = remaining / prm.emergency_soft_divisor;
        hard = remaining / prm.emergency_hard_divisor;
    }

    return {soft, hard};
}

/** Respond to "uci" with engine identity, supported options, and "uciok" */
void cmd_uci() {
    uci_print("id name Enigma");
    uci_print("id author Syed Zaidi");

    std::string threads_option = "option name Threads type spin"
        " default " + std::to_string(MIN_THREADS) +
        " min " + std::to_string(MIN_THREADS) +
        " max " + std::to_string(MAX_THREADS);
    uci_print(threads_option);

    std::string hash_option = "option name Hash type spin"
        " default " + std::to_string(DEFAULT_HASH_MB) +
        " min " + std::to_string(MIN_HASH_MB) +
        " max " + std::to_string(MAX_HASH_MB);
    uci_print(hash_option);

    uci_print("option name Ponder type check default false");
    uci_print("option name OwnBook type check default true");

    // --- Tunable search parameters ---
    uci_print("option name AspirationWindow type string");
    uci_print("option name ScoreDropThreshold type string");
    uci_print("option name NullMoveBaseReduction type string");
    uci_print("option name NullMoveDeeperThreshold type string");
    uci_print("option name NullMoveMinDepth type string");
    uci_print("option name ReverseFutilityMarginPerDepth type string");
    uci_print("option name ReverseFutilityMarginBase type string");
    uci_print("option name ReverseFutilityMaxDepth type string");
    uci_print("option name FutilityMarginPerDepth type string");
    uci_print("option name FutilityMarginBase type string");
    uci_print("option name FutilityMaxDepth type string");
    uci_print("option name LMPBase type string");
    uci_print("option name LMPMaxDepth type string");
    uci_print("option name RazoringMargin type string");
    uci_print("option name RazoringMaxDepth type string");
    uci_print("option name SEEPruningMaxDepth type string");
    uci_print("option name ReducedSearchMinDepth type string");
    uci_print("option name ReducedSearchDepthDivisor type string");
    uci_print("option name ReducedSearchMarginMultiplier type string");
    uci_print("option name ReducedSearchTTDepthMargin type string");
    uci_print("option name SEECutoff type string");
    uci_print("option name LMRTuningConstant type string");
    uci_print("option name MinimumIIDDepth type string");
    uci_print("option name IIDDepthDivisor type string");
    uci_print("option name LMRPVReduction type string");
    uci_print("option name BestMoveMinStability type string");
    uci_print("option name NullMoveDeepReduction type string");
    uci_print("option name HistoryMalusDivisor type string");
    
    // --- Tunable time management parameters ---
    uci_print("option name MovesLeftBase type string");
    uci_print("option name MovesLeftPhaseScale type string");
    uci_print("option name MinMovesNoIncrement type string");
    uci_print("option name IncrementFraction type string");
    uci_print("option name SoftFactorNoIncrement type string");
    uci_print("option name SoftFactorIncrement type string");
    uci_print("option name HardFactor type string");
    uci_print("option name HardCapDivisor type string");
    uci_print("option name EmergencyTrigger type string");
    uci_print("option name EmergencySoftDivisor type string");
    uci_print("option name EmergencyHardDivisor type string");

    uci_print("uciok");
}

/** Respond to "isready" with "readyok" */
void cmd_isready() {
    uci_print("readyok");
}

/** Parse "go" parameters and launch the appropriate search mode */
void cmd_go(std::string& cmd, Board& b) {
    int wtime = -1;
    int btime = -1;
    int winc = 0;
    int binc = 0;
    int movestogo = -1;
    bool is_ponder_search = false;
    std::istringstream iss(cmd);
    std::string token;

    int soft_time = -1;
    int hard_time = -1;
    SearchDepth max_depth = MAX_SEARCH_PLY - 1;
    uint64_t max_nodes = 0;
    bool has_depth = false;
    bool has_nodes = false;
    bool has_movetime = false;
    bool is_infinite = false;
    bool has_explicit_limit = false;

    iss >> token;
    while (iss >> token) {
        if (token == "wtime") {
            iss >> wtime;
        } else if (token == "btime") {
            iss >> btime;
        } else if (token == "winc") {
            iss >> winc;
        } else if (token == "binc") {
            iss >> binc;
        } else if (token == "movestogo") {
            iss >> movestogo;
        } else if (token == "movetime") {
            iss >> hard_time;
            has_movetime = true;
            has_explicit_limit = true;
        } else if (token == "nodes") {
            iss >> max_nodes;
            has_nodes = true;
            has_explicit_limit = true;
        } else if (token == "depth") {
            int depth;
            iss >> depth;
            max_depth = std::min(depth, MAX_SEARCH_PLY - 1);
            has_depth = true;
            has_explicit_limit = true;
        } else if (token == "infinite") {
            is_infinite = true;
            has_explicit_limit = true;
        } else if (token == "ponder") {
            is_ponder_search = true;
        }
    }

    if (!has_explicit_limit) {
        hard_time = 50;

        int remaining = (b.to_move() == WHITE) ? wtime : btime;
        int increment = (b.to_move() == WHITE) ? winc : binc;

        if (remaining != -1) {
            TimeLimits limits = calc_time_limit(b, remaining, increment, movestogo);
            soft_time = limits.soft_time;
            hard_time = limits.hard_time;
        }
    }

    engine().stop();

    if (is_ponder_search) {
        pending_limits = {max_depth, soft_time, hard_time, max_nodes};
        has_pending_limits = true;
        engine().search_infinite(b);
    } else if (is_infinite) {
        engine().search_infinite(b);
    } else {
        engine().search(b, max_depth, soft_time, hard_time, max_nodes);
    }
}

/** Convert an infinite ponder search into a bounded search */
void cmd_ponderhit() {
    if (has_pending_limits) {
        engine().apply_limits(
            pending_limits.max_depth,
            pending_limits.soft_time,
            pending_limits.hard_time,
            pending_limits.max_nodes
        );
        has_pending_limits = false;
    }
}

/** Halt the current search immediately */
void cmd_stop() {
    engine().stop();
}

void cmd_quit() {
    engine().stop();
}

/** Handle "setoption name ... value ..." for Threads, Hash, OwnBook, Ponder */
void cmd_setoption(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::string token;
    std::string name;
    std::string value;

    iss >> token;
    iss >> token;
    iss >> name;
    iss >> token;
    iss >> value;

    if (name == "Threads") {
        engine().set_threads(std::clamp(std::stoi(value), MIN_THREADS, MAX_THREADS));
    } else if (name == "Hash") {
        g_tt.resize(std::stoi(value));
    } else if (name == "OwnBook") {
        engine().set_use_opening_book(value == "true");
    } else if (name == "Ponder") {
        // Accepted but unused; go ponder is always respected.

    // --- Tunable search parameters ---
    } else if (name == "AspirationWindow") {
        prm.aspiration_window = std::stoi(value);
    } else if (name == "ScoreDropThreshold") {
        prm.score_drop_threshold = std::stoi(value);
    } else if (name == "NullMoveBaseReduction") {
        prm.null_move_base_reduction = std::stoi(value);
    } else if (name == "NullMoveDeeperThreshold") {
        prm.null_move_deeper_threshold = std::stoi(value);
    } else if (name == "NullMoveMinDepth") {
        prm.null_move_min_depth = std::stoi(value);
    } else if (name == "ReverseFutilityMarginPerDepth") {
        prm.reverse_futility_margin_per_depth = std::stoi(value);
    } else if (name == "ReverseFutilityMarginBase") {
        prm.reverse_futility_margin_base = std::stoi(value);
    } else if (name == "ReverseFutilityMaxDepth") {
        prm.reverse_futility_max_depth = std::stoi(value);
    } else if (name == "FutilityMarginPerDepth") {
        prm.futility_margin_per_depth = std::stoi(value);
    } else if (name == "FutilityMarginBase") {
        prm.futility_margin_base = std::stoi(value);
    } else if (name == "FutilityMaxDepth") {
        prm.futility_max_depth = std::stoi(value);
    } else if (name == "LMPBase") {
        prm.lmp_base = std::stoi(value);
    } else if (name == "LMPMaxDepth") {
        prm.lmp_max_depth = std::stoi(value);
    } else if (name == "RazoringMargin") {
        prm.razoring_margin = std::stoi(value);
    } else if (name == "RazoringMaxDepth") {
        prm.razoring_max_depth = std::stoi(value);
    } else if (name == "SEEPruningMaxDepth") {
        prm.see_pruning_max_depth = std::stoi(value);
    } else if (name == "ReducedSearchMinDepth") {
        prm.reduced_search_min_depth = std::stoi(value);
    } else if (name == "ReducedSearchDepthDivisor") {
        prm.reduced_search_depth_divisor = std::stoi(value);
    } else if (name == "ReducedSearchMarginMultiplier") {
        prm.reduced_search_margin_multiplier = std::stoi(value);
    } else if (name == "ReducedSearchTTDepthMargin") {
        prm.reduced_search_tt_depth_margin = std::stoi(value);
    } else if (name == "SEECutoff") {
        prm.see_cutoff = std::stoi(value);
    } else if (name == "LMRTuningConstant") {
        prm.lmr_tuning_constant = std::stod(value);
        // Rebuild LMR table if the tuning constant changed
        build_lmr_table();
    } else if (name == "MinimumIIDDepth") {
        prm.minimum_iid_depth = std::stoi(value);
    } else if (name == "IIDDepthDivisor") {
        prm.iid_depth_divisor = std::stoi(value);
    } else if (name == "LMRPVReduction") {
        prm.lmr_pv_reduction = std::stoi(value);
    } else if (name == "BestMoveMinStability") {
        prm.best_move_min_stability = std::stoi(value);
    } else if (name == "NullMoveDeepReduction") {
        prm.null_move_deep_reduction = std::stoi(value);
    } else if (name == "HistoryMalusDivisor") {
        prm.history_malus_divisor = std::stoi(value);

    // --- Tunable time management parameters ---
    } else if (name == "MovesLeftBase") {
        prm.moves_left_base = std::stoi(value);
    } else if (name == "MovesLeftPhaseScale") {
        prm.moves_left_phase_scale = std::stoi(value);
    } else if (name == "MinMovesNoIncrement") {
        prm.min_moves_no_increment = std::stoi(value);
    } else if (name == "IncrementFraction") {
        prm.increment_fraction = std::stod(value);
    } else if (name == "SoftFactorNoIncrement") {
        prm.soft_factor_no_increment = std::stod(value);
    } else if (name == "SoftFactorIncrement") {
        prm.soft_factor_increment = std::stod(value);
    } else if (name == "HardFactor") {
        prm.hard_factor = std::stod(value);
    } else if (name == "HardCapDivisor") {
        prm.hard_cap_divisor = std::stoi(value);
    } else if (name == "EmergencyTrigger") {
        prm.emergency_trigger = std::stoi(value);
    } else if (name == "EmergencySoftDivisor") {
        prm.emergency_soft_divisor = std::stoi(value);
    } else if (name == "EmergencyHardDivisor") {
        prm.emergency_hard_divisor = std::stoi(value);
    }
}

/** Reset engine state and board for a new game */
void cmd_ucinewgame(Board& b) {
    engine().clear();
    b.reset();
}

/** Set up the board from "position startpos/fen ... moves ..." */
void cmd_position(const std::string& cmd, Board& b) {
    engine().stop();

    std::istringstream iss(cmd);
    std::string token;
    iss >> token >> token;

    if (token == "startpos") {
        b.load_from_fen();
        iss >> token;
    } else if (token == "fen") {
        std::string fen;
        while (iss >> token && token != "moves") {
            fen += token + " ";
        }
        b.load_from_fen(fen);
    }

    if (token == "moves") {
        while (iss >> token) {
            Move move = encode_move_from_uci(b, token);
            b.make_move(move);
        }
    }
}

/** Parse "perft <depth> [fen]" and run perft on the current or given position */
void cmd_perft(const std::string& cmd, Board& b) {
    std::istringstream iss(cmd);
    std::string token;
    iss >> token; // consume "perft"

    int depth;
    if (!(iss >> depth) || depth < 1) {
        uci_print("Usage: perft <depth> [fen]");
        return;
    }
    depth = std::min(depth, MAX_SEARCH_PLY - 1);

    std::string fen;
    std::getline(iss >> std::ws, fen);
    if (!fen.empty()) {
        b.load_from_fen(fen);
    }

    set_uci_silent(true);
    perft<true>(b, depth);
    set_uci_silent(false);
}

/** Parse "search <depth> [fen]" and run a fixed-depth search */
void cmd_search(const std::string& cmd, Board& b) {
    std::istringstream iss(cmd);
    std::string token;
    iss >> token; // consume "search"

    int depth;
    if (!(iss >> depth) || depth < 1) {
        uci_print("Usage: search <depth> [fen]");
        return;
    }
    depth = std::min(depth, MAX_SEARCH_PLY - 1);

    std::string fen;
    std::getline(iss >> std::ws, fen);
    if (!fen.empty()) {
        b.load_from_fen(fen);
    }

    engine().sync_search_depth(b, depth);
}

} // namespace

void uci_loop() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Board b;
    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        if (cmd == "uci") {
            cmd_uci();
        } else if (cmd.starts_with("setoption")) {
            cmd_setoption(cmd);
        } else if (cmd == "isready") {
            cmd_isready();
        } else if (cmd == "ucinewgame") {
            cmd_ucinewgame(b);
        } else if (cmd.starts_with("position")) {
            cmd_position(cmd, b);
        } else if (cmd.starts_with("go")) {
            cmd_go(cmd, b);
        } else if (cmd == "ponderhit") {
            cmd_ponderhit();
        } else if (cmd == "stop") {
            cmd_stop();
        } else if (cmd.starts_with("perft")) {
            cmd_perft(cmd, b);
        } else if (cmd.starts_with("search")) {
            cmd_search(cmd, b);
        } else if (cmd == "quit") {
            cmd_quit();
            break;
        } else {
            uci_print("Unknown command: '" + cmd + "'");
        }
    }
}
