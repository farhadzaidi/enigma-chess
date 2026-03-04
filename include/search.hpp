#pragma once

#include "board.hpp"
#include "search_state.hpp"

template <SearchMode SM>
Move search(Board& b, const SearchLimits& limits);

inline Move search_time(Board& b, int soft_time, int hard_time) {
    return search<TIME>(b, {.soft_time = soft_time, .hard_time = hard_time});
}

inline Move search_nodes(Board& b, uint64_t nodes) {
    return search<NODES>(b, {.nodes = nodes});
}

inline Move search_depth(Board& b, SearchDepth depth) {
    return search<DEPTH>(b, {.depth = depth});
}

inline Move search_infinite(Board& b) {
    return search<INFINITE>(b, {});
}
