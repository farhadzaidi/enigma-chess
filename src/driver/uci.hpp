#pragma once

#include <algorithm>
#include <sstream>
#include <string>

#include "board/board.hpp"
#include "constants.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include "types.hpp"
#include "driver/thread_pool.hpp"
#include "eval/constants.hpp"
#include "eval/eval.hpp"
#include "search/context.hpp"
#include "search/search.hpp"
#include "utils/notation.hpp"

namespace {

ThreadPool g_thread_pool;
SearchLimits pending_limits;
int num_threads = MIN_THREADS;
bool ponder_enabled = false;

inline SearchLimits calc_time_limit(Board& b, int remaining, int increment, int movestogo) {
    int moves_left;
    if (movestogo > 0) {
        moves_left = movestogo;
    } else {
        float phase_ratio = (float)b.game_phase / MAX_GAME_PHASE;
        moves_left = 10 + (int)(30 * phase_ratio);
    }

    SearchLimits limits;
    int base = remaining / moves_left + increment / 2;
    limits.soft_time = base * 0.6;
    limits.hard_time = std::min((int)(base * 2.5), remaining / 3);
    return limits;
}

inline void clean_up_thread() {
    pending_limits = SearchLimits{};
    g_thread_pool.stop();
}

inline void cmd_uci() {
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

inline void cmd_isready() {
    uci_print("readyok");
}

inline void cmd_go(std::string& cmd, Board& b) {
    int wtime = -1, btime = -1, winc = 0, binc = 0;
    int movestogo = -1;
    bool is_ponder_search = false;
    bool has_explicit_limit = false;
    std::istringstream iss(cmd);
    std::string token;

    SearchLimits limits;

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
            iss >> limits.hard_time;
            has_explicit_limit = true;
        } else if (token == "nodes") {
            iss >> limits.max_nodes;
            has_explicit_limit = true;
        } else if (token == "depth") {
            int depth;
            iss >> depth;
            limits.max_depth = std::min(depth, MAX_SEARCH_PLY - 1);
            has_explicit_limit = true;
        } else if (token == "infinite") {
            has_explicit_limit = true;
        } else if (token == "ponder") {
            is_ponder_search = true;
        }
    }

    if (!has_explicit_limit) {
        limits.hard_time = 50;

        if (b.to_move == WHITE && wtime != -1) {
            limits = calc_time_limit(b, wtime, winc, movestogo);
        } else if (b.to_move == BLACK && btime != -1) {
            limits = calc_time_limit(b, btime, binc, movestogo);
        }
    }

    clean_up_thread();

    bool should_ponder = ponder_enabled && is_ponder_search;

    if (should_ponder) {
        pending_limits = limits;
        limits = SearchLimits{};
    } else {
        pending_limits = SearchLimits{};
    }

    g_thread_pool.start(
        b,
        limits,
        num_threads
    );
}

inline void cmd_ponderhit() {
    g_thread_pool.apply_limits(pending_limits);
    pending_limits = SearchLimits{};
}

inline void cmd_stop() {
    clean_up_thread();
}

inline void cmd_quit() {
    clean_up_thread();
}

inline void cmd_setoption(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::string token, name, value;

    iss >> token;
    iss >> token;
    iss >> name;
    iss >> token;
    iss >> value;

    if (name == "Threads") {
        num_threads = std::clamp(std::stoi(value), MIN_THREADS, MAX_THREADS);
    } else if (name == "Hash") {
        g_shared.transposition_table.resize(std::stoi(value));
    } else if (name == "OwnBook") {
        g_shared.use_opening_book = (value == "true");
    } else if (name == "Ponder") {
        ponder_enabled = (value == "true");
    }
}

inline void cmd_ucinewgame(Board& b) {
    g_thread_pool.clear();
    b.reset();
    clear_eval_cache();
}

inline void cmd_position(const std::string& cmd, Board& b) {
    clean_up_thread();

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

inline void uci_loop() {
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
