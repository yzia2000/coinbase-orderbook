#pragma once

#include <map>
#include <string>
#include <mutex>
#include <vector>
#include <chrono>

namespace cob {

struct PriceLevel {
    double price;
    double size;
};

class Orderbook {
public:
    explicit Orderbook(std::string product_id);

    void apply_snapshot(const std::vector<PriceLevel>& bids,
                        const std::vector<PriceLevel>& asks);

    void apply_update(const std::vector<PriceLevel>& bids,
                      const std::vector<PriceLevel>& asks);

    // Thread-safe snapshot for rendering
    struct Snapshot {
        std::string product_id;
        std::vector<PriceLevel> bids; // sorted best (highest) first
        std::vector<PriceLevel> asks; // sorted best (lowest) first
        std::chrono::steady_clock::time_point last_update;
        uint64_t update_count;
    };

    Snapshot snapshot(std::size_t depth = 20) const;

    const std::string& product_id() const { return product_id_; }

private:
    std::string product_id_;
    std::map<double, double, std::greater<>> bids_; // descending by price
    std::map<double, double> asks_;                  // ascending by price
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point last_update_;
    uint64_t update_count_ = 0;
};

} // namespace cob
