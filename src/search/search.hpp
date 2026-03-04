#pragma once

#include "search/iterative_deepening.hpp"

inline Move search_time(Board& b, int soft_time, int hard_time) {
    return search<SearchMode::Time>(b, {.soft_time = soft_time, .hard_time = hard_time});
}

inline Move search_nodes(Board& b, uint64_t nodes) {
    return search<SearchMode::Nodes>(b, {.nodes = nodes});
}

inline Move search_depth(Board& b, SearchDepth depth) {
    return search<SearchMode::Depth>(b, {.depth = depth});
}

inline Move search_infinite(Board& b) {
    return search<SearchMode::Infinite>(b, {});
}
