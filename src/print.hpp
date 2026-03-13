#pragma once

#include <iostream>
#include <string>
#include <syncstream>

inline void uci_print(const std::string& str) {
    std::osyncstream(std::cout) << str << '\n' << std::flush;
}
