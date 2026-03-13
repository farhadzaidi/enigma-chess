#pragma once

#include <random>
#include <cstdint>
#include <limits>

namespace {

// constants
constexpr uint64_t PRNG_SEED = 0xC0DEC0FFEE1234ULL;

} // namespace


inline std::mt19937_64 prng(PRNG_SEED);
inline std::mt19937_64 rng(std::random_device{}());

inline std::uniform_int_distribution<uint64_t> u64_dist(
    0, std::numeric_limits<uint64_t>::max()
);

inline uint64_t prandom_u64() {
    return u64_dist(prng);
}

// Returns a random sparse uint64_t
inline uint64_t prandom_magic() {
    return u64_dist(prng) & u64_dist(prng) & u64_dist(prng);
}
