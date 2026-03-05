#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>

#include "test.hpp"
#include "bench.hpp"

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }

    if (args.empty()) {
        std::clog << "Usage:\n";
        std::clog << "  enigma-debug test [group/name ...]\n";
        std::clog << "  enigma-debug bench [--fast] [--verbose] [--phased] [--movegen] [--engine]\n";
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
        BenchFlags flags = {false, false, false, false, false};

        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == "--verbose") {
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

    else {
        std::clog << "Error: Unknown command '" << cmd << "'\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
