#pragma once

#include "order_book.hpp"
#include <vector>

// Match incoming order against the book, writing fills into `trades`.
void match_into(Order& incoming, OrderBook& book, std::vector<Trade>& trades);

// Convenience wrapper that allocates and returns the trade vector.
[[nodiscard]] std::vector<Trade> match(Order& incoming, OrderBook& book);
