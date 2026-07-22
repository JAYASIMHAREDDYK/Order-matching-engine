# Order Book Engine

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

## Running the Benchmark

```bash
./build/Release/benchmark   # CMake build
./benchmark                 # direct compilation
```

Outputs p50/p90/p99/p999 latencies and a histogram. Typical results on modern hardware are sub-microsecond p99 for a single match against 10 resting price levels.

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

Unlicensed. Add a `LICENSE` file if you intend to publish.
