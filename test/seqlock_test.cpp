#include <gtest/gtest.h>
#include "seqlock.h"

#include <thread>
#include <atomic>
#include <cstdint>

struct TestData {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t c = 0;
};

static_assert(std::is_trivially_copyable_v<TestData>);

TEST(Seqlock, StoreAndLoad) {
    cob::Seqlock<TestData> sl;

    TestData d{1, 2, 3};
    sl.store(d);

    auto result = sl.load();
    EXPECT_EQ(result.a, 1);
    EXPECT_EQ(result.b, 2);
    EXPECT_EQ(result.c, 3);
}

TEST(Seqlock, MultipleStores) {
    cob::Seqlock<TestData> sl;

    sl.store({1, 2, 3});
    sl.store({4, 5, 6});
    sl.store({7, 8, 9});

    auto result = sl.load();
    EXPECT_EQ(result.a, 7);
    EXPECT_EQ(result.b, 8);
    EXPECT_EQ(result.c, 9);
}

TEST(Seqlock, DefaultInitializedToZero) {
    cob::Seqlock<TestData> sl;

    auto result = sl.load();
    EXPECT_EQ(result.a, 0);
    EXPECT_EQ(result.b, 0);
    EXPECT_EQ(result.c, 0);
}

// Concurrent test: writer continuously writes {n, n, n} for increasing n.
// Reader checks that all three fields are always consistent (same value).
// A torn read would produce mismatched fields.
TEST(Seqlock, ConcurrentConsistency) {
    cob::Seqlock<TestData> sl;
    std::atomic<bool> running{true};
    std::atomic<uint64_t> max_seen{0};

    std::thread writer([&]() {
        for (uint64_t n = 1; n <= 1'000'000; ++n) {
            sl.store({n, n, n});
        }
        running = false;
    });

    // Reader: verify consistency
    uint64_t reads = 0;
    while (running.load(std::memory_order_relaxed)) {
        auto d = sl.load();
        EXPECT_EQ(d.a, d.b) << "Torn read: a=" << d.a << " b=" << d.b;
        EXPECT_EQ(d.b, d.c) << "Torn read: b=" << d.b << " c=" << d.c;

        uint64_t val = d.a;
        uint64_t prev_max = max_seen.load(std::memory_order_relaxed);
        if (val > prev_max) {
            max_seen.store(val, std::memory_order_relaxed);
        }
        ++reads;
    }

    writer.join();

    // Verify we actually did meaningful work
    EXPECT_GT(reads, 0u);
    EXPECT_GT(max_seen.load(), 0u);
}

// Test with a larger struct to stress the memcpy path
struct LargeData {
    std::array<uint64_t, 64> values{};
};

static_assert(std::is_trivially_copyable_v<LargeData>);

TEST(Seqlock, ConcurrentConsistencyLargeStruct) {
    cob::Seqlock<LargeData> sl;
    std::atomic<bool> running{true};

    std::thread writer([&]() {
        for (uint64_t n = 1; n <= 500'000; ++n) {
            LargeData d;
            d.values.fill(n);
            sl.store(d);
        }
        running = false;
    });

    uint64_t reads = 0;
    while (running.load(std::memory_order_relaxed)) {
        auto d = sl.load();
        uint64_t expected = d.values[0];
        for (size_t i = 1; i < d.values.size(); ++i) {
            EXPECT_EQ(d.values[i], expected)
                << "Torn read at index " << i
                << ": expected " << expected << " got " << d.values[i];
        }
        ++reads;
    }

    writer.join();
    EXPECT_GT(reads, 0u);
}
