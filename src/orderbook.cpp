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
        bids_.apply(price, size);
    }
    for (const auto& [price, size] : asks) {
        asks_.apply(price, size);
    }

    update_count_ = 0;
    publish();
}

void Orderbook::apply_update(const std::vector<PriceLevel>& bids,
                              const std::vector<PriceLevel>& asks)
{
    for (const auto& [price, size] : bids) {
        bids_.apply(price, size);
    }
    for (const auto& [price, size] : asks) {
        asks_.apply(price, size);
    }

    ++update_count_;
    publish();
}

void Orderbook::publish()
{
    FlatSnapshot snap{};
    snap.update_count = update_count_;
    snap.bids = bids_.to_flat();
    snap.asks = asks_.to_flat();
    published_.store(snap);
}

} // namespace cob
