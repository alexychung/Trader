#include <gtest/gtest.h>
#include "exchange/mock_exchange.hpp"

using namespace trader;

TEST(MockExchange, ConnectDisconnect) {
    MockExchange ex(100.0);
    EXPECT_FALSE(ex.is_connected());
    EXPECT_TRUE(ex.connect());
    EXPECT_TRUE(ex.is_connected());
    ex.disconnect();
    EXPECT_FALSE(ex.is_connected());
}

TEST(MockExchange, BalanceIsConfigurable) {
    MockExchange ex(42.5);
    EXPECT_DOUBLE_EQ(ex.get_balance(), 42.5);
}

TEST(MockExchange, PlaceOrderLogsAndReturnsUniqueId) {
    MockExchange ex;
    Order o;
    o.ticker = "MKT1";
    o.side = Side::Buy;
    o.contract_side = "yes";
    o.price = 0.45;
    o.quantity = 3;

    auto id1 = ex.place_order(o);
    auto id2 = ex.place_order(o);
    EXPECT_NE(id1, "");
    EXPECT_NE(id2, "");
    EXPECT_NE(id1, id2);
    EXPECT_EQ(ex.order_count(), 2u);

    auto logged = ex.orders();
    ASSERT_EQ(logged.size(), 2u);
    EXPECT_EQ(logged[0].order.ticker, "MKT1");
    EXPECT_DOUBLE_EQ(logged[0].order.price, 0.45);
    EXPECT_FALSE(logged[0].canceled);
}

TEST(MockExchange, CancelMarksOrder) {
    MockExchange ex;
    Order o;
    o.ticker = "MKT1";
    o.quantity = 1;
    o.price = 0.30;
    auto id = ex.place_order(o);
    EXPECT_TRUE(ex.cancel_order(id));
    EXPECT_FALSE(ex.cancel_order(id));   // double-cancel is a no-op
    EXPECT_FALSE(ex.cancel_order("bogus"));
    EXPECT_EQ(ex.canceled_count(), 1u);
}

TEST(MockExchange, ClockDrivenPlacedAt) {
    MockExchange ex;
    auto t = std::chrono::system_clock::from_time_t(1700000000);  // 2023-11-14
    ex.set_clock(t);
    Order o; o.ticker = "MKT1"; o.quantity = 1; o.price = 0.5;
    ex.place_order(o);
    auto logged = ex.orders();
    ASSERT_EQ(logged.size(), 1u);
    EXPECT_EQ(logged[0].placed_at, t);
}

TEST(MockExchange, ImplementsIExchange) {
    std::unique_ptr<IExchange> iface = std::make_unique<MockExchange>(50.0);
    EXPECT_DOUBLE_EQ(iface->get_balance(), 50.0);
    Order o; o.ticker = "X"; o.quantity = 1; o.price = 0.5;
    EXPECT_NE(iface->place_order(o), "");
}
