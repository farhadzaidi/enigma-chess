#include "print.hpp"

#include <atomic>
#include <iostream>
#include <syncstream>

namespace {
std::atomic<bool> g_uci_silent{false};
}

void uci_print(const std::string& str) {
    if (g_uci_silent) return;
    std::osyncstream(std::cout) << str << '\n' << std::flush;
}

void set_uci_silent(bool silent) {
    g_uci_silent = silent;
}
