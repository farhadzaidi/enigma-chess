#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>
#include <algorithm>

#include "core/uci.hpp"
#include "board/board.hpp"
#include "move_generator/perft.hpp"
#include "utils/notation.hpp"
#include "search/search.hpp"
#include "move_generator/move_generator.hpp"

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }

    if (args.empty()) {
        uci_loop();
        return EXIT_SUCCESS;
    }

    std::string cmd = args[0];

    if (cmd == "perft" || cmd == "search") {
        if (args.size() == 1) {
            std::clog << "Error: Please specify depth\n";
            return EXIT_FAILURE;
        }

        std::string depth_str = args[1];
        if (!is_pos_int(depth_str)) {
            std::clog << "Error: Invalid depth\n";
            return EXIT_FAILURE;
        }

        int requested_depth = std::stoi(depth_str);
        SearchDepth depth = std::min(requested_depth, MAX_SEARCH_PLY - 1);

        Board b;
        if (args.size() >= 3) {
            std::string fen;
            for (size_t i = 2; i < args.size(); i++) {
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

    else {
        std::clog << "Error: Unknown argument '" << args[0] << "'\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
