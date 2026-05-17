// Tests for position seeding at startup — covers the logic added when we
// discovered that positions opened before state.json's last_fill_ts_ms
// watermark were invisible to the risk manager, letting the bot stack
// orders on top of already-funded positions.
//
// Two things under test:
//   1. KalshiRestClient::parse_market_positions — pure JSON parsing of the
//      /portfolio/positions response. Mocked body; no HTTP.
//   2. RiskManager::position_quantity — accessor added for the event
//      strategy's "order only the delta" fix.

#include <gtest/gtest.h>
#include "exchange/kalshi/rest_client.hpp"
#include "risk/risk_manager.hpp"
#include <nlohmann/json.hpp>

using namespace trader;
using namespace trader::kalshi;

// ===== KalshiRestClient::parse_market_positions =====

namespace {

// Minimal realistic positions body, modeled on the shape we captured from
// Kalshi demo on 2026-04-22. String-encoded dollar fields, position_fp as
// decimal string. Multi-market so order-preserving tests can check indexing.
const char* kSampleBody = R"({
  "cursor": "",
  "market_positions": [
    {
      "ticker": "KXHIGHNY-26APR23-T68",
      "position_fp": "4.00",
      "market_exposure_dollars": "2.400000",
      "total_traded_dollars": "2.400000",
      "fees_paid_dollars": "0.080000",
      "realized_pnl_dollars": "0.000000",
      "resting_orders_count": 0
    },
    {
      "ticker": "KXHIGHCHI-26APR23-B84.5",
      "position_fp": "2.00",
      "market_exposure_dollars": "0.450000",
      "total_traded_dollars": "0.450000",
      "fees_paid_dollars": "0.040000",
      "realized_pnl_dollars": "0.000000",
      "resting_orders_count": 0
    }
  ]
})";

}  // namespace

TEST(ParseMarketPositions, HappyPath) {
    auto body = nlohmann::json::parse(kSampleBody);
    auto positions = KalshiRestClient::parse_market_positions(body);

    ASSERT_EQ(positions.size(), 2u);

    EXPECT_EQ(positions[0].ticker, "KXHIGHNY-26APR23-T68");
    EXPECT_EQ(positions[0].position, 4);
    EXPECT_DOUBLE_EQ(positions[0].cost, 2.4);
    EXPECT_DOUBLE_EQ(positions[0].fees_paid, 0.08);
    EXPECT_DOUBLE_EQ(positions[0].realized_pnl, 0.0);

    EXPECT_EQ(positions[1].ticker, "KXHIGHCHI-26APR23-B84.5");
    EXPECT_EQ(positions[1].position, 2);
    EXPECT_DOUBLE_EQ(positions[1].cost, 0.45);
    EXPECT_DOUBLE_EQ(positions[1].fees_paid, 0.04);
}

TEST(ParseMarketPositions, SkipsZeroPositions) {
    // Closed positions remain in the response with position_fp="0.00" —
    // dropping them on the parse side keeps the risk manager from seeing
    // stale "zombie" entries that would inflate active_market_count.
    auto body = nlohmann::json::parse(R"({
      "market_positions": [
        {"ticker": "OPEN", "position_fp": "3.00", "total_traded_dollars": "1.0"},
        {"ticker": "CLOSED", "position_fp": "0.00", "total_traded_dollars": "0.0"},
        {"ticker": "ALSO-CLOSED", "position_fp": "0", "total_traded_dollars": "0.5"}
      ]
    })");
    auto positions = KalshiRestClient::parse_market_positions(body);

    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].ticker, "OPEN");
}

TEST(ParseMarketPositions, SkipsEntriesMissingTicker) {
    auto body = nlohmann::json::parse(R"({
      "market_positions": [
        {"position_fp": "1.00", "total_traded_dollars": "0.5"},
        {"ticker": "", "position_fp": "1.00", "total_traded_dollars": "0.5"},
        {"ticker": "GOOD", "position_fp": "2.00", "total_traded_dollars": "1.0"}
      ]
    })");
    auto positions = KalshiRestClient::parse_market_positions(body);

    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].ticker, "GOOD");
    EXPECT_EQ(positions[0].position, 2);
}

TEST(ParseMarketPositions, HandlesNumericPositionField) {
    // Older / non-fractional markets may deliver `position` as a number
    // instead of `position_fp` as a string. Accept both.
    auto body = nlohmann::json::parse(R"({
      "market_positions": [
        {"ticker": "NUM", "position": 7, "total_traded_dollars": 1.5}
      ]
    })");
    auto positions = KalshiRestClient::parse_market_positions(body);

    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].position, 7);
    EXPECT_DOUBLE_EQ(positions[0].cost, 1.5);
}

TEST(ParseMarketPositions, HandlesMissingMoneyFields) {
    // A position with only shares and no cost-side fields shouldn't crash
    // or throw — zeros are the sensible default.
    auto body = nlohmann::json::parse(R"({
      "market_positions": [
        {"ticker": "BARE", "position_fp": "1.00"}
      ]
    })");
    auto positions = KalshiRestClient::parse_market_positions(body);

    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0].position, 1);
    EXPECT_DOUBLE_EQ(positions[0].cost, 0.0);
    EXPECT_DOUBLE_EQ(positions[0].fees_paid, 0.0);
}

TEST(ParseMarketPositions, EmptyBodyReturnsEmpty) {
    auto body = nlohmann::json::parse("{}");
    EXPECT_EQ(KalshiRestClient::parse_market_positions(body).size(), 0u);
}

TEST(ParseMarketPositions, NonArrayMarketPositionsReturnsEmpty) {
    auto body = nlohmann::json::parse(R"({"market_positions": "oops"})");
    EXPECT_EQ(KalshiRestClient::parse_market_positions(body).size(), 0u);
}

TEST(ParseMarketPositions, FractionalPositionTruncates) {
    // Fractional markets deliver e.g. "0.50" — our risk layer tracks int
    // shares, matching MarketFilter's skip-fractional gate. Truncate, don't
    // round, to avoid accidentally crediting extra shares we don't hold.
    auto body = nlohmann::json::parse(R"({
      "market_positions": [
        {"ticker": "FRAC", "position_fp": "0.75", "total_traded_dollars": "0.1"}
      ]
    })");
    auto positions = KalshiRestClient::parse_market_positions(body);

    // 0.75 truncates to 0, so the position is skipped entirely.
    EXPECT_EQ(positions.size(), 0u);
}

// ===== RiskManager::position_quantity =====

class PositionQuantityTest : public ::testing::Test {
protected:
    void SetUp() override {
        RiskConfig c;
        c.max_position_per_market = 10;
        c.max_total_exposure = 80.0;
        c.max_daily_loss = 15.0;
        c.kill_switch_loss = 30.0;
        c.cash_reserve_pct = 0.20;
        rm_ = std::make_unique<RiskManager>(c);
        rm_->set_balance(100.0);
    }
    std::unique_ptr<RiskManager> rm_;
};

TEST_F(PositionQuantityTest, UnknownTickerReturnsZero) {
    EXPECT_EQ(rm_->position_quantity("NEVER-SEEN"), 0);
}

TEST_F(PositionQuantityTest, ReflectsFillQuantity) {
    rm_->on_fill("MKT", "yes", 3, 0.40);
    EXPECT_EQ(rm_->position_quantity("MKT"), 3);
    rm_->on_fill("MKT", "yes", 2, 0.40);
    EXPECT_EQ(rm_->position_quantity("MKT"), 5);
}

TEST_F(PositionQuantityTest, SettledPositionReportsZero) {
    // After settlement the ticker still exists in the positions map, but
    // the pre-trade gate treats it as closed. position_quantity must agree
    // — otherwise the "order only the delta" fix in event_strategy.cpp
    // would see stale exposure and never re-enter a resolved market.
    rm_->on_fill("MKT", "yes", 5, 0.40);
    rm_->on_settlement("MKT", +1.0);
    EXPECT_EQ(rm_->position_quantity("MKT"), 0);
}

TEST_F(PositionQuantityTest, IsolatedPerTicker) {
    rm_->on_fill("A", "yes", 3, 0.50);
    rm_->on_fill("B", "yes", 7, 0.30);
    EXPECT_EQ(rm_->position_quantity("A"), 3);
    EXPECT_EQ(rm_->position_quantity("B"), 7);
    EXPECT_EQ(rm_->position_quantity("C"), 0);
}
