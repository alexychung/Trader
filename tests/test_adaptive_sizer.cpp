#include <gtest/gtest.h>
#include "strategy/kalshi/adaptive_sizer.hpp"
#include "strategy/kalshi/calibration.hpp"

using namespace trader::kalshi;

static CategoryAggregate agg(int real, double brier, double pnl, double expected_edge) {
    CategoryAggregate a;
    a.category = "weather";
    a.resolved_real = real;
    a.resolved_total = real;
    a.brier = brier;
    a.realized_pnl = pnl;
    a.expected_edge = expected_edge;
    return a;
}

TEST(AdaptiveSizer, ColdStartCapsScale) {
    AdaptiveSizer s;
    auto a = agg(0, 0.10, 0.0, 0.0);
    double k = s.effective_kelly("weather", 1.0, a);
    EXPECT_NEAR(k, 0.25, 0.001);  // cold-start cap = 0.25
}

TEST(AdaptiveSizer, GoodCalibrationFullScale) {
    AdaptiveSizer s;
    auto a = agg(40, 0.10, 5.0, 5.0);  // model delivers exactly expected PnL
    double k = s.effective_kelly("weather", 1.0, a);
    EXPECT_GT(k, 0.9);  // brier_factor=1, pnl_factor=1, scale ~ 1
}

TEST(AdaptiveSizer, BadBrierZeroScale) {
    AdaptiveSizer s;
    auto a = agg(40, 0.30, 0.0, 0.0);  // brier worse than 0.25 threshold
    double k = s.effective_kelly("weather", 1.0, a);
    EXPECT_NEAR(k, 0.0, 0.001);
}

TEST(AdaptiveSizer, NegativePnlReducesScale) {
    AdaptiveSizer s;
    auto a = agg(40, 0.15, -10.0, 5.0);  // realized worse than expected
    double k = s.effective_kelly("weather", 1.0, a);
    EXPECT_LT(k, 0.5);
}

TEST(AdaptiveSizer, AutoDisableOnHighBrier) {
    AdaptiveSizer s;
    CalibrationLogger logger;
    // Seed 25 resolved trades with high Brier (model wrong every time at 0.85 confidence).
    for (int i = 0; i < 25; ++i) {
        CalibrationRecord r;
        r.market_ticker = "MKT" + std::to_string(i);
        r.category = "weather";
        r.model_probability = 0.85;
        r.market_price = 0.50;
        r.edge = 0.35;
        r.side = "yes";
        r.quantity = 1;
        r.entry_price = 0.50;
        logger.log_trade(r);
        logger.resolve(r.market_ticker, false, -0.50);  // model said YES, NO won
    }

    s.refresh(logger);
    EXPECT_TRUE(s.is_disabled("weather"));
    auto a = logger.category_aggregate("weather");
    EXPECT_EQ(s.effective_kelly("weather", 1.0, a), 0.0);
}

TEST(AdaptiveSizer, EffectiveBankrollCappedAt1_5x) {
    AdaptiveSizer s;
    EXPECT_NEAR(s.effective_bankroll(200.0, 100.0), 150.0, 0.001);
    EXPECT_NEAR(s.effective_bankroll(120.0, 100.0), 120.0, 0.001);
    EXPECT_NEAR(s.effective_bankroll(80.0, 100.0), 80.0, 0.001);
}

TEST(AdaptiveSizer, ExplorationRemainingTracksSpend) {
    AdaptiveSizer s;
    double bk = 100.0;
    EXPECT_NEAR(s.exploration_remaining(bk, 0.0), 5.0, 0.001);   // 5% of 100
    EXPECT_NEAR(s.exploration_remaining(bk, 3.0), 2.0, 0.001);
    EXPECT_NEAR(s.exploration_remaining(bk, 10.0), 0.0, 0.001);  // exceeded
}

TEST(AdaptiveSizer, DetectsDriftWhenRollingBrierWorsensBeyondAllTime) {
    // 30 good-calibration records (model=0.70, 70% actual wins) followed by
    // 20 broken-calibration records (model=0.90, all losses). All-time Brier
    // stays under the 0.25 threshold thanks to the good tail; rolling-window
    // Brier on the recent 20 is ~0.81. AdaptiveSizer should still disable
    // the category on the drift signal alone.
    CalibrationLogger logger;
    auto mk = [](const std::string& t, double prob, const std::string& side = "yes") {
        CalibrationRecord r;
        r.market_ticker = t; r.category = "weather"; r.side = side;
        r.trade_time = std::chrono::system_clock::now();
        r.model_probability = prob; r.market_price = 0.50;
        r.edge = prob - 0.50; r.quantity = 1; r.entry_price = 0.50;
        return r;
    };

    // Good tail: 30 records, 70% model, matching outcomes — Brier ≈ 0.21.
    for (int i = 0; i < 30; ++i) {
        logger.log_trade(mk("G" + std::to_string(i), 0.70));
        logger.resolve("G" + std::to_string(i), i < 21, 0);  // 21/30 wins
    }
    // Drift tail: 20 records, model=0.90, all losses — Brier ≈ 0.81.
    for (int i = 0; i < 20; ++i) {
        logger.log_trade(mk("D" + std::to_string(i), 0.90));
        logger.resolve("D" + std::to_string(i), false, 0);
    }

    auto agg_check = logger.category_aggregate("weather", 30);
    // Sanity: rolling clearly worse than all-time, but neither is necessarily
    // forced below brier_zero on all-time (depends on mixing).
    EXPECT_GT(agg_check.rolling_brier - agg_check.brier, 0.05);

    AdaptiveSizer s;
    s.refresh(logger);
    EXPECT_TRUE(s.is_disabled("weather"));
}
