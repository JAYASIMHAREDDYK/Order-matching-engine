#include "order_book.hpp"
#include "matching_engine.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

using namespace std;
using Clock = chrono::high_resolution_clock;

// Simple nanosecond timer — no assembly, no platform hacks.
static double elapsed_ns(Clock::time_point start, Clock::time_point end) {
    return chrono::duration<double, nano>(end - start).count();
}

static void seed_book(OrderBook& book) {
    book.clear();
    vector<Trade> unused;
    for (int i = 0; i < 10; ++i) {
        Order s{static_cast<uint64_t>(1000 + i), 10050 + i * 10, 100, Side::SELL, OrderType::LIMIT};
        match_into(s, book, unused);
    }
    for (int i = 0; i < 10; ++i) {
        Order b{static_cast<uint64_t>(2000 + i), 10040 - i * 10, 100, Side::BUY, OrderType::LIMIT};
        match_into(b, book, unused);
    }
}

int main() {
    cout << "=== Order Book Latency Benchmark ===" << endl << endl;

    // Use C++17 unsynchronized_pool_resource to automatically recycle memory during match/clear.
    // This is backed by a upstream monotonic buffer resource which uses a pre-allocated stack buffer.
    char stack_buffer[512 * 1024];
    std::pmr::monotonic_buffer_resource mem_pool(stack_buffer, sizeof(stack_buffer));
    std::pmr::unsynchronized_pool_resource pool(&mem_pool);

    OrderBook book(&pool);
    vector<Trade> trades;
    trades.reserve(8);

    // Warmup
    for (int i = 0; i < 10'000; ++i) {
        seed_book(book);
        Order o{static_cast<uint64_t>(i + 200000), 10050, 100, Side::BUY, OrderType::LIMIT};
        match_into(o, book, trades);
    }

    constexpr int ITERS = 1'000'000;
    vector<double> lat(ITERS);

    cout << "Running " << ITERS << " iterations..." << endl << endl;
    for (int i = 0; i < ITERS; ++i) {
        seed_book(book);
        Order o{static_cast<uint64_t>(i + 100000), 10050, 100, Side::BUY, OrderType::LIMIT};
        auto t0 = Clock::now();
        match_into(o, book, trades);
        auto t1 = Clock::now();
        lat[i] = elapsed_ns(t0, t1);
    }

    sort(lat.begin(), lat.end());

    // Percentile helper: pct(50) = p50, pct(999,1000) = p999
    auto pct = [&](int p, int d = 100) -> double {
        return lat[(size_t)ITERS * p / d];
    };

    cout << fixed << setprecision(1);
    cout << "=== Results ===" << endl;
    cout << "p50  = " << setw(6) << pct(50)  << " ns" << endl;
    cout << "p90  = " << setw(6) << pct(90)  << " ns" << endl;
    cout << "p99  = " << setw(6) << pct(99)  << " ns" << endl;
    cout << "p999 = " << setw(6) << pct(999, 1000) << " ns" << endl;
    cout << "min  = " << setw(6) << lat[0]   << " ns" << endl;
    cout << "max  = " << setw(6) << lat[ITERS - 1] << " ns" << endl;

    // Simple histogram — 10 equal-width buckets
    cout << endl << "=== Latency Distribution ===" << endl;
    double range = lat[ITERS - 1] - lat[0];
    double bucket_w = range / 10.0 + 1.0;
    int buckets[10] = {};
    for (double v : lat) {
        int idx = min(9, (int)((v - lat[0]) / bucket_w));
        ++buckets[idx];
    }
    for (int i = 0; i < 10; ++i) {
        double lo = lat[0] + i * bucket_w;
        double hi = lo + bucket_w;
        cout << "[" << setprecision(0) << setw(6) << lo << " - "
             << setw(6) << hi << " ns]  "
             << setw(7) << buckets[i] << "  ("
             << setprecision(1) << 100.0 * buckets[i] / ITERS << "%)" << endl;
    }

    double p99 = pct(99);
    cout << endl << (p99 < 500.0 ? "PASS" : "WARN")
         << ": p99 " << p99 << " ns "
         << (p99 < 500.0 ? "<" : ">=") << " 500 ns target" << endl;
    return 0;
}
