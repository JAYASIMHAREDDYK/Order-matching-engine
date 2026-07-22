#pragma once

#include <cstdint>
#include <climits>
#include <list>
#include <map>
#include <unordered_map>
#include <memory_resource>

enum class Side : uint8_t { BUY, SELL };
enum class OrderType : uint8_t { LIMIT, MARKET, CANCEL };

struct Order {
    uint64_t  order_id;
    int64_t   price;      // integer ticks (e.g. cents)
    int64_t   quantity;
    Side      side;
    OrderType type;
};

struct Trade {
    uint64_t aggressor_id;
    uint64_t passive_id;
    int64_t  price;
    int64_t  quantity;
};

struct OrderLocation {
    Side side;
    int64_t price;
    std::pmr::list<Order>::iterator iter;
};

// Limit order book with pmr price-level queues and O(1) order lookup.
struct OrderBook {
    using Queue = std::pmr::list<Order>;

    std::pmr::map<int64_t, Queue, std::greater<int64_t>> bids;
    std::pmr::map<int64_t, Queue>                        asks;
    std::pmr::unordered_map<uint64_t, OrderLocation>     order_index;

    // Construct OrderBook with a memory resource (defaults to new_delete_resource)
    OrderBook(std::pmr::memory_resource* mr = std::pmr::new_delete_resource())
        : bids(mr), asks(mr), order_index(mr) {}

    [[nodiscard]] int64_t best_bid() const { return bids.empty() ? 0 : bids.begin()->first; }
    [[nodiscard]] int64_t best_ask() const { return asks.empty() ? INT64_MAX : asks.begin()->first; }
    [[nodiscard]] int64_t spread()   const { return best_ask() - best_bid(); }

    void clear() { bids.clear(); asks.clear(); order_index.clear(); }
};

