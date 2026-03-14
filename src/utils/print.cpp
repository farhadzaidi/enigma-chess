#include "utils/print.hpp"

#include <iostream>
#include <syncstream>

void uci_print(const std::string& str) {
    std::osyncstream(std::cout) << str << '\n' << std::flush;
}
