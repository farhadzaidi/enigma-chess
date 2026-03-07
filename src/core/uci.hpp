#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

#include "core/types.hpp"
#include "core/constants.hpp"
#include "board/board.hpp"
#include "eval/constants.hpp"
#include "core/move.hpp"
#include "core/globals.hpp"
#include "core/transposition_table.hpp"
#include "search/search.hpp"
#include "search/search_state.hpp"
#include "utils/notation.hpp"
#include "eval/pawn_table.hpp"

namespace {

// --- Time Management ---

struct SearchTime {
    int soft_limit;
    int hard_limit;
};

inline SearchTime calc_time_limit(Board& b, int remaining, int increment, int movestogo) {
    int moves_left;
    if (movestogo > 0) {
        moves_left = movestogo;
    } else {
        float phase_ratio = (float)b.game_phase / MAX_GAME_PHASE;
        moves_left = 10 + (int)(30 * phase_ratio);
    }

    int base = remaining / moves_left + increment / 2;
    int soft_limit = base * 0.6;
    int hard_limit = std::min((int)(base * 2.5), remaining / 3);

    return {soft_limit, hard_limit};
}

// functions

// Stops the search and joins the thread to prevent any dangling threads/race conditions
inline void clean_up_thread() {
    g_stop_requested = true;
    g_pondering = false;

    if (g_search_thread.joinable()) {
        g_search_thread.join();
    }

    g_stop_requested = false;
}

inline void print(const std::string& str) {
    std::cout << str << "\n";
    std::cout.flush();
}

// --- UCI Commands ---

inline void cmd_uci() {
    print("id name Enigma");
    print("id author Syed Zaidi");
    print("option name Ponder type check default false");
    print("option name OwnBook type check default true");
    print("uciok");
}

inline void cmd_isready() {
    print("readyok");
}

inline void cmd_go(std::string& cmd, Board& b) {
    g_transposition_table.generation++;

    // Parse go command
    int wtime = -1, btime = -1, winc = 0, binc = 0;
    int movestogo = -1;
    int nodes = -1, depth = -1;
    bool infinite = false;
    bool is_ponder_search = false;
    std::istringstream iss(cmd);
    std::string token;

    int soft_time = -1;
    int hard_time = -1;

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
        } else if (token == "nodes") {
            iss >> nodes;
        } else if (token == "depth") {
            iss >> depth;
        } else if (token == "infinite") {
            infinite = true;
        } else if (token == "ponder") {
            is_ponder_search = true;
        }
    }

    bool do_ponder = g_enable_ponder && is_ponder_search;

    SearchMode search_mode;
    if (hard_time != -1) {
        search_mode = SearchMode::Time;
    } else if (nodes != -1) {
        search_mode = SearchMode::Nodes;
    } else if (depth != -1) {
        search_mode = SearchMode::Depth;
    } else if (infinite) {
        search_mode = SearchMode::Infinite;
    } else {
        // If we're not explicitly told how to search, then we attempt
        // compute search time by looking at time controls
        search_mode = SearchMode::Time;

        // Default time limit in milliseconds in case time controls
        // haven't been specified
        hard_time = 50;

        // Determine how long to search for
        if (b.to_move == WHITE && wtime != -1) {
            auto result = calc_time_limit(b, wtime, winc, movestogo);
            soft_time = result.soft_limit;
            hard_time = result.hard_limit;
        } else if (b.to_move == BLACK && btime != -1) {
            auto result = calc_time_limit(b, btime, binc, movestogo);
            soft_time = result.soft_limit;
            hard_time = result.hard_limit;
        }
    }

    // Create new search thread and start the search
    clean_up_thread();
    g_pondering = do_ponder;

    g_search_thread = std::thread([&b, search_mode, soft_time, hard_time, nodes, depth]() {
        Move best_move;

        if (search_mode == SearchMode::Time) {
            best_move = search_time(b, soft_time, hard_time);
        } else if (search_mode == SearchMode::Nodes) {
            best_move = search_nodes(b, nodes);
        } else if (search_mode == SearchMode::Depth) {
            best_move = search_depth(b, std::min(depth, MAX_SEARCH_PLY - 1));
        } else if (search_mode == SearchMode::Infinite) {
            best_move = search_infinite(b);
        }

        // Extract ponder move by probing TT after making bestmove
        std::string ponder_str;
        if (best_move != NULL_MOVE) {
            b.make_move(best_move);
            TTEntry* tt_entry = g_transposition_table.get_entry(b.position_hash);
            if (tt_entry && tt_entry->best_move != NULL_MOVE) {
                ponder_str = " ponder " + decode_move_to_uci(tt_entry->best_move);
            }
            b.unmake_move(best_move);
        }

        // Wait while pondering -- ponderhit or stop will release us
        while (g_pondering && !g_stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        print("bestmove " + decode_move_to_uci(best_move) + ponder_str);
    });
}

inline void cmd_ponderhit() {
    // Reset deadlines relative to now, then release the pondering flag.
    // Write ordering matters: deadline before pondering (release-acquire).
    auto now = std::chrono::steady_clock::now();
    g_search_state.deadline = now + std::chrono::milliseconds(g_search_state.limits.hard_time);
    if (g_search_state.limits.soft_time != -1) {
        g_search_state.soft_deadline = now + std::chrono::milliseconds(g_search_state.limits.soft_time);
    }
    g_pondering = false;
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

    // Parse: setoption name <name> value <value>
    iss >> token; // "setoption"
    iss >> token; // "name"
    iss >> name;
    iss >> token; // "value"
    iss >> value;

    if (name == "OwnBook") {
        g_use_own_book = (value == "true");
    } else if (name == "Ponder") {
        g_enable_ponder = (value == "true");
    }
}

inline void cmd_ucinewgame(Board& b) {
    clean_up_thread();
    b.reset();
    g_transposition_table.clear();
    g_pawn_table.clear();
}

inline void cmd_position(const std::string& cmd, Board& b) {
    clean_up_thread();

    std::istringstream iss(cmd);
    std::string token;
    iss >> token >> token; // Discard "position" token and read next

    // Load from standard start position
    if (token == "startpos") {
        b.load_from_fen();
        iss >> token;
    } else if (token == "fen") {
        // Load from provided FEN
        // Read FEN until the token is "moves" or we reach the end of the command
        std::string fen;
        while (iss >> token && token != "moves") {
            fen += token + " ";
        }
        b.load_from_fen(fen);
    }

    // Make moves on the board if they are provided
    if (token == "moves") {
        while (iss >> token) {
            Move move = encode_move_from_uci(b, token);
            b.make_move(move);
        }
    }
}

} // namespace

inline void uci_loop() {
    // Remove sync with stdio to improve performance
    std::ios::sync_with_stdio(false);

    // Untie cin from cout to prevent automatic flushing (will be manually controlled)
    std::cin.tie(nullptr);

    // Create board object
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
            print("Unknown command: '" + cmd + "'");
        }
    }
}
