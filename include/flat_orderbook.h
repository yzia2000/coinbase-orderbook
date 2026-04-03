#pragma once

#include "seqlock.h"
#include <array>
#include <cstdint>
#include <algorithm>

namespace cob {

inline constexpr std::size_t MAX_DEPTH = 20;

struct alignas(16) FlatPriceLevel {
    double price = 0.0;
    double size = 0.0;
};

// Trivially copyable book side — suitable for seqlock
struct FlatBookSide {
    std::array<FlatPriceLevel, MAX_DEPTH> levels{};
    uint32_t count = 0;
    uint32_t padding_ = 0; // keep alignment clean
};

static_assert(std::is_trivially_copyable_v<FlatBookSide>);

// Published snapshot — both sides, trivially copyable
struct FlatSnapshot {
    FlatBookSide bids; // sorted descending by price (best bid first)
    FlatBookSide asks; // sorted ascending by price (best ask first)
    uint64_t update_count = 0;
};

static_assert(std::is_trivially_copyable_v<FlatSnapshot>);

// Flat array-based book side with insert/update/remove via linear scan + shift
class FlatBookSideBuilder {
public:
    // Insert or update a price level. If size == 0, removes the level.
    // For bids: descending order (highest first)
    // For asks: ascending order (lowest first)
    template <bool Descending>
    void apply(double price, double size) {
        // Linear scan to find existing level
        for (uint32_t i = 0; i < count_; ++i) {
            if (levels_[i].price == price) {
                if (size == 0.0) {
                    remove_at(i);
                } else {
                    levels_[i].size = size;
                }
                return;
            }
        }

        // Not found — insert if size > 0
        if (size == 0.0 || count_ >= MAX_DEPTH) {
            // If full, check if this level is better than worst
            if (size > 0.0 && count_ >= MAX_DEPTH) {
                bool is_better;
                if constexpr (Descending) {
                    is_better = price > levels_[count_ - 1].price;
                } else {
                    is_better = price < levels_[count_ - 1].price;
                }
                if (is_better) {
                    // Evict worst, then insert
                    --count_;
                } else {
                    return; // outside our depth
                }
            } else {
                return;
            }
        }

        insert_sorted<Descending>(price, size);
    }

    void clear() { count_ = 0; }

    FlatBookSide to_flat() const {
        FlatBookSide side{};
        side.count = count_;
        for (uint32_t i = 0; i < count_; ++i) {
            side.levels[i] = levels_[i];
        }
        return side;
    }

    void load_from(const FlatBookSide& side) {
        count_ = side.count;
        for (uint32_t i = 0; i < count_; ++i) {
            levels_[i] = side.levels[i];
        }
    }

    uint32_t count() const { return count_; }

private:
    void remove_at(uint32_t idx) {
        for (uint32_t i = idx; i + 1 < count_; ++i) {
            levels_[i] = levels_[i + 1];
        }
        --count_;
    }

    template <bool Descending>
    void insert_sorted(double price, double size) {
        // Find insertion point
        uint32_t pos = 0;
        for (; pos < count_; ++pos) {
            bool should_insert;
            if constexpr (Descending) {
                should_insert = price > levels_[pos].price;
            } else {
                should_insert = price < levels_[pos].price;
            }
            if (should_insert) break;
        }

        // Shift right
        for (uint32_t i = count_; i > pos; --i) {
            levels_[i] = levels_[i - 1];
        }

        levels_[pos] = {price, size};
        ++count_;
    }

    std::array<FlatPriceLevel, MAX_DEPTH> levels_{};
    uint32_t count_ = 0;
};

} // namespace cob
