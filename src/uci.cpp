#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

#include "types.hpp"
#include "board.hpp"
#include "search.hpp"
#include "search_state.hpp"
#include "utils.hpp"
#include "move.hpp"
#include "transposition_table.hpp"
#include "pawn_table.hpp"

std::thread search_thread;
std::atomic<bool> stop_requested(false);
std::atomic<bool> pondering(false);
bool use_own_book = true;
bool enable_ponder = false;

struct SearchTime {
    int soft_limit;
    int hard_limit;
};

// Stops the search and joins the thread to prevent any dangling threads/race conditions
static void clean_up_thread() {
    stop_requested = true;
    pondering = false;

    if (search_thread.joinable()) {
        search_thread.join();
    }

    stop_requested = false;
}

// Calculates how much time to spend on the search in milliseconds
static SearchTime calc_time_limit(Board& b, int remaining, int increment, int movestogo) {
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

static void print(const std::string& str) {
    std::cout << str << "\n";
    std::cout.flush();
}

static void cmd_uci() {
    print("id name Enigma");
    print("id author Syed Zaidi");
    print("option name Ponder type check default false");
    print("option name OwnBook type check default true");
    print("uciok");
}

static void cmd_setoption(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::string token, name, value;

    // Parse: setoption name <name> value <value>
    iss >> token; // "setoption"
    iss >> token; // "name"
    iss >> name;
    iss >> token; // "value"
    iss >> value;

    if (name == "OwnBook") {
        use_own_book = (value == "true");
    } else if (name == "Ponder") {
        enable_ponder = (value == "true");
    }
}

static void cmd_isready() {
    print("readyok");
}

static void cmd_ucinewgame(Board& b) {
    clean_up_thread();
    b.reset();
    TT.clear();
    PT.clear();
}

static void cmd_position(const std::string& cmd, Board& b) {
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

static void cmd_go(std::string& cmd, Board& b) {
    TT.generation++;

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

    bool do_ponder = enable_ponder && is_ponder_search;

    SearchMode search_mode;
    if (hard_time != -1) {
        search_mode = TIME;
    } else if (nodes != -1) {
        search_mode = NODES;
    } else if (depth != -1) {
        search_mode = DEPTH;
    } else if (infinite) {
        search_mode = INFINITE;
    } else {
        // If we're not explicitly told how to search, then we attempt
        // compute search time by looking at time controls
        search_mode = TIME;

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
    pondering = do_ponder;

    search_thread = std::thread([&b, search_mode, soft_time, hard_time, nodes, depth]() {
        Move best_move;

        if (search_mode == TIME) {
            best_move = search_time(b, soft_time, hard_time);
        } else if (search_mode == NODES) {
            best_move = search_nodes(b, nodes);
        } else if (search_mode == DEPTH) {
            best_move = search_depth(b, std::min(depth, MAX_SEARCH_PLY - 1));
        } else if (search_mode == INFINITE) {
            best_move = search_infinite(b);
        }

        // Extract ponder move by probing TT after making bestmove
        std::string ponder_str;
        if (best_move != NULL_MOVE) {
            b.make_move(best_move);
            TTEntry* tt_entry = TT.get_entry(b.zobrist_hash);
            if (tt_entry && tt_entry->best_move != NULL_MOVE) {
                ponder_str = " ponder " + decode_move_to_uci(tt_entry->best_move);
            }
            b.unmake_move(best_move);
        }

        // Wait while pondering — ponderhit or stop will release us
        while (pondering && !stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        print("bestmove " + decode_move_to_uci(best_move) + ponder_str);
    });
}

static void cmd_debug() {
    // TODO
}

static void cmd_register() {
    // TODO
}

static void cmd_ponderhit() {
    // Reset deadlines relative to now, then release the pondering flag.
    // Write ordering matters: deadline before pondering (release-acquire).
    auto now = std::chrono::steady_clock::now();
    ss.deadline = now + std::chrono::milliseconds(ss.limits.hard_time);
    if (ss.limits.soft_time != -1) {
        ss.soft_deadline = now + std::chrono::milliseconds(ss.limits.soft_time);
    }
    pondering = false;
}

static void cmd_stop() {
    clean_up_thread();
}

static void cmd_quit() {
    clean_up_thread();
}

void uci_loop() {
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
        } else if (cmd.starts_with("debug")) {
            cmd_debug();
        } else if (cmd == "register") {
            cmd_register();
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
