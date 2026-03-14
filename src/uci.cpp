#include "uci.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include "board.hpp"
#include "evaluate.hpp"
#include "move.hpp"
#include "utils/print.hpp"
#include "engine.hpp"
#include "transposition_table.hpp"
#include "types.hpp"
#include "utils/notation.hpp"

namespace {

Engine g_engine;

int pending_soft_time = -1;
int pending_hard_time = -1;
bool has_pending_limits = false;
bool ponder_enabled = false;

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
        moves_left = 10 + static_cast<int>(30 * phase_ratio);
    }

    int base = remaining / moves_left + increment / 2;
    int soft = base * 0.6;
    int hard = std::min(static_cast<int>(base * 2.5), remaining / 3);
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

        if (b.to_move() == WHITE && wtime != -1) {
            auto limits = calc_time_limit(b, wtime, winc, movestogo);
            soft_time = limits.soft_time;
            hard_time = limits.hard_time;
        } else if (b.to_move() == BLACK && btime != -1) {
            auto limits = calc_time_limit(b, btime, binc, movestogo);
            soft_time = limits.soft_time;
            hard_time = limits.hard_time;
        }
    }

    g_engine.stop();

    bool should_ponder = ponder_enabled && is_ponder_search;

    if (should_ponder) {
        pending_soft_time = soft_time;
        pending_hard_time = hard_time;
        has_pending_limits = true;
        g_engine.search_infinite(b);
    } else if (has_depth) {
        g_engine.search_depth(b, max_depth);
    } else if (has_nodes) {
        g_engine.search_nodes(b, max_nodes);
    } else if (is_infinite) {
        g_engine.search_infinite(b);
    } else {
        g_engine.search_time(b, soft_time, hard_time);
    }
}

/** Convert an infinite ponder search into a timed search */
void cmd_ponderhit() {
    if (has_pending_limits) {
        g_engine.apply_time(pending_soft_time, pending_hard_time);
        has_pending_limits = false;
    }
}

/** Halt the current search immediately */
void cmd_stop() {
    g_engine.stop();
}

void cmd_quit() {
    g_engine.stop();
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
        g_engine.set_threads(std::clamp(std::stoi(value), MIN_THREADS, MAX_THREADS));
    } else if (name == "Hash") {
        g_tt.resize(std::stoi(value));
    } else if (name == "OwnBook") {
        g_engine.set_use_opening_book(value == "true");
    } else if (name == "Ponder") {
        ponder_enabled = (value == "true");
    }
}

/** Reset engine state and board for a new game */
void cmd_ucinewgame(Board& b) {
    g_engine.clear();
    b.reset();
}

/** Set up the board from "position startpos/fen ... moves ..." */
void cmd_position(const std::string& cmd, Board& b) {
    g_engine.stop();

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
        } else if (cmd == "quit") {
            cmd_quit();
            break;
        } else {
            uci_print("Unknown command: '" + cmd + "'");
        }
    }
}
