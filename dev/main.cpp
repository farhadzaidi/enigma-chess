#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>

#include "tests/test.hpp"
#include "bench/bench.hpp"

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }

    if (args.empty()) {
        std::clog << "Usage:\n";
        std::clog << "  enigma-dev test [name ...]\n";
        std::clog << "  enigma-dev bench [movegen|search] [--fast] [--verbose] [--phased] [--threads <n>]\n";
        return EXIT_FAILURE;
    }

    std::string cmd = args[0];

    if (cmd == "test") {
        std::vector<std::string> tests(args.begin() + 1, args.end());

        for (const auto& test : tests) {
            if (!is_valid_test_selector(test)) {
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

    else if (cmd == "bench") {
        std::string type;
        bool verbose = false;
        bool fast = false;
        bool phased = false;
        int threads = 1;
        size_t flags_start = 1;

        if (args.size() > 1 && args[1][0] != '-') {
            type = args[1];
            flags_start = 2;

            if (type != "movegen" && type != "search" && type != "engine") {
                std::clog << "Error: Unknown bench type '" << type << "'\n";
                std::clog << "Available: movegen, search\n";
                return EXIT_FAILURE;
            }
        }

        for (size_t i = flags_start; i < args.size(); i++) {
            if (args[i] == "--verbose") {
                verbose = true;
            } else if (args[i] == "--fast") {
                fast = true;
            } else if (args[i] == "--phased") {
                phased = true;
            } else if (args[i] == "--threads" && i + 1 < args.size()) {
                threads = std::stoi(args[++i]);
            } else {
                std::clog << "Error: Unknown option '" << args[i] << "'\n";
                return EXIT_FAILURE;
            }
        }

        if (phased && (type == "search" || type == "engine")) {
            std::clog << "Error: --phased is only valid for movegen bench\n";
            return EXIT_FAILURE;
        }

        run_bench(type, verbose, fast, phased, threads);
    }

    else {
        std::clog << "Error: Unknown command '" << cmd << "'\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
