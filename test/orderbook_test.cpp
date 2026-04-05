#include <gtest/gtest.h>
#include "orderbook.h"

class OrderbookTest : public ::testing::Test {
protected:
    cob::Orderbook book{"BTC-USD"};
};

TEST_F(OrderbookTest, SnapshotPopulatesBothSides) {
    book.apply_snapshot(
        {{100.0, 1.0}, {99.0, 2.0}, {98.0, 3.0}},
        {{101.0, 1.5}, {102.0, 2.5}}
    );

    auto snap = book.load_snapshot();

    ASSERT_EQ(snap.bids.count, 3);
    EXPECT_DOUBLE_EQ(snap.bids.levels[0].price, 100.0); // best bid
    EXPECT_DOUBLE_EQ(snap.bids.levels[1].price, 99.0);
    EXPECT_DOUBLE_EQ(snap.bids.levels[2].price, 98.0);

    ASSERT_EQ(snap.asks.count, 2);
    EXPECT_DOUBLE_EQ(snap.asks.levels[0].price, 101.0); // best ask
    EXPECT_DOUBLE_EQ(snap.asks.levels[1].price, 102.0);
}

TEST_F(OrderbookTest, SnapshotResetsUpdateCount) {
    book.apply_update({{100.0, 1.0}}, {});
    book.apply_update({{100.0, 2.0}}, {});

    auto snap = book.load_snapshot();
    EXPECT_GT(snap.update_count, 0u);

    book.apply_snapshot({{50.0, 1.0}}, {{51.0, 1.0}});
    snap = book.load_snapshot();
    EXPECT_EQ(snap.update_count, 0u);
}

TEST_F(OrderbookTest, SnapshotClearsPreviousState) {
    book.apply_snapshot(
        {{100.0, 1.0}, {99.0, 2.0}},
        {{101.0, 1.0}}
    );

    // New snapshot with different data
    book.apply_snapshot(
        {{200.0, 5.0}},
        {{201.0, 6.0}}
    );

    auto snap = book.load_snapshot();

    ASSERT_EQ(snap.bids.count, 1);
    EXPECT_DOUBLE_EQ(snap.bids.levels[0].price, 200.0);

    ASSERT_EQ(snap.asks.count, 1);
    EXPECT_DOUBLE_EQ(snap.asks.levels[0].price, 201.0);
}

TEST_F(OrderbookTest, IncrementalUpdateAddLevel) {
    book.apply_snapshot({{100.0, 1.0}}, {{102.0, 1.0}});

    book.apply_update({{101.0, 3.0}}, {}); // new bid between

    auto snap = book.load_snapshot();
    ASSERT_EQ(snap.bids.count, 2);
    EXPECT_DOUBLE_EQ(snap.bids.levels[0].price, 101.0); // new best bid
    EXPECT_DOUBLE_EQ(snap.bids.levels[1].price, 100.0);
}

TEST_F(OrderbookTest, IncrementalUpdateModifyLevel) {
    book.apply_snapshot({{100.0, 1.0}}, {{101.0, 2.0}});

    book.apply_update({{100.0, 9.0}}, {});

    auto snap = book.load_snapshot();
    ASSERT_EQ(snap.bids.count, 1);
    EXPECT_DOUBLE_EQ(snap.bids.levels[0].size, 9.0);
}

TEST_F(OrderbookTest, IncrementalUpdateRemoveLevel) {
    book.apply_snapshot(
        {{100.0, 1.0}, {99.0, 2.0}},
        {{101.0, 1.0}}
    );

    book.apply_update({{100.0, 0.0}}, {}); // remove best bid

    auto snap = book.load_snapshot();
    ASSERT_EQ(snap.bids.count, 1);
    EXPECT_DOUBLE_EQ(snap.bids.levels[0].price, 99.0);
}

TEST_F(OrderbookTest, UpdateCountIncrementsPerUpdate) {
    book.apply_snapshot({{100.0, 1.0}}, {{101.0, 1.0}});
    EXPECT_EQ(book.load_snapshot().update_count, 0u);

    book.apply_update({{100.0, 2.0}}, {});
    EXPECT_EQ(book.load_snapshot().update_count, 1u);

    book.apply_update({}, {{101.0, 3.0}});
    EXPECT_EQ(book.load_snapshot().update_count, 2u);

    book.apply_update({{99.0, 1.0}}, {{102.0, 1.0}});
    EXPECT_EQ(book.load_snapshot().update_count, 3u);
}

TEST_F(OrderbookTest, SpreadConsistency) {
    book.apply_snapshot(
        {{100.0, 1.0}, {99.0, 2.0}},
        {{101.0, 1.0}, {102.0, 2.0}}
    );

    auto snap = book.load_snapshot();
    double best_bid = snap.bids.levels[0].price;
    double best_ask = snap.asks.levels[0].price;

    EXPECT_LT(best_bid, best_ask); // no crossed book
    EXPECT_DOUBLE_EQ(best_ask - best_bid, 1.0);
}

TEST_F(OrderbookTest, MixedBidAskUpdate) {
    book.apply_snapshot(
        {{100.0, 1.0}},
        {{101.0, 1.0}}
    );

    // Single update touching both sides
    book.apply_update(
        {{100.5, 2.0}},  // new best bid
        {{100.8, 0.5}}   // new best ask (inside spread!)
    );

    auto snap = book.load_snapshot();
    EXPECT_DOUBLE_EQ(snap.bids.levels[0].price, 100.5);
    EXPECT_DOUBLE_EQ(snap.asks.levels[0].price, 100.8);
}

TEST_F(OrderbookTest, EmptyBook) {
    auto snap = book.load_snapshot();
    EXPECT_EQ(snap.bids.count, 0);
    EXPECT_EQ(snap.asks.count, 0);
    EXPECT_EQ(snap.update_count, 0u);
}

TEST_F(OrderbookTest, RemoveAllLevels) {
    book.apply_snapshot(
        {{100.0, 1.0}, {99.0, 2.0}},
        {{101.0, 1.0}}
    );

    book.apply_update({{100.0, 0.0}, {99.0, 0.0}}, {{101.0, 0.0}});

    auto snap = book.load_snapshot();
    EXPECT_EQ(snap.bids.count, 0);
    EXPECT_EQ(snap.asks.count, 0);
}

TEST_F(OrderbookTest, ProductId) {
    EXPECT_EQ(book.product_id(), "BTC-USD");
}

TEST_F(OrderbookTest, BidsDescendingAfterManyUpdates) {
    book.apply_snapshot({{50.0, 1.0}}, {{60.0, 1.0}});

    // Apply many updates in random order
    book.apply_update({{55.0, 1.0}}, {});
    book.apply_update({{52.0, 1.0}}, {});
    book.apply_update({{58.0, 1.0}}, {});
    book.apply_update({{51.0, 1.0}}, {});
    book.apply_update({{57.0, 1.0}}, {});

    auto snap = book.load_snapshot();
    for (uint32_t i = 1; i < snap.bids.count; ++i) {
        EXPECT_GT(snap.bids.levels[i - 1].price, snap.bids.levels[i].price)
            << "Bids not descending at index " << i;
    }
}

TEST_F(OrderbookTest, AsksAscendingAfterManyUpdates) {
    book.apply_snapshot({{50.0, 1.0}}, {{60.0, 1.0}});

    book.apply_update({}, {{65.0, 1.0}});
    book.apply_update({}, {{62.0, 1.0}});
    book.apply_update({}, {{68.0, 1.0}});
    book.apply_update({}, {{61.0, 1.0}});
    book.apply_update({}, {{67.0, 1.0}});

    auto snap = book.load_snapshot();
    for (uint32_t i = 1; i < snap.asks.count; ++i) {
        EXPECT_LT(snap.asks.levels[i - 1].price, snap.asks.levels[i].price)
            << "Asks not ascending at index " << i;
    }
}
