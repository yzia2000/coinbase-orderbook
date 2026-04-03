#include <benchmark/benchmark.h>

#include "orderbook.h"
#include "flat_orderbook.h"
#include "seqlock.h"

#include <random>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>

// ---------------------------------------------------------------------------
// Helpers: generate realistic price updates
// ---------------------------------------------------------------------------

struct Update {
    double price;
    double size;
    bool is_bid;
};

static std::vector<Update> generate_updates(std::size_t count, double mid_price = 50000.0) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_offset(-50.0, 50.0);
    std::uniform_real_distribution<double> size_dist(0.0, 5.0);
    std::bernoulli_distribution side_dist(0.5);
    std::bernoulli_distribution remove_dist(0.15); // 15% of updates are removals

    std::vector<Update> updates;
    updates.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        double offset = price_offset(rng);
        double price = mid_price + offset;
        // Round to 2 decimal places
        price = std::round(price * 100.0) / 100.0;
        double size = remove_dist(rng) ? 0.0 : size_dist(rng);
        bool is_bid = side_dist(rng);
        updates.push_back({price, size, is_bid});
    }
    return updates;
}

// Pre-generate updates to avoid measuring RNG in benchmarks
static const auto g_updates = generate_updates(100'000);

// ---------------------------------------------------------------------------
// Benchmark 1: Write throughput — map-based Orderbook
// ---------------------------------------------------------------------------

static void BM_MapOrderbook_Write(benchmark::State& state) {
    cob::Orderbook book("BTC-USD");

    std::size_t idx = 0;
    for (auto _ : state) {
        const auto& u = g_updates[idx % g_updates.size()];
        std::vector<cob::PriceLevel> bids, asks;
        if (u.is_bid) {
            bids.push_back({u.price, u.size});
        } else {
            asks.push_back({u.price, u.size});
        }
        book.apply_update(bids, asks);
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MapOrderbook_Write);

// ---------------------------------------------------------------------------
// Benchmark 2: Write throughput — flat array-based
// ---------------------------------------------------------------------------

static void BM_FlatOrderbook_Write(benchmark::State& state) {
    cob::FlatBookSideBuilder bids, asks;

    std::size_t idx = 0;
    for (auto _ : state) {
        const auto& u = g_updates[idx % g_updates.size()];
        if (u.is_bid) {
            bids.apply<true>(u.price, u.size);
        } else {
            asks.apply<false>(u.price, u.size);
        }
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FlatOrderbook_Write);

// ---------------------------------------------------------------------------
// Benchmark 3: Read (snapshot) throughput — map + mutex
// ---------------------------------------------------------------------------

static void BM_MapOrderbook_Read(benchmark::State& state) {
    cob::Orderbook book("BTC-USD");

    // Populate with some data
    for (std::size_t i = 0; i < 1000; ++i) {
        const auto& u = g_updates[i];
        std::vector<cob::PriceLevel> bids, asks;
        if (u.is_bid) bids.push_back({u.price, u.size});
        else asks.push_back({u.price, u.size});
        book.apply_update(bids, asks);
    }

    for (auto _ : state) {
        auto snap = book.snapshot(20);
        benchmark::DoNotOptimize(snap);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MapOrderbook_Read);

// ---------------------------------------------------------------------------
// Benchmark 4: Read throughput — flat array + seqlock
// ---------------------------------------------------------------------------

static void BM_FlatOrderbook_Read(benchmark::State& state) {
    cob::Seqlock<cob::FlatSnapshot> seqlock;

    // Populate
    cob::FlatBookSideBuilder bids_builder, asks_builder;
    for (std::size_t i = 0; i < 1000; ++i) {
        const auto& u = g_updates[i];
        if (u.is_bid) bids_builder.apply<true>(u.price, u.size);
        else asks_builder.apply<false>(u.price, u.size);
    }
    cob::FlatSnapshot snap;
    snap.bids = bids_builder.to_flat();
    snap.asks = asks_builder.to_flat();
    snap.update_count = 1000;
    seqlock.store(snap);

    for (auto _ : state) {
        auto result = seqlock.load();
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FlatOrderbook_Read);

// ---------------------------------------------------------------------------
// Benchmark 5: Write + publish throughput — map + mutex snapshot copy
// ---------------------------------------------------------------------------

static void BM_MapOrderbook_WriteAndPublish(benchmark::State& state) {
    cob::Orderbook book("BTC-USD");

    std::size_t idx = 0;
    for (auto _ : state) {
        const auto& u = g_updates[idx % g_updates.size()];
        std::vector<cob::PriceLevel> bids, asks;
        if (u.is_bid) bids.push_back({u.price, u.size});
        else asks.push_back({u.price, u.size});
        book.apply_update(bids, asks);

        // Simulate what a reader would do: take a snapshot under lock
        auto snap = book.snapshot(20);
        benchmark::DoNotOptimize(snap);
        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MapOrderbook_WriteAndPublish);

// ---------------------------------------------------------------------------
// Benchmark 6: Write + publish throughput — flat array + seqlock
// ---------------------------------------------------------------------------

static void BM_FlatOrderbook_WriteAndPublish(benchmark::State& state) {
    cob::Seqlock<cob::FlatSnapshot> seqlock;
    cob::FlatBookSideBuilder bids_builder, asks_builder;

    std::size_t idx = 0;
    for (auto _ : state) {
        const auto& u = g_updates[idx % g_updates.size()];
        if (u.is_bid) bids_builder.apply<true>(u.price, u.size);
        else asks_builder.apply<false>(u.price, u.size);

        // Publish to seqlock
        cob::FlatSnapshot snap;
        snap.bids = bids_builder.to_flat();
        snap.asks = asks_builder.to_flat();
        snap.update_count = idx;
        seqlock.store(snap);

        ++idx;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FlatOrderbook_WriteAndPublish);

// ---------------------------------------------------------------------------
// Benchmark 7: Concurrent read+write — map + mutex
// ---------------------------------------------------------------------------

static void BM_MapOrderbook_Concurrent(benchmark::State& state) {
    cob::Orderbook book("BTC-USD");
    std::atomic<bool> running{true};

    // Writer thread: continuous updates
    std::thread writer([&]() {
        std::size_t idx = 0;
        while (running.load(std::memory_order_relaxed)) {
            const auto& u = g_updates[idx % g_updates.size()];
            std::vector<cob::PriceLevel> bids, asks;
            if (u.is_bid) bids.push_back({u.price, u.size});
            else asks.push_back({u.price, u.size});
            book.apply_update(bids, asks);
            ++idx;
        }
    });

    // Reader (benchmark thread): measure snapshot read rate
    for (auto _ : state) {
        auto snap = book.snapshot(20);
        benchmark::DoNotOptimize(snap);
    }
    state.SetItemsProcessed(state.iterations());

    running = false;
    writer.join();
}
BENCHMARK(BM_MapOrderbook_Concurrent);

// ---------------------------------------------------------------------------
// Benchmark 8: Concurrent read+write — flat array + seqlock
// ---------------------------------------------------------------------------

static void BM_FlatOrderbook_Concurrent(benchmark::State& state) {
    cob::Seqlock<cob::FlatSnapshot> seqlock;
    std::atomic<bool> running{true};

    // Writer thread: continuous updates + publish
    std::thread writer([&]() {
        cob::FlatBookSideBuilder bids_builder, asks_builder;
        std::size_t idx = 0;
        while (running.load(std::memory_order_relaxed)) {
            const auto& u = g_updates[idx % g_updates.size()];
            if (u.is_bid) bids_builder.apply<true>(u.price, u.size);
            else asks_builder.apply<false>(u.price, u.size);

            cob::FlatSnapshot snap;
            snap.bids = bids_builder.to_flat();
            snap.asks = asks_builder.to_flat();
            snap.update_count = idx;
            seqlock.store(snap);
            ++idx;
        }
    });

    // Reader (benchmark thread)
    for (auto _ : state) {
        auto snap = seqlock.load();
        benchmark::DoNotOptimize(snap);
    }
    state.SetItemsProcessed(state.iterations());

    running = false;
    writer.join();
}
BENCHMARK(BM_FlatOrderbook_Concurrent);
