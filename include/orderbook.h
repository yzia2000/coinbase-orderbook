#pragma once

#include "flat_orderbook.h"
#include "seqlock.h"

#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace cob {

struct PriceLevel {
    double price;
    double size;
};

// Orderbook maintains a full-depth map (writer-side only, no lock needed)
// and publishes top-N levels via a seqlock for lock-free reader access.
class Orderbook {
public:
    explicit Orderbook(std::string product_id);

    // Called from IO thread only — no synchronization needed on the maps
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
    std::map<double, double, std::greater<>> bids_; // descending by price
    std::map<double, double> asks_;                  // ascending by price
    uint64_t update_count_ = 0;

    Seqlock<FlatSnapshot> published_;
};

} // namespace cob
