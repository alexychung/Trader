#include <gtest/gtest.h>
#include "core/event_bus.hpp"
#include <thread>
#include <chrono>
#include <vector>

using namespace trader;

TEST(SPSCQueue, PushPopSingleItem) {
    SPSCQueue<int> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0u);

    EXPECT_TRUE(queue.try_push(42));
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1u);

    auto item = queue.try_pop();
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(*item, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(SPSCQueue, PopFromEmptyReturnsNullopt) {
    SPSCQueue<int> queue;
    auto item = queue.try_pop();
    EXPECT_FALSE(item.has_value());
}

TEST(SPSCQueue, FIFOOrdering) {
    SPSCQueue<int> queue;
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(queue.try_push(i));
    }
    for (int i = 0; i < 100; ++i) {
        auto item = queue.try_pop();
        ASSERT_TRUE(item.has_value());
        EXPECT_EQ(*item, i);
    }
}

TEST(SPSCQueue, FullQueueReturnsFalse) {
    SPSCQueue<int, 4> queue;  // Capacity = 4, usable = 3 (one slot reserved)
    EXPECT_TRUE(queue.try_push(1));
    EXPECT_TRUE(queue.try_push(2));
    EXPECT_TRUE(queue.try_push(3));
    EXPECT_FALSE(queue.try_push(4));  // Full
}

TEST(SPSCQueue, WrapAround) {
    SPSCQueue<int, 4> queue;
    // Fill and drain multiple times to exercise wrap-around
    for (int round = 0; round < 10; ++round) {
        EXPECT_TRUE(queue.try_push(round * 10 + 1));
        EXPECT_TRUE(queue.try_push(round * 10 + 2));
        auto a = queue.try_pop();
        auto b = queue.try_pop();
        ASSERT_TRUE(a.has_value());
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(*a, round * 10 + 1);
        EXPECT_EQ(*b, round * 10 + 2);
    }
}

TEST(SPSCQueue, WorksWithStructs) {
    struct Message {
        std::string ticker;
        double price;
        int quantity;
    };

    SPSCQueue<Message, 64> queue;
    Message msg{"KXHIGHNY-26APR-T75", 0.65, 5};
    EXPECT_TRUE(queue.try_push(msg));

    auto result = queue.try_pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ticker, "KXHIGHNY-26APR-T75");
    EXPECT_DOUBLE_EQ(result->price, 0.65);
    EXPECT_EQ(result->quantity, 5);
}

TEST(SPSCQueue, CrossThreadCorrectness) {
    constexpr int NUM_ITEMS = 100000;
    SPSCQueue<int> queue;
    std::vector<int> received;
    received.reserve(NUM_ITEMS);

    // Producer thread
    std::thread producer([&queue]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!queue.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer thread
    std::thread consumer([&queue, &received]() {
        int count = 0;
        while (count < NUM_ITEMS) {
            auto item = queue.try_pop();
            if (item.has_value()) {
                received.push_back(*item);
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // Verify all items received in order
    ASSERT_EQ(received.size(), static_cast<size_t>(NUM_ITEMS));
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(received[i], i) << "Mismatch at index " << i;
    }
}

TEST(SPSCQueue, ThroughputBenchmark) {
    constexpr int NUM_ITEMS = 1000000;
    SPSCQueue<int> queue;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&queue]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!queue.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    int consumed = 0;
    std::thread consumer([&queue, &consumed]() {
        while (consumed < NUM_ITEMS) {
            if (queue.try_pop().has_value()) {
                ++consumed;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ns_per_msg = static_cast<double>(ns) / NUM_ITEMS;

    // Target: < 200ns per message in Debug, < 100ns in Release
    // Debug builds have no optimizations, so relax threshold
    EXPECT_LT(ns_per_msg, 200.0) << "SPSC queue too slow: " << ns_per_msg << " ns/msg";

    // Log the result for reference
    std::cout << "[BENCHMARK] SPSC throughput: " << ns_per_msg << " ns/msg ("
              << (1e9 / ns_per_msg / 1e6) << " M msg/sec)" << std::endl;
}
