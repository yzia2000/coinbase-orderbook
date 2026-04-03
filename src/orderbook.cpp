#include "orderbook.h"

namespace cob {

Orderbook::Orderbook(std::string product_id)
    : product_id_(std::move(product_id))
{
}

void Orderbook::apply_snapshot(const std::vector<PriceLevel>& bids,
                                const std::vector<PriceLevel>& asks)
{
    bids_.clear();
    asks_.clear();

    for (const auto& [price, size] : bids) {
        bids_[price] = size;
    }
    for (const auto& [price, size] : asks) {
        asks_[price] = size;
    }

    update_count_ = 0;
    publish();
}

void Orderbook::apply_update(const std::vector<PriceLevel>& bids,
                              const std::vector<PriceLevel>& asks)
{
    for (const auto& [price, size] : bids) {
        if (size == 0.0) {
            bids_.erase(price);
        } else {
            bids_[price] = size;
        }
    }

    for (const auto& [price, size] : asks) {
        if (size == 0.0) {
            asks_.erase(price);
        } else {
            asks_[price] = size;
        }
    }

    ++update_count_;
    publish();
}

void Orderbook::publish()
{
    FlatSnapshot snap{};
    snap.update_count = update_count_;

    // Extract top-N bids (map is already sorted descending)
    uint32_t count = 0;
    for (const auto& [price, size] : bids_) {
        if (count >= MAX_DEPTH) break;
        snap.bids.levels[count] = {price, size};
        ++count;
    }
    snap.bids.count = count;

    // Extract top-N asks (map is already sorted ascending)
    count = 0;
    for (const auto& [price, size] : asks_) {
        if (count >= MAX_DEPTH) break;
        snap.asks.levels[count] = {price, size};
        ++count;
    }
    snap.asks.count = count;

    published_.store(snap);
}

} // namespace cob
