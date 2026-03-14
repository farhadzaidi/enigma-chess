#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "types.hpp"

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "./"
#endif

// --- FEN/EPD File Paths ---

inline std::filesystem::path DATA_DIR = std::filesystem::path(PROJECT_ROOT) / "positions";

inline const std::filesystem::path SINGLE_CHECK_EPD = DATA_DIR / "single_check.epd";
inline const std::filesystem::path DOUBLE_CHECK_EPD = DATA_DIR / "double_check.epd";
inline const std::filesystem::path NOT_IN_CHECK_FEN = DATA_DIR / "not_in_check.fen";
inline const std::filesystem::path MIXED_EPD = DATA_DIR / "mixed.epd";
inline const std::filesystem::path CPW_EPD = DATA_DIR / "cpw.epd";
inline const std::filesystem::path EN_PASSANT_EPD = DATA_DIR / "en_passant.epd";
inline const std::filesystem::path ENGINE_EPD = DATA_DIR / "engine.epd";

// --- Structs ---

struct PerftEpdResult {
    std::string fen;
    std::unordered_map<SearchDepth, uint64_t> depth_nodes;
};

struct EngineEpdResult {
    std::string fen;
    std::string best_move_san;
};

// --- File Utilities ---

void read_file(std::vector<std::string>& buffer, std::filesystem::path file_path, int max_lines = -1);

// Parses a line in the form [FEN]; D[DEPTH] [NODES]; D[DEPTH] [NODES]; ...
// e.g. 1Q3k2/8/8/p2p1p2/R4p2/5bP1/3B1bP1/5K2 b - - 0 1; D1 3; D2 117; D3 1994; D4 67254
PerftEpdResult parse_perft_epd_line(std::string line);

// Parses search benchmark EPD line in the form [FEN]; bm [MOVE]; ...
// e.g. rnbqkb1r/p3pppp/1p6/2ppP3/3N4/2P5/PPP1QPPP/R1B1KB1R w KQkq - ; bm e6; id BK.04
EngineEpdResult parse_engine_epd_line(std::string line);
