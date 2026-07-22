#include "matching_engine.hpp"
#include <algorithm>

namespace {

// Check if the order is valid before processing.
bool is_valid(const Order& order, const OrderBook& book) {
    if (order.type == OrderType::CANCEL)
        return true;
    if (order.quantity <= 0)
        return false;
    if (order.type == OrderType::LIMIT && order.price <= 0)
        return false;
    // Reject duplicate order IDs.
    return book.order_index.count(order.order_id) == 0;
}

// Remove an existing order from the book. Returns true if found and removed.
bool remove_order(OrderBook& book, uint64_t id) {
    auto found = book.order_index.find(id);
    if (found == book.order_index.end())
        return false;

    auto info = found->second;
    if (info.side == Side::BUY) {
        auto level = book.bids.find(info.price);
        if (level == book.bids.end()) {
            book.order_index.erase(found);
            return false;
        }
        level->second.erase(info.iter);
        if (level->second.empty())
            book.bids.erase(level);
    } else {
        auto level = book.asks.find(info.price);
        if (level == book.asks.end()) {
            book.order_index.erase(found);
            return false;
        }
        level->second.erase(info.iter);
        if (level->second.empty())
            book.asks.erase(level);
    }
    book.order_index.erase(found);
    return true;
}

// Simple price-crossing check — replaces the template lambda approach.
// A buy crosses if its price >= the ask (or it's a market order).
// A sell crosses if its price <= the bid (or it's a market order).
bool prices_cross(const Order& incoming, int64_t resting_price) {
    if (incoming.type == OrderType::MARKET)
        return true;
    if (incoming.side == Side::BUY)
        return incoming.price >= resting_price;
    return incoming.price <= resting_price;
}

// Fill incoming order against the opposite side.
template <typename MapType>
void fill_side(Order& incoming, MapType& opposite, OrderBook& book, std::vector<Trade>& trades) {
    while (incoming.quantity > 0 && !opposite.empty()) {
        auto best = opposite.begin();

        if (!prices_cross(incoming, best->first))
            break;

        auto& queue = best->second;
        while (incoming.quantity > 0 && !queue.empty()) {
            auto& resting = queue.front();
            int64_t filled = std::min(incoming.quantity, resting.quantity);

            Trade t;
            t.aggressor_id = incoming.order_id;
            t.passive_id = resting.order_id;
            t.price = best->first;
            t.quantity = filled;
            trades.push_back(t);

            incoming.quantity -= filled;
            resting.quantity  -= filled;

            if (resting.quantity == 0) {
                book.order_index.erase(resting.order_id);
                queue.pop_front();
            }
        }

        if (queue.empty())
            opposite.erase(best);
    }
}

void fill_order(Order& incoming, OrderBook& book, std::vector<Trade>& trades) {
    if (incoming.side == Side::BUY) {
        fill_side(incoming, book.asks, book, trades);
    } else {
        fill_side(incoming, book.bids, book, trades);
    }
}

// If there's leftover quantity on a limit order, add it to the book.
void add_to_book(Order& order, OrderBook& book) {
    if (order.quantity <= 0 || order.type != OrderType::LIMIT)
        return;
    
    if (order.side == Side::BUY) {
        auto& queue = book.bids[order.price];
        queue.push_back(order);
        OrderLocation loc;
        loc.side = order.side;
        loc.price = order.price;
        loc.iter = std::prev(queue.end());
        book.order_index[order.order_id] = loc;
    } else {
        auto& queue = book.asks[order.price];
        queue.push_back(order);
        OrderLocation loc;
        loc.side = order.side;
        loc.price = order.price;
        loc.iter = std::prev(queue.end());
        book.order_index[order.order_id] = loc;
    }
}

} // namespace

// --- Public API (signatures must match matching_engine.hpp) ---

void match_into(Order& incoming, OrderBook& book, std::vector<Trade>& trades) {
    trades.clear();

    if (!is_valid(incoming, book))
        return;

    // Handle cancel requests.
    if (incoming.type == OrderType::CANCEL) {
        remove_order(book, incoming.order_id);
        return;
    }

    // Fill against opposite side, then rest any remainder.
    fill_order(incoming, book, trades);
    add_to_book(incoming, book);
}

std::vector<Trade> match(Order& incoming, OrderBook& book) {
    std::vector<Trade> trades;
    trades.reserve(4);
    match_into(incoming, book, trades);
    return trades;
}

