#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace trader {

// Lock-free single-producer single-consumer ring buffer.
// Cache-line aligned to prevent false sharing between producer and consumer threads.
template<typename T, size_t Capacity = 4096>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Producer: attempt to push an item. Returns false if queue is full.
    bool try_push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // Full
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: attempt to pop an item. Returns nullopt if queue is empty.
    std::optional<T> try_pop() {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // Empty
        }
        T item = buffer_[tail];
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return item;
    }

    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & mask_;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    static constexpr size_t capacity() { return Capacity; }

private:
    static constexpr size_t mask_ = Capacity - 1;

    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    std::array<T, Capacity> buffer_;
};

} // namespace trader
