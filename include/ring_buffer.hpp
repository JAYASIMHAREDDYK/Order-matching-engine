#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Lock-free single-producer / single-consumer ring buffer.
// Head and tail are cache-line-isolated to prevent false sharing.
template<typename T, std::size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "capacity must be a power of two");
    static_assert(N > 0, "capacity must be positive");

    std::array<T, N> buf_;
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};

public:
    [[nodiscard]] bool push(const T& item) noexcept {
        uint64_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) == N) return false;
        buf_[h & (N - 1)] = item;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(T& out) noexcept {
        uint64_t t = tail_.load(std::memory_order_relaxed);
        if (head_.load(std::memory_order_acquire) == t) return false;
        out = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // Approximate snapshot; not linearizable across threads.
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint64_t size() const noexcept {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_acquire);
    }
};
