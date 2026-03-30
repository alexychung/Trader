#include <gtest/gtest.h>
#include "exchange/iexchange.hpp"
#include "strategy/istrategy.hpp"
#include <memory>

using namespace trader;

// ===== Mock implementations to test interfaces =====

class MockExchange : public IExchange {
public:
    bool connect() override { connected_ = true; return true; }
    void disconnect() override { connected_ = false; }
    bool is_connected() const override { return connected_; }

    OrderId place_order(const Order& order) override {
        last_order_ = order;
        return "mock-order-123";
    }
    bool cancel_order(const OrderId& id) override {
        cancelled_id_ = id;
        return true;
    }
    double get_balance() const override { return 100.0; }

    bool connected_ = false;
    Order last_order_;
    OrderId cancelled_id_;
};

class MockStrategy : public IStrategy {
public:
    void on_market_update(const MarketUpdate& update) override {
        last_update_ = update;
        update_count_++;
    }
    void on_data_signal(const DataSignal& signal) override {
        signal_count_++;
    }
    void on_fill(const Fill& fill) override {
        fill_count_++;
    }
    void on_settlement(const Settlement& settlement) override {
        settlement_count_++;
    }
    std::vector<TradeSignal> generate_signals() override {
        return signals_;
    }

    MarketUpdate last_update_;
    int update_count_ = 0;
    int signal_count_ = 0;
    int fill_count_ = 0;
    int settlement_count_ = 0;
    std::vector<TradeSignal> signals_;
};

// ===== IExchange tests =====

TEST(IExchange, ConnectDisconnect) {
    auto exchange = std::make_unique<MockExchange>();
    EXPECT_FALSE(exchange->is_connected());
    EXPECT_TRUE(exchange->connect());
    EXPECT_TRUE(exchange->is_connected());
    exchange->disconnect();
    EXPECT_FALSE(exchange->is_connected());
}

TEST(IExchange, PlaceOrder) {
    auto exchange = std::make_unique<MockExchange>();
    Order order;
    order.ticker = "KXHIGHNY-26APR-T75";
    order.side = Side::Buy;
    order.contract_side = "yes";
    order.price = 0.65;
    order.quantity = 5;
    order.post_only = true;

    OrderId id = exchange->place_order(order);
    EXPECT_EQ(id, "mock-order-123");
    EXPECT_EQ(exchange->last_order_.ticker, "KXHIGHNY-26APR-T75");
    EXPECT_EQ(exchange->last_order_.side, Side::Buy);
    EXPECT_EQ(exchange->last_order_.contract_side, "yes");
    EXPECT_DOUBLE_EQ(exchange->last_order_.price, 0.65);
    EXPECT_EQ(exchange->last_order_.quantity, 5);
    EXPECT_TRUE(exchange->last_order_.post_only);
}

TEST(IExchange, CancelOrder) {
    auto exchange = std::make_unique<MockExchange>();
    EXPECT_TRUE(exchange->cancel_order("order-to-cancel"));
    EXPECT_EQ(exchange->cancelled_id_, "order-to-cancel");
}

TEST(IExchange, GetBalance) {
    auto exchange = std::make_unique<MockExchange>();
    EXPECT_DOUBLE_EQ(exchange->get_balance(), 100.0);
}

TEST(IExchange, PolymorphicUsage) {
    // Verify interface can be used polymorphically
    std::unique_ptr<IExchange> exchange = std::make_unique<MockExchange>();
    EXPECT_TRUE(exchange->connect());
    EXPECT_TRUE(exchange->is_connected());
}

// ===== IStrategy tests =====

TEST(IStrategy, OnMarketUpdate) {
    auto strategy = std::make_unique<MockStrategy>();
    MarketUpdate update;
    update.ticker = "KXHIGHNY-26APR-T75";
    update.yes_bid = 0.60;
    update.yes_ask = 0.65;
    update.volume = 150;

    strategy->on_market_update(update);
    EXPECT_EQ(strategy->update_count_, 1);
    EXPECT_EQ(strategy->last_update_.ticker, "KXHIGHNY-26APR-T75");
    EXPECT_DOUBLE_EQ(strategy->last_update_.yes_bid, 0.60);
    EXPECT_DOUBLE_EQ(strategy->last_update_.yes_ask, 0.65);
}

TEST(IStrategy, GenerateSignals) {
    auto strategy = std::make_unique<MockStrategy>();
    TradeSignal signal;
    signal.ticker = "KXHIGHNY-26APR-T75";
    signal.side = Side::Buy;
    signal.contract_side = "yes";
    signal.model_probability = 0.85;
    signal.market_price = 0.65;
    signal.edge = 0.20;
    signal.quantity = 5;
    signal.rationale = "Ensemble shows 85% probability, market at 65%";

    strategy->signals_.push_back(signal);
    auto signals = strategy->generate_signals();

    ASSERT_EQ(signals.size(), 1u);
    EXPECT_EQ(signals[0].ticker, "KXHIGHNY-26APR-T75");
    EXPECT_DOUBLE_EQ(signals[0].model_probability, 0.85);
    EXPECT_DOUBLE_EQ(signals[0].edge, 0.20);
}

TEST(IStrategy, OnFillAndSettlement) {
    auto strategy = std::make_unique<MockStrategy>();

    Fill fill;
    fill.order_id = "order-123";
    fill.ticker = "KXHIGHNY-26APR-T75";
    fill.price = 0.65;
    fill.quantity = 5;
    strategy->on_fill(fill);
    EXPECT_EQ(strategy->fill_count_, 1);

    Settlement settlement;
    settlement.ticker = "KXHIGHNY-26APR-T75";
    settlement.outcome = true;  // YES won
    settlement.pnl = 1.75;
    strategy->on_settlement(settlement);
    EXPECT_EQ(strategy->settlement_count_, 1);
}

TEST(IStrategy, PolymorphicUsage) {
    std::unique_ptr<IStrategy> strategy = std::make_unique<MockStrategy>();
    auto signals = strategy->generate_signals();
    EXPECT_TRUE(signals.empty());
}
