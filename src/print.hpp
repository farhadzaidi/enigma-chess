#pragma once

#include <string>

/** Thread-safe output to stdout, used for all UCI protocol responses */
void uci_print(const std::string& str);
