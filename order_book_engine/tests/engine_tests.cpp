#include <gtest/gtest.h>
#include "order_book.hpp"
#include "matching_engine.hpp"

namespace {

// Place an order into the book (ignoring any resulting trades).
void place(Order o, OrderBook& book) {
    std::vector<Trade> unused;
    match_into(o, book, unused);
}

} // namespace

TEST(MatchingEngine, FullFill) {
    OrderBook book;
    place({1, 10050, 100, Side::SELL, OrderType::LIMIT}, book);

    Order buy{2, 10050, 100, Side::BUY, OrderType::LIMIT};
    auto trades = match(buy, book);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_EQ(trades[0].aggressor_id, 2u);
    EXPECT_EQ(trades[0].passive_id, 1u);
    EXPECT_TRUE(book.asks.empty());
    EXPECT_TRUE(book.bids.empty());
}

TEST(MatchingEngine, PartialFillRestsRemainder) {
    OrderBook book;
    place({1, 10050, 100, Side::SELL, OrderType::LIMIT}, book);

    Order buy{2, 10050, 150, Side::BUY, OrderType::LIMIT};
    auto trades = match(buy, book);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 100);
    EXPECT_TRUE(book.asks.empty());
    ASSERT_FALSE(book.bids.empty());
    EXPECT_EQ(book.bids[10050].front().quantity, 50);
}

TEST(MatchingEngine, PriceTimePriority) {
    OrderBook book;
    place({1, 10050, 50, Side::SELL, OrderType::LIMIT}, book);
    place({2, 10050, 50, Side::SELL, OrderType::LIMIT}, book);

    Order buy{3, 10050, 50, Side::BUY, OrderType::LIMIT};
    auto trades = match(buy, book);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].passive_id, 1u);
}

TEST(MatchingEngine, MarketOrderSweepsBook) {
    OrderBook book;
    for (int64_t p = 10050; p <= 10080; p += 10) {
        place({static_cast<uint64_t>(p), p, 100, Side::SELL, OrderType::LIMIT}, book);
    }

    Order mkt{999, 0, 400, Side::BUY, OrderType::MARKET};
    auto trades = match(mkt, book);

    EXPECT_EQ(trades.size(), 4u);
    EXPECT_TRUE(book.asks.empty());
}

TEST(MatchingEngine, NoCrossRestsBothSides) {
    OrderBook book;
    place({1, 10050, 100, Side::SELL, OrderType::LIMIT}, book);

    Order buy{2, 10040, 100, Side::BUY, OrderType::LIMIT};
    auto trades = match(buy, book);

    EXPECT_TRUE(trades.empty());
    EXPECT_FALSE(book.asks.empty());
    EXPECT_FALSE(book.bids.empty());
}

TEST(MatchingEngine, MultiLevelFill) {
    OrderBook book;
    place({1, 10050, 50, Side::SELL, OrderType::LIMIT}, book);
    place({2, 10060, 50, Side::SELL, OrderType::LIMIT}, book);
    place({3, 10070, 50, Side::SELL, OrderType::LIMIT}, book);

    Order buy{4, 10060, 100, Side::BUY, OrderType::LIMIT};
    auto trades = match(buy, book);

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, 10050);
    EXPECT_EQ(trades[1].price, 10060);
    EXPECT_EQ(book.asks.size(), 1u);
}

TEST(OrderBook, BestBidAskSpread) {
    OrderBook book;
    place({1, 10050, 100, Side::SELL, OrderType::LIMIT}, book);
    place({2, 10060, 100, Side::SELL, OrderType::LIMIT}, book);
    place({3, 10040, 100, Side::BUY, OrderType::LIMIT}, book);
    place({4, 10030, 100, Side::BUY, OrderType::LIMIT}, book);

    EXPECT_EQ(book.best_ask(), 10050);
    EXPECT_EQ(book.best_bid(), 10040);
    EXPECT_EQ(book.spread(), 10);
}

TEST(OrderBook, EmptyBookSentinels) {
    OrderBook book;
    EXPECT_EQ(book.best_bid(), 0);
    EXPECT_EQ(book.best_ask(), INT64_MAX);
}

TEST(MatchingEngine, SellMatchesBid) {
    OrderBook book;
    place({1, 10050, 100, Side::BUY, OrderType::LIMIT}, book);

    Order sell{2, 10050, 100, Side::SELL, OrderType::LIMIT};
    auto trades = match(sell, book);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].aggressor_id, 2u);
    EXPECT_EQ(trades[0].passive_id, 1u);
    EXPECT_TRUE(book.bids.empty());
    EXPECT_TRUE(book.asks.empty());
}

TEST(MatchingEngine, LargePartialFill) {
    OrderBook book;
    for (uint64_t i = 0; i < 10; ++i)
        place({i, 10050, 10, Side::SELL, OrderType::LIMIT}, book);

    Order buy{100, 10050, 50, Side::BUY, OrderType::LIMIT};
    auto trades = match(buy, book);

    EXPECT_EQ(trades.size(), 5u);
    int64_t total = 0;
    for (const auto& t : trades) total += t.quantity;
    EXPECT_EQ(total, 50);
}

TEST(MatchingEngine, CancelSell) {
    OrderBook book;
    place({42, 10050, 100, Side::SELL, OrderType::LIMIT}, book);

    Order cancel{42, 0, 0, Side::SELL, OrderType::CANCEL};
    auto trades = match(cancel, book);

    EXPECT_TRUE(trades.empty());
    EXPECT_TRUE(book.asks.empty());
}

TEST(MatchingEngine, CancelBuy) {
    OrderBook book;
    place({84, 10040, 100, Side::BUY, OrderType::LIMIT}, book);

    Order cancel{84, 0, 0, Side::BUY, OrderType::CANCEL};
    auto trades = match(cancel, book);

    EXPECT_TRUE(trades.empty());
    EXPECT_TRUE(book.bids.empty());
}

TEST(MatchingEngine, RejectZeroQuantity) {
    OrderBook book;
    Order bad{91, 10050, 0, Side::BUY, OrderType::LIMIT};
    auto trades = match(bad, book);
    EXPECT_TRUE(trades.empty());
    EXPECT_TRUE(book.bids.empty());
}

TEST(MatchingEngine, RejectNegativePrice) {
    OrderBook book;
    Order bad{92, -1, 100, Side::SELL, OrderType::LIMIT};
    auto trades = match(bad, book);
    EXPECT_TRUE(trades.empty());
    EXPECT_TRUE(book.asks.empty());
}

TEST(MatchingEngine, RejectDuplicateId) {
    OrderBook book;
    place({93, 10050, 100, Side::SELL, OrderType::LIMIT}, book);

    Order dup{93, 10050, 100, Side::BUY, OrderType::LIMIT};
    auto trades = match(dup, book);

    EXPECT_TRUE(trades.empty());
    ASSERT_FALSE(book.asks.empty());
    EXPECT_EQ(book.asks[10050].front().order_id, 93u);
    EXPECT_TRUE(book.bids.empty());
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
