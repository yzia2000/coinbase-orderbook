#pragma once

#include "flat_orderbook.h"
#include "seqlock.h"

#include <string>
#include <vector>
#include <cstdint>

namespace cob {

struct PriceLevel {
    double price;
    double size;
};

// Fully flat orderbook — no std::map, no heap allocations on the hot path.
// Maintains top-N levels in sorted FlatBookSideBuilders and publishes
// a trivially-copyable snapshot via seqlock for lock-free reader access.
class Orderbook {
public:
    explicit Orderbook(std::string product_id);

    // Called from IO thread only — no synchronization needed
    void apply_snapshot(const std::vector<PriceLevel>& bids,
                        const std::vector<PriceLevel>& asks);

    void apply_update(const std::vector<PriceLevel>& bids,
                      const std::vector<PriceLevel>& asks);

    // Lock-free read from any thread
    FlatSnapshot load_snapshot() const { return published_.load(); }

    const std::string& product_id() const { return product_id_; }

private:
    void publish();

    std::string product_id_;
    FlatBookSideBuilder bids_; // descending (best bid first)
    FlatBookSideBuilder asks_; // ascending (best ask first)
    uint64_t update_count_ = 0;

    Seqlock<FlatSnapshot> published_;
};

} // namespace cob
