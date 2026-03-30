#include <gtest/gtest.h>
#include "exchange/kalshi/kalshi_exchange.hpp"

using namespace trader;
using namespace trader::kalshi;

class KalshiExchangeTest : public ::testing::Test {
protected:
    void SetUp() override {
        auth_.set_api_key_id("test-key");
        exchange_ = std::make_unique<KalshiExchange>(
            "https://demo-api.kalshi.co/trade-api/v2",
            "wss://demo-api.kalshi.co/trade-api/ws/v2",
            auth_
        );
    }

    KalshiAuth auth_;
    std::unique_ptr<KalshiExchange> exchange_;
};

// ===== Position Tracking =====

TEST_F(KalshiExchangeTest, InitialPositionIsEmpty) {
    auto pos = exchange_->get_position("KXHIGHNY-26APR-T75");
    EXPECT_EQ(pos.quantity, 0);
    EXPECT_DOUBLE_EQ(pos.avg_cost, 0.0);
}

TEST_F(KalshiExchangeTest, FillUpdatesPosition) {
    WsFill fill;
    fill.order_id = "ord-1";
    fill.ticker = "KXHIGHNY-26APR-T75";
    fill.price = 0.65;
    fill.count = 5;
    fill.side = "yes";
    fill.action = "buy";

    exchange_->on_fill(fill);

    auto pos = exchange_->get_position("KXHIGHNY-26APR-T75");
    EXPECT_EQ(pos.quantity, 5);
    EXPECT_DOUBLE_EQ(pos.avg_cost, 0.65);
    EXPECT_DOUBLE_EQ(pos.total_cost, 3.25);
    EXPECT_EQ(pos.contract_side, "yes");
}

TEST_F(KalshiExchangeTest, MultipleFillsAverageCost) {
    WsFill fill1;
    fill1.order_id = "ord-1";
    fill1.ticker = "MKT1";
    fill1.price = 0.50;
    fill1.count = 4;
    fill1.side = "yes";
    fill1.action = "buy";
    exchange_->on_fill(fill1);

    WsFill fill2;
    fill2.order_id = "ord-2";
    fill2.ticker = "MKT1";
    fill2.price = 0.60;
    fill2.count = 6;
    fill2.side = "yes";
    fill2.action = "buy";
    exchange_->on_fill(fill2);

    auto pos = exchange_->get_position("MKT1");
    EXPECT_EQ(pos.quantity, 10);
    EXPECT_DOUBLE_EQ(pos.total_cost, 4 * 0.50 + 6 * 0.60);  // 5.60
    EXPECT_DOUBLE_EQ(pos.avg_cost, 5.60 / 10.0);  // 0.56
}

TEST_F(KalshiExchangeTest, SellReducesPosition) {
    WsFill buy;
    buy.order_id = "ord-1";
    buy.ticker = "MKT1";
    buy.price = 0.50;
    buy.count = 10;
    buy.side = "yes";
    buy.action = "buy";
    exchange_->on_fill(buy);

    WsFill sell;
    sell.order_id = "ord-2";
    sell.ticker = "MKT1";
    sell.price = 0.60;
    sell.count = 4;
    sell.side = "yes";
    sell.action = "sell";
    exchange_->on_fill(sell);

    auto pos = exchange_->get_position("MKT1");
    EXPECT_EQ(pos.quantity, 6);
}

// ===== Total Exposure =====

TEST_F(KalshiExchangeTest, TotalExposureAcrossMarkets) {
    WsFill fill1{.order_id="o1", .ticker="MKT1", .price=0.50, .count=5, .side="yes", .action="buy"};
    WsFill fill2{.order_id="o2", .ticker="MKT2", .price=0.30, .count=10, .side="no", .action="buy"};

    exchange_->on_fill(fill1);
    exchange_->on_fill(fill2);

    double exposure = exchange_->total_exposure();
    EXPECT_DOUBLE_EQ(exposure, 5 * 0.50 + 10 * 0.30);  // 2.50 + 3.00 = 5.50
}

// ===== Settlement =====

TEST_F(KalshiExchangeTest, SettlementWinYes) {
    WsFill fill{.order_id="o1", .ticker="MKT1", .price=0.40, .count=5, .side="yes", .action="buy"};
    exchange_->on_fill(fill);

    // YES wins — we hold YES, so we win
    exchange_->on_settlement("MKT1", true);

    auto pos = exchange_->get_position("MKT1");
    EXPECT_TRUE(pos.is_settled);
    EXPECT_TRUE(pos.outcome);
    // PnL = 5 * $1.00 - 5 * $0.40 = $3.00
    EXPECT_DOUBLE_EQ(pos.settled_pnl, 3.0);
}

TEST_F(KalshiExchangeTest, SettlementLoseYes) {
    WsFill fill{.order_id="o1", .ticker="MKT1", .price=0.60, .count=3, .side="yes", .action="buy"};
    exchange_->on_fill(fill);

    // NO wins — we hold YES, so we lose
    exchange_->on_settlement("MKT1", false);

    auto pos = exchange_->get_position("MKT1");
    EXPECT_TRUE(pos.is_settled);
    EXPECT_FALSE(pos.outcome);
    // PnL = -total_cost = -(3 * 0.60) = -$1.80
    EXPECT_DOUBLE_EQ(pos.settled_pnl, -1.80);
}

TEST_F(KalshiExchangeTest, SettlementWinNo) {
    WsFill fill{.order_id="o1", .ticker="MKT1", .price=0.55, .count=4, .side="no", .action="buy"};
    exchange_->on_fill(fill);

    // NO wins — we hold NO, so we win
    exchange_->on_settlement("MKT1", false);

    auto pos = exchange_->get_position("MKT1");
    EXPECT_TRUE(pos.is_settled);
    // PnL = 4 * $1.00 - 4 * $0.55 = $1.80
    EXPECT_DOUBLE_EQ(pos.settled_pnl, 1.80);
}

TEST_F(KalshiExchangeTest, SettledPositionsExcludedFromExposure) {
    WsFill fill{.order_id="o1", .ticker="MKT1", .price=0.50, .count=5, .side="yes", .action="buy"};
    exchange_->on_fill(fill);

    EXPECT_DOUBLE_EQ(exchange_->total_exposure(), 2.50);

    exchange_->on_settlement("MKT1", true);

    // Settled positions should not count toward active exposure
    // (is_settled = true means the position has resolved)
    auto positions = exchange_->get_all_positions();
    EXPECT_EQ(positions.size(), 0u);  // No active positions
}

// ===== Order Tracking =====

TEST_F(KalshiExchangeTest, OpenOrdersTrackCorrectly) {
    // Simulate directly since we can't hit real API
    auto open = exchange_->get_open_orders();
    EXPECT_EQ(open.size(), 0u);  // No orders placed yet
}

// ===== IExchange Interface =====

TEST_F(KalshiExchangeTest, ImplementsIExchange) {
    // Verify polymorphic usage
    std::unique_ptr<IExchange> iface = std::make_unique<KalshiExchange>(
        "https://demo-api.kalshi.co/trade-api/v2",
        "wss://demo-api.kalshi.co/trade-api/ws/v2",
        auth_
    );
    EXPECT_DOUBLE_EQ(iface->get_balance(), 0.0);  // Not connected, default 0
}

TEST_F(KalshiExchangeTest, CheckSettlementsReturnsSettled) {
    WsFill fill{.order_id="o1", .ticker="MKT1", .price=0.40, .count=5, .side="yes", .action="buy"};
    exchange_->on_fill(fill);
    exchange_->on_settlement("MKT1", true);

    auto settlements = exchange_->check_settlements();
    ASSERT_EQ(settlements.size(), 1u);
    EXPECT_EQ(settlements[0].ticker, "MKT1");
    EXPECT_TRUE(settlements[0].outcome);
    EXPECT_DOUBLE_EQ(settlements[0].pnl, 3.0);
    EXPECT_EQ(settlements[0].contracts, 5);
}
