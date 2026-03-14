#pragma once

#include <string>
#include <string_view>

#include "board.hpp"
#include "move.hpp"

/** Convert a UCI square string (e.g. "e4") to a square index */
Square uci_to_index(std::string_view square);
/** Convert a square index to a UCI square string (e.g. "e4") */
std::string index_to_uci(Square square);
/** Build a Move from a UCI move string (e.g. "e2e4", "e7e8q") using board context */
Move encode_move_from_uci(const Board& b, std::string_view uci_move);
/** Convert a Move to its UCI string representation */
std::string decode_move_to_uci(Move move);
/** Parse a SAN move string (e.g. "Nf3", "O-O") into a Move by matching against legal moves */
Move parse_move_from_san(Board& b, std::string_view san);
