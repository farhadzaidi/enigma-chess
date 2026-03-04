#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "core/types.hpp"
#include "core/move.hpp"

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

inline void read_file(std::vector<std::string>& buffer, std::filesystem::path file_path, int max_lines = -1) {
    std::ifstream file(file_path);
    if (!file) {
        std::cerr << "Failed to open: " << file_path << "\n";
    }

    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        count++;
        buffer.push_back(line);
        if (max_lines != -1 && count >= max_lines) {
            break;
        }
    }

    file.close();
}

// Parses a line in the form [FEN]; D[DEPTH] [NODES]; D[DEPTH] [NODES]; ...
// e.g. 1Q3k2/8/8/p2p1p2/R4p2/5bP1/3B1bP1/5K2 b - - 0 1; D1 3; D2 117; D3 1994; D4 67254
inline PerftEpdResult parse_perft_epd_line(std::string line) {
    auto pos = line.find(";");
    std::string fen = line.substr(0, pos);
    std::string rest = line.substr(pos + 1);

    std::istringstream iss(rest);
    std::unordered_map<SearchDepth, uint64_t> depth_nodes;

    std::string depth_str;
    uint64_t nodes;

    // Parse all D[depth] [nodes] pairs
    while (iss >> depth_str >> nodes) {
        SearchDepth depth = std::stoi(depth_str.substr(1));
        depth_nodes[depth] = nodes;
    }

    return {fen, depth_nodes};
}

// Parses engine benchmark EPD line in the form [FEN]; bm [MOVE]; ...
// e.g. rnbqkb1r/p3pppp/1p6/2ppP3/3N4/2P5/PPP1QPPP/R1B1KB1R w KQkq - ; bm e6; id BK.04
inline EngineEpdResult parse_engine_epd_line(std::string line) {
    // Find FEN (everything before "; bm ")
    auto bm_pos = line.find("; bm ");
    if (bm_pos == std::string::npos) {
        return {"", ""};  // Invalid format
    }

    std::string fen = line.substr(0, bm_pos);

    // Find SAN move (between "bm " and next ";")
    size_t san_start = bm_pos + 5;  // length of "; bm "
    size_t san_end = line.find(';', san_start);

    std::string san_part;
    if (san_end != std::string::npos) {
        san_part = line.substr(san_start, san_end - san_start);
    } else {
        san_part = line.substr(san_start);
    }

    // Extract first move (in case there are multiple best moves separated by space)
    std::istringstream iss(san_part);
    std::string san;
    iss >> san;

    return {fen, san};
}
