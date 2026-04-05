#include <gtest/gtest.h>
#include "flat_orderbook.h"
#include <functional>

using BidBuilder = cob::FlatBookSideBuilder<std::greater<double>>;
using AskBuilder = cob::FlatBookSideBuilder<std::less<double>>;

// ---------------------------------------------------------------------------
// BidBuilder (descending: best/highest first)
// ---------------------------------------------------------------------------

TEST(BidBuilder, InsertMaintainsDescendingOrder) {
    BidBuilder bids;
    bids.apply(100.0, 1.0);
    bids.apply(102.0, 2.0);
    bids.apply(101.0, 3.0);

    auto side = bids.to_flat();
    ASSERT_EQ(side.count, 3);
    EXPECT_DOUBLE_EQ(side.levels[0].price, 102.0);
    EXPECT_DOUBLE_EQ(side.levels[1].price, 101.0);
    EXPECT_DOUBLE_EQ(side.levels[2].price, 100.0);
}

TEST(BidBuilder, UpdateExistingLevel) {
    BidBuilder bids;
    bids.apply(100.0, 1.0);
    bids.apply(101.0, 2.0);

    bids.apply(100.0, 5.0);

    auto side = bids.to_flat();
    ASSERT_EQ(side.count, 2);
    EXPECT_DOUBLE_EQ(side.levels[1].price, 100.0);
    EXPECT_DOUBLE_EQ(side.levels[1].size, 5.0);
}

TEST(BidBuilder, RemoveByZeroSize) {
    BidBuilder bids;
    bids.apply(100.0, 1.0);
    bids.apply(101.0, 2.0);
    bids.apply(102.0, 3.0);

    bids.apply(101.0, 0.0);

    auto side = bids.to_flat();
    ASSERT_EQ(side.count, 2);
    EXPECT_DOUBLE_EQ(side.levels[0].price, 102.0);
    EXPECT_DOUBLE_EQ(side.levels[1].price, 100.0);
}

TEST(BidBuilder, RemoveNonExistentIsNoop) {
    BidBuilder bids;
    bids.apply(100.0, 1.0);

    bids.apply(999.0, 0.0);

    auto side = bids.to_flat();
    ASSERT_EQ(side.count, 1);
}

TEST(BidBuilder, EvictWorstWhenFull) {
    BidBuilder bids;
    // Fill to MAX_DEPTH
    for (uint32_t i = 0; i < cob::MAX_DEPTH; ++i) {
        bids.apply(100.0 + i, 1.0);
    }
    auto side = bids.to_flat();
    ASSERT_EQ(side.count, cob::MAX_DEPTH);

    double worst_price = side.levels[cob::MAX_DEPTH - 1].price;

    // Insert a price better than the worst — should evict worst
    bids.apply(worst_price + 0.5, 2.0);

    side = bids.to_flat();
    ASSERT_EQ(side.count, cob::MAX_DEPTH);
    // Worst should no longer be present
    for (uint32_t i = 0; i < side.count; ++i) {
        EXPECT_NE(side.levels[i].price, worst_price);
    }
    // New level should be present
    bool found = false;
    for (uint32_t i = 0; i < side.count; ++i) {
        if (side.levels[i].price == worst_price + 0.5) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(BidBuilder, DiscardWorseThanFullBook) {
    BidBuilder bids;
    for (uint32_t i = 0; i < cob::MAX_DEPTH; ++i) {
        bids.apply(100.0 + i, 1.0);
    }

    double worst_price = bids.to_flat().levels[cob::MAX_DEPTH - 1].price;

    // Insert price worse than the worst — should be discarded
    bids.apply(worst_price - 1.0, 2.0);

    auto side = bids.to_flat();
    ASSERT_EQ(side.count, cob::MAX_DEPTH);
    for (uint32_t i = 0; i < side.count; ++i) {
        EXPECT_NE(side.levels[i].price, worst_price - 1.0);
    }
}

TEST(BidBuilder, RemoveFromFullBookReducesCount) {
    BidBuilder bids;
    for (uint32_t i = 0; i < cob::MAX_DEPTH; ++i) {
        bids.apply(100.0 + i, 1.0);
    }
    ASSERT_EQ(bids.count(), cob::MAX_DEPTH);

    bids.apply(105.0, 0.0);
    EXPECT_EQ(bids.count(), cob::MAX_DEPTH - 1);
}

TEST(BidBuilder, ClearResetsState) {
    BidBuilder bids;
    bids.apply(100.0, 1.0);
    bids.apply(101.0, 2.0);

    bids.clear();

    EXPECT_EQ(bids.count(), 0);
    auto side = bids.to_flat();
    EXPECT_EQ(side.count, 0);
}

TEST(BidBuilder, SingleElement) {
    BidBuilder bids;
    bids.apply(50.0, 3.0);

    auto side = bids.to_flat();
    ASSERT_EQ(side.count, 1);
    EXPECT_DOUBLE_EQ(side.levels[0].price, 50.0);
    EXPECT_DOUBLE_EQ(side.levels[0].size, 3.0);
}

TEST(BidBuilder, RemoveOnlyElement) {
    BidBuilder bids;
    bids.apply(50.0, 3.0);
    bids.apply(50.0, 0.0);

    EXPECT_EQ(bids.count(), 0);
}

// ---------------------------------------------------------------------------
// AskBuilder (ascending: best/lowest first)
// ---------------------------------------------------------------------------

TEST(AskBuilder, InsertMaintainsAscendingOrder) {
    AskBuilder asks;
    asks.apply(102.0, 1.0);
    asks.apply(100.0, 2.0);
    asks.apply(101.0, 3.0);

    auto side = asks.to_flat();
    ASSERT_EQ(side.count, 3);
    EXPECT_DOUBLE_EQ(side.levels[0].price, 100.0);
    EXPECT_DOUBLE_EQ(side.levels[1].price, 101.0);
    EXPECT_DOUBLE_EQ(side.levels[2].price, 102.0);
}

TEST(AskBuilder, EvictWorstWhenFull) {
    AskBuilder asks;
    for (uint32_t i = 0; i < cob::MAX_DEPTH; ++i) {
        asks.apply(100.0 + i, 1.0);
    }

    double worst_price = asks.to_flat().levels[cob::MAX_DEPTH - 1].price;

    // Insert better (lower) than worst — should evict worst
    asks.apply(worst_price - 0.5, 2.0);

    auto side = asks.to_flat();
    ASSERT_EQ(side.count, cob::MAX_DEPTH);
    for (uint32_t i = 0; i < side.count; ++i) {
        EXPECT_NE(side.levels[i].price, worst_price);
    }
}

TEST(AskBuilder, DiscardWorseThanFullBook) {
    AskBuilder asks;
    for (uint32_t i = 0; i < cob::MAX_DEPTH; ++i) {
        asks.apply(100.0 + i, 1.0);
    }

    double worst_price = asks.to_flat().levels[cob::MAX_DEPTH - 1].price;

    // Insert worse (higher) than worst — discard
    asks.apply(worst_price + 1.0, 2.0);

    auto side = asks.to_flat();
    ASSERT_EQ(side.count, cob::MAX_DEPTH);
    for (uint32_t i = 0; i < side.count; ++i) {
        EXPECT_NE(side.levels[i].price, worst_price + 1.0);
    }
}

TEST(AskBuilder, UpdateAndRemove) {
    AskBuilder asks;
    asks.apply(100.0, 1.0);
    asks.apply(101.0, 2.0);
    asks.apply(102.0, 3.0);

    asks.apply(101.0, 9.0); // update
    asks.apply(100.0, 0.0); // remove

    auto side = asks.to_flat();
    ASSERT_EQ(side.count, 2);
    EXPECT_DOUBLE_EQ(side.levels[0].price, 101.0);
    EXPECT_DOUBLE_EQ(side.levels[0].size, 9.0);
    EXPECT_DOUBLE_EQ(side.levels[1].price, 102.0);
}

// ---------------------------------------------------------------------------
// FlatSnapshot trivial copyability
// ---------------------------------------------------------------------------

TEST(FlatSnapshot, IsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<cob::FlatSnapshot>);
    EXPECT_TRUE(std::is_trivially_copyable_v<cob::FlatBookSide>);
    EXPECT_TRUE(std::is_trivially_copyable_v<cob::FlatPriceLevel>);
}
