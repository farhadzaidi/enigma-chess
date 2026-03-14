#include "transposition_table.hpp"

// --- TT Entry ---

/** Pack move/depth/score/node into data_ and XOR with hash for verification */
TTEntry::TTEntry(ZobristHash hash, Move best_move, SearchDepth depth, PositionScore score, TTNode node) {
    data_ = (
        static_cast<uint64_t>(best_move.move)                     |
        static_cast<uint64_t>(depth)                        << 16 |
        static_cast<uint64_t>(static_cast<uint16_t>(score)) << 24 |
        static_cast<uint64_t>(node)                         << 40
    );

    // XOR hash with data so we can verify integrity on lookup
    hash_ = hash ^ data_;
}

Move TTEntry::move() const {
    Move best_move;
    best_move.move = data_ & 0xFFFF;
    return best_move;
}

SearchDepth TTEntry::depth() const {
    return (data_ >> 16) & 0xFF;
}

PositionScore TTEntry::score() const {
    return static_cast<int16_t>((data_ >> 24) & 0xFFFF);
}

TTNode TTEntry::node() const {
    return static_cast<TTNode>((data_ >> 40) & 0xFF);
}

uint16_t TTEntry::age() const {
    return (data_ >> 48) & 0xFFFF;
}

ZobristHash TTEntry::hash() const {
    return hash_ ^ data_;
}

bool TTEntry::is_empty() const {
    return data_ == EMPTY_DATA;
}

void TTEntry::set_age(uint16_t age) {
    hash_ = hash();
    data_ &= ~(uint64_t{0xFFFF} << 48);
    data_ |= static_cast<uint64_t>(age) << 48;
    hash_ = hash();
}

// --- Transposition Table ---

TranspositionTable::TranspositionTable() {
    resize(DEFAULT_HASH_MB);
}

void TranspositionTable::resize(size_t mb) {
    mb = std::clamp(mb, MIN_HASH_MB, MAX_HASH_MB);
    uint64_t target_buckets = (mb * 1024ULL * 1024ULL) / sizeof(TTBucket);

    // Round down to power of two so index masking works
    num_buckets = 1;
    while (num_buckets * 2 <= target_buckets) {
        num_buckets *= 2;
    }

    table.assign(num_buckets, TTBucket{});
    generation = 0;
}

void TranspositionTable::clear() {
    std::fill(table.begin(), table.end(), TTBucket{});
    generation = 0;
}

TTEntry* TranspositionTable::get_entry(ZobristHash hash) {
    TTBucket& bucket = table[get_index(hash)];

    for (auto& entry : bucket) {
        if (!entry.is_empty() && entry.hash() == hash) {
            return &entry;
        }
    }

    return nullptr;
}

void TranspositionTable::add_entry(TTEntry entry) {
    TTBucket& bucket = table[get_index(entry.hash())];

    entry.set_age(generation);

    // Prefer empty slots or same-position updates; otherwise evict least valuable
    TTEntry* least_valuable = &bucket[0];
    int least_value = get_entry_value(*least_valuable);
    for (auto& bucket_entry : bucket) {
        if (bucket_entry.is_empty() || bucket_entry.hash() == entry.hash()) {
            bucket_entry = entry;
            return;
        }

        int value = get_entry_value(bucket_entry);
        if (value < least_value) {
            least_valuable = &bucket_entry;
            least_value = value;
        }
    }

    *least_valuable = entry;
}

void TranspositionTable::increment_generation() {
    generation++;
}

uint64_t TranspositionTable::get_index(ZobristHash hash) const {
    return hash & (num_buckets - 1);
}

int TranspositionTable::get_entry_value(const TTEntry& entry) const {
    return entry.depth() - AGE_PENALTY_WEIGHT * (generation - entry.age());
}
