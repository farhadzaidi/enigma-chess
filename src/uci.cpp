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
#include "utils.hpp"
#include "move.hpp"
#include "transposition_table.hpp"

std::thread search_thread;
std::atomic<bool> stop_requested(false);

// Stops the search and joins the thread to prevent any dangling threads/race conditions
static void clean_up_thread() {
    stop_requested = true;

    if (search_thread.joinable()) {
        search_thread.join();
    }

    stop_requested = false;
}

// Calculates how much time to spend on the search in milliseconds
static int calc_time_limit(int remaining, int increment) {
    return remaining / 20 + increment / 2;
}

static void print(const std::string& str) {
    std::cout << str << "\n";
    std::cout.flush();
}

static void cmd_uci() {
    print("id name Enigma");
    print("id author Syed Zaidi");
    print("uciok");
}

static void cmd_setoption(const std::string&) {
    // No runtime engine options currently exposed.
}

static void cmd_isready() {
    print("readyok");
}

static void cmd_ucinewgame(Board& b) {
    clean_up_thread();
    b.reset();
    TT.clear();
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
    // Parse go command
    int wtime = -1, btime = -1, winc = 0, binc = 0;
    int movetime = -1, nodes = -1, depth = -1;
    bool infinite = false;
    std::istringstream iss(cmd);
    std::string token;

    // TODO: implement remaining go options
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
        } else if (token == "movetime") {
            iss >> movetime;
        } else if (token == "nodes") {
            iss >> nodes;
        } else if (token == "depth") {
            iss >> depth;
        } else if (token == "infinite") {
            infinite = true;
        }
    }

    SearchMode search_mode;
    if (movetime != -1) {
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
        movetime = 50;

        // Determine how long to search for
        if (b.to_move == WHITE && wtime != -1) {
            movetime = calc_time_limit(wtime, winc);
        } else if (b.to_move == BLACK && btime != -1) {
            movetime = calc_time_limit(btime, binc);
        }
    }

    // Create new search thread and start the search
    clean_up_thread();
    search_thread = std::thread([&b, search_mode, movetime, nodes, depth]() {
        Move best_move;

        if (search_mode == TIME) {
            best_move = search_time(b, movetime);
        } else if (search_mode == NODES) {
            best_move = search_nodes(b, nodes);
        } else if (search_mode == DEPTH) {
            best_move = search_depth(b, std::min(depth, MAX_SEARCH_PLY - 1));
        } else if (search_mode == INFINITE) {
            best_move = search_infinite(b);
        }

        print("bestmove " + decode_move_to_uci(best_move));
    });
}

static void cmd_debug() {
    // TODO
}

static void cmd_register() {
    // TODO
}

static void cmd_ponderhit() {
    // TODO
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
