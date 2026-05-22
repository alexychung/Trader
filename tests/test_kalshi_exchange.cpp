#include <gtest/gtest.h>
#include "core/types.hpp"
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

// ===== Shadow mode =====

TEST_F(KalshiExchangeTest, ShadowModePlaceOrderSynthesizesIdAndInstantFill) {
    // Shadow mode used to return an empty id and skip position tracking,
    // but that produced two coupled bugs (kill-switch tripped on "empty id
    // = order error", strategy resized to the full target every tick
    // because position never moved). Fix: synthetic id + synthetic fill
    // routed through on_fill. See memory project_shadow_mode_bugs.
    exchange_->set_shadow_mode(true);
    EXPECT_TRUE(exchange_->shadow_mode());

    Order o;
    o.ticker = "KXHIGHNY-26APR-T75";
    o.side = Side::Buy;
    o.contract_side = "yes";
    o.price = 0.55;
    o.quantity = 3;
    o.post_only = true;
    auto id = exchange_->place_order(o);
    EXPECT_FALSE(id.empty());
    EXPECT_NE(id.find("shadow-"), std::string::npos);
    EXPECT_EQ(exchange_->get_position(o.ticker).quantity, 3);
}

TEST_F(KalshiExchangeTest, ShadowModeCancelAllIsNoop) {
    exchange_->set_shadow_mode(true);
    // Even without open orders, cancel_all must return true (idempotent success).
    EXPECT_TRUE(exchange_->cancel_all_orders());
    EXPECT_TRUE(exchange_->cancel_order("synthetic-id"));
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
    // PnL = 5 * $1.00 - 5 * $0.40 - maker fee (Kalshi ceil() schedule)
    double fee = kalshi_maker_fee(5, 0.40);
    EXPECT_DOUBLE_EQ(pos.settled_pnl, 5 * 1.0 - 5 * 0.40 - fee);
}

TEST_F(KalshiExchangeTest, SettlementLoseYes) {
    WsFill fill{.order_id="o1", .ticker="MKT1", .price=0.60, .count=3, .side="yes", .action="buy"};
    exchange_->on_fill(fill);

    // NO wins — we hold YES, so we lose
    exchange_->on_settlement("MKT1", false);

    auto pos = exchange_->get_position("MKT1");
    EXPECT_TRUE(pos.is_settled);
    EXPECT_FALSE(pos.outcome);
    // PnL = -total_cost - maker fee
    double fee = kalshi_maker_fee(3, 0.60);
    EXPECT_DOUBLE_EQ(pos.settled_pnl, -(3 * 0.60) - fee);
}

TEST_F(KalshiExchangeTest, SettlementWinNo) {
    WsFill fill{.order_id="o1", .ticker="MKT1", .price=0.55, .count=4, .side="no", .action="buy"};
    exchange_->on_fill(fill);

    // NO wins — we hold NO, so we win
    exchange_->on_settlement("MKT1", false);

    auto pos = exchange_->get_position("MKT1");
    EXPECT_TRUE(pos.is_settled);
    // PnL = 4 * $1.00 - 4 * $0.55 - maker fee
    double fee = kalshi_maker_fee(4, 0.55);
    EXPECT_DOUBLE_EQ(pos.settled_pnl, 4 * 1.0 - 4 * 0.55 - fee);
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
    double fee = kalshi_maker_fee(5, 0.40);
    EXPECT_DOUBLE_EQ(settlements[0].pnl, 5 * 1.0 - 5 * 0.40 - fee);
    EXPECT_EQ(settlements[0].contracts, 5);
}

TEST_F(KalshiExchangeTest, OnFillDedupsByTradeId) {
    // Same trade_id delivered twice — second should be a no-op. This is the
    // core invariant keeping REST reconciliation safe when WS is also
    // replaying recent fills post-reconnect.
    WsFill f{};
    f.order_id = "o1";
    f.trade_id = "trade-xyz";
    f.ticker = "MKT1";
    f.price = 0.40;
    f.count = 5;
    f.side = "yes";
    f.action = "buy";

    double initial_balance = exchange_->get_balance();
    exchange_->on_fill(f);
    double after_first = exchange_->get_balance();
    exchange_->on_fill(f);  // same trade_id → dedup
    double after_second = exchange_->get_balance();

    EXPECT_NE(initial_balance, after_first);
    EXPECT_DOUBLE_EQ(after_first, after_second);

    auto pos = exchange_->get_position("MKT1");
    EXPECT_EQ(pos.quantity, 5);  // still 5, not 10
}

TEST_F(KalshiExchangeTest, OnFillBumpsWatermarkMonotonically) {
    using namespace std::chrono;

    WsFill early{};
    early.order_id = "o1"; early.trade_id = "t1";
    early.ticker = "MKT1"; early.price = 0.5; early.count = 1;
    early.side = "yes"; early.action = "buy";
    early.timestamp = Timestamp(milliseconds(1'700'000'000'000LL));

    WsFill late = early;
    late.order_id = "o2"; late.trade_id = "t2";
    late.timestamp = Timestamp(milliseconds(1'700'000'005'000LL));

    WsFill out_of_order = early;
    out_of_order.order_id = "o3"; out_of_order.trade_id = "t3";
    out_of_order.timestamp = Timestamp(milliseconds(1'700'000'002'000LL));

    exchange_->on_fill(early);
    EXPECT_EQ(exchange_->last_fill_ts_ms(), 1'700'000'000'000LL);
    exchange_->on_fill(late);
    EXPECT_EQ(exchange_->last_fill_ts_ms(), 1'700'000'005'000LL);
    // Out-of-order fill must NOT regress the watermark — otherwise a replay
    // would re-fetch the later fills on next reconcile.
    exchange_->on_fill(out_of_order);
    EXPECT_EQ(exchange_->last_fill_ts_ms(), 1'700'000'005'000LL);
}
