#include <gtest/gtest.h>
#include "core/types.hpp"
#include <chrono>
#include <string>

using namespace trader;

// ===== Side enum =====

TEST(Types, SideEnumValues) {
    EXPECT_NE(Side::Buy, Side::Sell);
    EXPECT_EQ(static_cast<int>(Side::Buy), 0);
    EXPECT_EQ(static_cast<int>(Side::Sell), 1);
}

TEST(Types, SideToString) {
    EXPECT_EQ(to_string(Side::Buy), "buy");
    EXPECT_EQ(to_string(Side::Sell), "sell");
}

TEST(Types, SideFromString) {
    EXPECT_EQ(side_from_string("buy"), Side::Buy);
    EXPECT_EQ(side_from_string("sell"), Side::Sell);
    EXPECT_EQ(side_from_string("BUY"), Side::Buy);
    EXPECT_EQ(side_from_string("SELL"), Side::Sell);
}

// ===== OrderStatus enum =====

TEST(Types, OrderStatusValues) {
    // All five states should be distinct
    EXPECT_NE(OrderStatus::Pending, OrderStatus::Open);
    EXPECT_NE(OrderStatus::Open, OrderStatus::Filled);
    EXPECT_NE(OrderStatus::Filled, OrderStatus::Cancelled);
    EXPECT_NE(OrderStatus::Cancelled, OrderStatus::Rejected);
}

TEST(Types, OrderStatusToString) {
    EXPECT_EQ(to_string(OrderStatus::Pending), "pending");
    EXPECT_EQ(to_string(OrderStatus::Open), "open");
    EXPECT_EQ(to_string(OrderStatus::Filled), "filled");
    EXPECT_EQ(to_string(OrderStatus::Cancelled), "cancelled");
    EXPECT_EQ(to_string(OrderStatus::Rejected), "rejected");
}

// ===== Type aliases =====

TEST(Types, PriceIsDouble) {
    Price p = 0.6500;
    EXPECT_DOUBLE_EQ(p, 0.65);
}

TEST(Types, ProbabilityRange) {
    Probability prob = 0.75;
    EXPECT_GE(prob, 0.0);
    EXPECT_LE(prob, 1.0);
}

TEST(Types, ContractPriceRange) {
    ContractPrice cp = 0.42;
    EXPECT_GE(cp, 0.0);
    EXPECT_LE(cp, 1.0);
}

TEST(Types, TimestampIsChronoTimePoint) {
    Timestamp now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    EXPECT_GT(epoch.count(), 0);
}

TEST(Types, OrderIdIsString) {
    OrderId id = "order-12345-abc";
    EXPECT_EQ(id.size(), 15u);
}

// ===== MarketEvent =====

TEST(Types, MarketEventConstruction) {
    MarketEvent event;
    event.ticker = "KXHIGHNY-26APR-T75";
    event.category = "weather";
    event.close_time = std::chrono::system_clock::now();
    event.resolution_time = event.close_time + std::chrono::hours(24);

    EXPECT_EQ(event.ticker, "KXHIGHNY-26APR-T75");
    EXPECT_EQ(event.category, "weather");
    EXPECT_GT(event.resolution_time, event.close_time);
}

// ===== Fee calculation =====

TEST(Types, MakerFeeCalculation) {
    // maker_fee = ceil(0.0175 * contracts * price * (1 - price))
    // At price 0.50: ceil(0.0175 * 1 * 0.50 * 0.50) = ceil(0.004375) = 0.01
    // Kalshi fees are in dollars, ceil to nearest cent
    double fee = kalshi_maker_fee(1, 0.50);
    EXPECT_NEAR(fee, 0.01, 0.001);  // ~0.44 cents per contract

    // At price 0.10: ceil(0.0175 * 1 * 0.10 * 0.90) = ceil(0.001575) = 0.01
    fee = kalshi_maker_fee(1, 0.10);
    EXPECT_NEAR(fee, 0.01, 0.001);

    // At price 0.90: same as 0.10 (symmetric)
    fee = kalshi_maker_fee(1, 0.90);
    EXPECT_NEAR(fee, 0.01, 0.001);

    // 10 contracts at 0.50
    fee = kalshi_maker_fee(10, 0.50);
    EXPECT_NEAR(fee, 0.05, 0.001);
}

TEST(Types, TakerFeeCalculation) {
    // taker_fee = ceil(0.07 * contracts * price * (1 - price))
    // At price 0.50: ceil(0.07 * 1 * 0.50 * 0.50) = ceil(0.0175) = 0.02
    double fee = kalshi_taker_fee(1, 0.50);
    EXPECT_NEAR(fee, 0.02, 0.001);
}
