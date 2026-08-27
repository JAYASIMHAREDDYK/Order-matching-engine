#include "order_book.hpp"
#include "matching_engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

using namespace std;

// Fix #1: Anti-DCE sink — the compiler cannot prove this is unused,
// so it must keep the match_into() calls that feed it.
static atomic<int64_t> sink{0};

// Fix #2: Use steady_clock (guaranteed monotonic, usually finer than
// high_resolution_clock on Windows where the latter can alias system_clock
// at ~100ns granularity).
using Clock = chrono::steady_clock;

static double elapsed_ns(Clock::time_point start, Clock::time_point end) {
    return chrono::duration<double, nano>(end - start).count();
}

// ---------- Pre-generated workload (Fix #3) ----------

struct BenchInput {
    vector<Order> resting_sells;
    vector<Order> resting_buys;
    Order          incoming;
};

// Generate a pool of varied inputs OUTSIDE the timed region.
// Uses a fixed seed for reproducibility.
static vector<BenchInput> generate_inputs(int count) {
    mt19937 rng(42);
    uniform_int_distribution<int> num_levels(5, 20);
    uniform_int_distribution<int> price_jitter(-50, 50);
    uniform_int_distribution<int64_t> qty_dist(1, 500);
    uniform_int_distribution<int> side_dist(0, 1);

    vector<BenchInput> pool(count);
    uint64_t id_counter = 1;

    for (int i = 0; i < count; ++i) {
        auto& inp = pool[i];
        int n_sell = num_levels(rng);
        int n_buy  = num_levels(rng);

        // Generate resting sell orders at varied prices above midpoint
        inp.resting_sells.reserve(n_sell);
        for (int j = 0; j < n_sell; ++j) {
            int64_t price = 10050 + j * 10 + price_jitter(rng);
            inp.resting_sells.push_back(
                {id_counter++, price, qty_dist(rng), Side::SELL, OrderType::LIMIT});
        }

        // Generate resting buy orders at varied prices below midpoint
        inp.resting_buys.reserve(n_buy);
        for (int j = 0; j < n_buy; ++j) {
            int64_t price = 10040 - j * 10 + price_jitter(rng);
            inp.resting_buys.push_back(
                {id_counter++, price, qty_dist(rng), Side::BUY, OrderType::LIMIT});
        }

        // Generate the incoming aggressive order
        Side s = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
        int64_t agg_price = (s == Side::BUY)
                                ? 10050 + price_jitter(rng)   // crosses into asks
                                : 10040 + price_jitter(rng);  // crosses into bids
        inp.incoming = {id_counter++, agg_price, qty_dist(rng), s, OrderType::LIMIT};
    }
    return pool;
}

// Seed the book from a BenchInput (resting orders only).
static void seed_book(OrderBook& book, const BenchInput& inp) {
    book.clear();
    vector<Trade> unused;
    for (auto s : inp.resting_sells) {           // copy intentional
        match_into(s, book, unused);
    }
    for (auto b : inp.resting_buys) {            // copy intentional
        match_into(b, book, unused);
    }
}

int main() {
    cout << "=== Order Book Latency Benchmark ===" << endl << endl;

    // Fix #2: Print clock resolution so the user can immediately see
    // whether the platform timer is coarse.
    {
        constexpr double tick_ns =
            Clock::period::num / (double)Clock::period::den * 1e9;
        cout << "Clock tick period : " << tick_ns << " ns" << endl;

        // Empirical resolution: measure the smallest non-zero delta.
        double min_delta = 1e18;
        for (int i = 0; i < 1000; ++i) {
            auto a = Clock::now();
            auto b = Clock::now();
            double d = elapsed_ns(a, b);
            if (d > 0.0) min_delta = min(min_delta, d);
        }
        cout << "Empirical min tick: " << min_delta << " ns" << endl;
    }

    // PMR arena — same as before, untouched.
    char stack_buffer[512 * 1024];
    pmr::monotonic_buffer_resource mem_pool(stack_buffer, sizeof(stack_buffer));
    pmr::unsynchronized_pool_resource pool(&mem_pool);

    OrderBook book(&pool);
    vector<Trade> trades;
    trades.reserve(64);

    // Fix #3: Pre-generate varied inputs (outside the timed region).
    constexpr int ITERS = 1'000'000;
    cout << "Generating " << ITERS << " random inputs..." << flush;
    auto inputs = generate_inputs(ITERS);
    cout << " done." << endl << endl;

    // Warmup — use a subset of the pool so branch predictors/caches see
    // varied patterns, not just the one best-case path.
    for (int i = 0; i < 10'000; ++i) {
        seed_book(book, inputs[i]);
        Order o = inputs[i].incoming;
        match_into(o, book, trades);
        sink.fetch_add(static_cast<int64_t>(trades.size()), memory_order_relaxed);
    }

    // ---------- Timed loop ----------
    vector<double> lat(ITERS);

    cout << "Running " << ITERS << " iterations..." << endl << endl;
    for (int i = 0; i < ITERS; ++i) {
        seed_book(book, inputs[i]);
        Order o = inputs[i].incoming;            // copy so match_into can mutate

        auto t0 = Clock::now();
        match_into(o, book, trades);
        auto t1 = Clock::now();

        lat[i] = elapsed_ns(t0, t1);

        // Fix #1: Force the compiler to observe the result.
        sink.fetch_add(static_cast<int64_t>(trades.size()), memory_order_relaxed);
    }

    // ---------- Statistics ----------
    sort(lat.begin(), lat.end());

    auto pct = [&](int p, int d = 100) -> double {
        return lat[(size_t)ITERS * p / d];
    };

    // Fix #4: Add mean and stdev.
    double sum   = accumulate(lat.begin(), lat.end(), 0.0);
    double mean  = sum / ITERS;
    double sq_sum = 0.0;
    for (double v : lat) sq_sum += (v - mean) * (v - mean);
    double stdev = sqrt(sq_sum / ITERS);

    cout << fixed << setprecision(1);
    cout << "=== Results ===" << endl;
    cout << "mean = " << setw(8) << mean  << " ns" << endl;
    cout << "std  = " << setw(8) << stdev << " ns" << endl;
    cout << "p50  = " << setw(8) << pct(50)  << " ns" << endl;
    cout << "p90  = " << setw(8) << pct(90)  << " ns" << endl;
    cout << "p99  = " << setw(8) << pct(99)  << " ns" << endl;
    cout << "p999 = " << setw(8) << pct(999, 1000) << " ns" << endl;
    cout << "min  = " << setw(8) << lat[0]   << " ns" << endl;
    cout << "max  = " << setw(8) << lat[ITERS - 1] << " ns" << endl;

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

    // Print sink so DCE cannot remove anything that feeds it.
    cout << "sink = " << sink.load() << "  (anti-DCE checksum)" << endl;

    return 0;
}
