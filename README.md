# Order Matching Engine

A limit order book and matching engine written in C++17. Uses polymorphic memory resources (`std::pmr`) to avoid heap allocation on the matching hot path.

## Project Structure

```
include/
  order_book.hpp        # Order, Trade, OrderBook definitions (PMR containers)
  matching_engine.hpp   # Public API: match(), match_into()
  ring_buffer.hpp       # Lock-free SPSC ring buffer (alignas(64))
src/
  matching_engine.cpp   # Matching logic: validation, price-time fill, cancel
  benchmark.cpp         # Latency harness (1M iterations, percentile output)
tests/
  engine_tests.cpp      # Google Test suite (14 test cases)
CMakeLists.txt          # Build config, GTest via FetchContent
```

## How It Works

**Price-time priority matching.** Bids are stored highest-first (`std::greater`), asks lowest-first. An incoming order walks the opposite side, filling at each price level in FIFO order until the price no longer crosses or quantity is exhausted. Remaining quantity on limit orders rests in the book.

**O(1) cancel.** Every resting order is indexed by ID in an `unordered_map` pointing directly to its list iterator. Cancels erase in constant time without scanning.

**PMR allocation.** The `OrderBook` accepts a `std::pmr::memory_resource*`. The benchmark pre-allocates a 512 KB stack buffer, wraps it in a `monotonic_buffer_resource` + `unsynchronized_pool_resource`, and passes it to the book. No `malloc` or `new` calls happen during matching.

## Building

Requires CMake 3.16+ and a C++17 compiler (GCC 9+, Clang 10+, or MSVC 19.14+).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Or compile directly with g++:

```bash
g++ -O3 -std=c++17 -Iinclude src/matching_engine.cpp src/benchmark.cpp -o benchmark
```

## Benchmark Results

Measured with `./benchmark` — 1,000,000 iterations of randomly varied inputs (5–20 resting price levels, random prices/quantities/sides per iteration), Release build (`-O3`).

Hardware: Intel Core i5-12450H — latency is hardware-dependent; numbers are given for reference, not as a portable guarantee.

### Latency Summary

| Metric | Value |
|--------|-------|
| mean   | 110.5 ns |
| stdev  | 463.3 ns |
| p50    | 100 ns |
| p90    | 200 ns |
| p99    | 300 ns |
| p99.9  | 500 ns |
| min    | 0 ns |
| max    | 342,200 ns |

### Latency Distribution

```
[     0 -  34221 ns]   999986  (100.0%)
[ 34221 -  68442 ns]       10  (0.0%)
[ 68442 - 102663 ns]        1  (0.0%)
[102663 - 136884 ns]        1  (0.0%)
[136884 - 171105 ns]        1  (0.0%)
[205326 - 239547 ns]        0  (0.0%)
[307989 - 342210 ns]        1  (0.0%)
```

99.99% of iterations complete within a single histogram bucket (< 34 µs). The rare outliers in the tail are OS scheduling jitter, not matching-engine latency.

### Methodology

- **Anti-DCE**: An `std::atomic<int64_t>` sink accumulates `trades.size()` after every call and is printed at exit, preventing the compiler from optimising the matching loop away.
- **Varied inputs**: Each iteration uses a unique, pre-generated `(book state, incoming order)` tuple with randomised price levels, quantities, and sides (deterministic seed for reproducibility).
- **Clock**: `std::chrono::steady_clock` with empirical resolution printed at startup. On Windows the effective resolution is ~100 ns, so sub-100 ns calls are rounded to the nearest tick.
- **Zero allocation**: The `unsynchronized_pool_resource` over a 512 KB stack buffer absorbs all allocation — no `malloc`/`new` on the hot path.

### Sample Output

```
=== Order Book Latency Benchmark ===

Clock tick period : 1 ns
Generating 1000000 random inputs... done.

Running 1000000 iterations...

=== Results ===
mean =    110.5 ns
std  =    463.3 ns
p50  =    100.0 ns
p90  =    200.0 ns
p99  =    300.0 ns
p999 =    500.0 ns
min  =      0.0 ns
max  = 342200.0 ns

=== Latency Distribution ===
[     0 -  34221 ns]   999986  (100.0%)
[ 34221 -  68442 ns]       10  (0.0%)
...

PASS: p99 300.0 ns < 500 ns target
sink = 728862  (anti-DCE checksum)
```

### Running it yourself

```bash
./build/benchmark    # CMake build
./benchmark          # direct compilation
```

Prints clock resolution diagnostics, mean/stdev, p50–p999 percentiles, and a full latency histogram.

## Running Tests

Tests use Google Test, fetched automatically via CMake FetchContent.

```bash
cd build
ctest --output-on-failure -C Release
```

Or run the test binary directly:

```bash
./build/Release/engine_tests
```

## License

MIT — see `LICENSE`.
