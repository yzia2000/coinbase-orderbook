#include "orderbook.h"

namespace cob {

Orderbook::Orderbook(std::string product_id)
    : product_id_(std::move(product_id))
{
}

void Orderbook::apply_snapshot(const std::vector<PriceLevel>& bids,
                                const std::vector<PriceLevel>& asks)
{
    std::lock_guard lock(mutex_);
    bids_.clear();
    asks_.clear();

    for (const auto& [price, size] : bids) {
        bids_[price] = size;
    }
    for (const auto& [price, size] : asks) {
        asks_[price] = size;
    }

    last_update_ = std::chrono::steady_clock::now();
    update_count_ = 0;
}

void Orderbook::apply_update(const std::vector<PriceLevel>& bids,
                              const std::vector<PriceLevel>& asks)
{
    std::lock_guard lock(mutex_);

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

    last_update_ = std::chrono::steady_clock::now();
    ++update_count_;
}

Orderbook::Snapshot Orderbook::snapshot(std::size_t depth) const
{
    std::lock_guard lock(mutex_);
    Snapshot snap;
    snap.product_id = product_id_;
    snap.last_update = last_update_;
    snap.update_count = update_count_;

    std::size_t count = 0;
    for (const auto& [price, size] : bids_) {
        if (count++ >= depth) break;
        snap.bids.push_back({price, size});
    }

    count = 0;
    for (const auto& [price, size] : asks_) {
        if (count++ >= depth) break;
        snap.asks.push_back({price, size});
    }

    return snap;
}

} // namespace cob
