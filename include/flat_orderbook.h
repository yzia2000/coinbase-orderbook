#pragma once

#include "seqlock.h"
#include <array>
#include <cstdint>
#include <functional>

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

// Flat array-based book side with insert/update/remove.
// Comp(a, b) returns true if a should be placed before b.
// e.g. std::greater<>{} for bids (descending), std::less<>{} for asks (ascending).
template <typename Comp = std::less<double>>
class FlatBookSideBuilder {
public:
    // Single-scan apply: find insertion point, handle update/remove/insert in one pass.
    void apply(double price, double size) {
        // Single scan: find where this price lives or should live.
        // Since levels are sorted by Comp, we scan until we find
        // a level that is not "before" price.
        uint32_t pos = 0;
        for (; pos < count_; ++pos) {
            if (levels_[pos].price == price) {
                // Found existing level — update or remove
                if (size == 0.0) {
                    remove_at(pos);
                } else {
                    levels_[pos].size = size;
                }
                return;
            }
            if (!comp_(levels_[pos].price, price)) {
                // levels_[pos] is not before price, so price belongs at pos
                break;
            }
        }

        // Not found — insert at pos if size > 0
        if (size == 0.0) return;

        if (count_ < MAX_DEPTH) {
            // Room available — shift right from pos and insert
            insert_at(pos, price, size);
        } else if (pos < MAX_DEPTH) {
            // Full but price belongs within range — evict last, shift right, insert
            count_ = MAX_DEPTH - 1;
            insert_at(pos, price, size);
        }
        // else: pos == MAX_DEPTH means price is worse than all current levels, discard
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

    uint32_t count() const { return count_; }

private:
    void remove_at(uint32_t idx) {
        for (uint32_t i = idx; i + 1 < count_; ++i) {
            levels_[i] = levels_[i + 1];
        }
        --count_;
    }

    void insert_at(uint32_t pos, double price, double size) {
        for (uint32_t i = count_; i > pos; --i) {
            levels_[i] = levels_[i - 1];
        }
        levels_[pos] = {price, size};
        ++count_;
    }

    [[no_unique_address]] Comp comp_{};
    std::array<FlatPriceLevel, MAX_DEPTH> levels_{};
    uint32_t count_ = 0;
};

} // namespace cob
