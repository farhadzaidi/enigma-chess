#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <iomanip>

#include "bench.hpp"
#include "uci.hpp"
#include "board.hpp"
#include "perft.hpp"
#include "utils.hpp"
#include "search.hpp"

#include "../tests/test.hpp"

int main(int argc, char* argv[]) {
    // Extract command line arguments
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }

    // ### MAIN UCI LOOP
    if (args.empty()) {
        uci_loop();
        return EXIT_SUCCESS;
    }

    std::string cmd = args[0];

    // ### BENCH - Comprehensive move generation test suite
    if (cmd == "bench") {
        BenchFlags flags = {false, false, false, false, false};

        for (int i = 1; i < args.size(); i++) {
            if (args[i] == "--verbose"){
                flags.verbose = true;
            } else if (args[i] == "--fast") {
                flags.fast = true;
            } else if (args[i] == "--phased") {
                flags.phased = true;
            } else if (args[i] == "--movegen") {
                flags.movegen_only = true;
            } else if (args[i] == "--engine") {
                flags.engine_only = true;
            } else {
                std::clog << "Error: Unknown option for bench '" << args[i] << "'\n";
                return EXIT_FAILURE;
            }
        }

        run_bench(flags);
    } 
    
    // ### PERFT / SEARCH - Test move generation or search a position
    else if (cmd == "perft" || cmd == "search") {
        // Require depth
        if (args.size() == 1) {
            std::clog << "Error: Please specify depth\n";
            return EXIT_FAILURE;
        }

        // Validate depth
        std::string depth_str = args[1];
        if (!is_pos_int(depth_str)) {
            std::clog << "Error: Invalid depth\n";
            return EXIT_FAILURE;
        }

        // Clamp max search depth
        int requested_depth = std::stoi(depth_str);
        SearchDepth depth = static_cast<SearchDepth>(std::min(requested_depth, MAX_SEARCH_PLY - 1));

        // Parse optional FEN
        Board b;
        if (args.size() >= 3) {
            // FEN string is all remaining arguments joined by spaces
            std::string fen;
            for (int i = 2; i < args.size(); i++) {
                if (i > 2) fen += " ";
                fen += args[i];
            }
            b.load_from_fen(fen);
        } else {
            b.load_from_fen();
        }

        if (cmd == "perft") {
            perft<true>(b, depth);
        } else {
            Move best_move = search_depth(b, depth);
            std::cout << "Best move: " << decode_move_to_uci(best_move) << "\n";
        }
    } 
    
    // ### TEST - Run test suite
    else if (cmd == "test") {
        std::vector<std::string> tests(args.begin() + 1, args.end());

        for (const auto& test : tests) {
            if (std::find(TEST_NAMES.begin(), TEST_NAMES.end(), test) == TEST_NAMES.end()) {
                std::clog << "Error: Unknown test '" << test << "'\n";
                return EXIT_FAILURE;
            }
        }

        int failures = run_tests(tests);
        if (failures > 0) {
            std::clog << failures << " test(s) failed.\n";
            return EXIT_FAILURE;
        }
        std::clog << "All tests passed.\n";
    }
    
    // ### DEBUG - Runs in debug mode
    else if  (cmd == "debug") {
        Board b;
        b.load_from_fen();
        b.debug();
    } 
    
    else {
        std::clog << "Error: Unknown argument " << "'" << args[0] << "'\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
