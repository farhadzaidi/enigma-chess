#pragma once

#include <string>

/** Thread-safe output to stdout, used for all UCI protocol responses */
void uci_print(const std::string& str);

/** Suppress all uci_print output when true. */
void set_uci_silent(bool silent);
