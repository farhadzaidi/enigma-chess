#include "parse.hpp"

void read_file(std::vector<std::string>& buffer, std::filesystem::path file_path, int max_lines) {
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

PerftEpdResult parse_perft_epd_line(std::string line) {
    auto trim = [](const std::string& s) -> std::string {
        const size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        const size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    };

    auto pos = line.find(";");
    if (pos == std::string::npos) {
        return {trim(line), {}};
    }

    std::string fen = trim(line.substr(0, pos));
    std::string rest = line.substr(pos + 1);

    std::unordered_map<SearchDepth, uint64_t> depth_nodes;
    std::istringstream iss(rest);
    std::string token;

    while (std::getline(iss, token, ';')) {
        std::istringstream pair_stream(trim(token));
        std::string depth_str;
        uint64_t nodes;
        if (!(pair_stream >> depth_str >> nodes)) continue;
        if (depth_str.size() < 2 || depth_str[0] != 'D') continue;

        SearchDepth depth = std::stoi(depth_str.substr(1));
        depth_nodes[depth] = nodes;
    }

    return {fen, depth_nodes};
}

EngineEpdResult parse_engine_epd_line(std::string line) {
    auto bm_pos = line.find("; bm ");
    if (bm_pos == std::string::npos) {
        return {"", ""};
    }

    std::string fen = line.substr(0, bm_pos);

    size_t san_start = bm_pos + 5;
    size_t san_end = line.find(';', san_start);

    std::string san_part;
    if (san_end != std::string::npos) {
        san_part = line.substr(san_start, san_end - san_start);
    } else {
        san_part = line.substr(san_start);
    }

    std::istringstream iss(san_part);
    std::string san;
    iss >> san;

    return {fen, san};
}
