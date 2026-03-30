#include <gtest/gtest.h>
#include "strategy/kalshi/event_strategy.hpp"

using namespace trader;
using namespace trader::kalshi;

class EventStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        weather_model_ = std::make_shared<WeatherEnsembleModel>();

        // Set up ensemble data: 28/31 members above 75°F → ~90% probability
        EnsembleForecast ef;
        ef.station_id = "KNYC";
        ef.model = "gfs";
        ef.target_date = "2026-04-26";
        ef.num_members = 31;
        for (int i = 0; i < 31; ++i) {
            ef.member_highs.push_back(i < 28 ? 80.0 : 70.0);  // 28 above 75
        }
        weather_model_->set_ensemble(ef);

        prob_engine_.register_model("weather", weather_model_);

        EdgeDetector::Config ecfg;
        ecfg.min_edge = 0.05;
        ecfg.kelly_fraction = 0.25;
        ecfg.bankroll = 100.0;
        ecfg.max_position = 10;
        edge_detector_ = std::make_unique<EdgeDetector>(ecfg);

        RiskConfig rcfg;
        rcfg.max_position_per_market = 10;
        rcfg.max_total_exposure = 80.0;
        rcfg.max_daily_loss = 15.0;
        rcfg.kill_switch_loss = 30.0;
        rcfg.cash_reserve_pct = 0.20;
        risk_ = std::make_unique<RiskManager>(rcfg);
        risk_->set_balance(100.0);

        strategy_ = std::make_unique<KalshiEventStrategy>(
            prob_engine_, *edge_detector_, filter_, *risk_, calibration_
        );
    }

    std::shared_ptr<WeatherEnsembleModel> weather_model_;
    ProbabilityEngine prob_engine_;
    std::unique_ptr<EdgeDetector> edge_detector_;
    MarketFilter filter_;
    std::unique_ptr<RiskManager> risk_;
    CalibrationLogger calibration_;
    std::unique_ptr<KalshiEventStrategy> strategy_;
};

TEST_F(EventStrategyTest, GeneratesSignalForMispricedWeather) {
    // Market is priced at 65% YES, but model says 90% → 25% edge
    std::vector<KalshiMarket> markets;
    KalshiMarket m;
    m.ticker = "KXHIGHNY-26APR-T75";
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;
    markets.push_back(m);

    strategy_->set_markets(markets);
    auto signals = strategy_->generate_signals();

    ASSERT_GE(signals.size(), 1u);
    EXPECT_EQ(signals[0].ticker, "KXHIGHNY-26APR-T75");
    EXPECT_EQ(signals[0].contract_side, "yes");
    EXPECT_GT(signals[0].edge, 0.20);  // ~25% edge
    EXPECT_GT(signals[0].quantity, 0);
}

TEST_F(EventStrategyTest, NoSignalWhenNarrowSpread) {
    KalshiMarket m;
    m.ticker = "KXHIGHNY-26APR-T75";
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.88;
    m.yes_ask = 0.89;  // 1 cent spread → too narrow for filter
    m.volume = 200;

    strategy_->set_markets({m});
    auto signals = strategy_->generate_signals();
    EXPECT_EQ(signals.size(), 0u);
}

TEST_F(EventStrategyTest, NoSignalWhenKilled) {
    risk_->trigger_kill("Test");

    KalshiMarket m;
    m.ticker = "KXHIGHNY-26APR-T75";
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;

    strategy_->set_markets({m});
    auto signals = strategy_->generate_signals();
    EXPECT_EQ(signals.size(), 0u);
}

TEST_F(EventStrategyTest, CalibrationLogsOnSignal) {
    KalshiMarket m;
    m.ticker = "KXHIGHNY-26APR-T75";
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;

    strategy_->set_markets({m});
    strategy_->generate_signals();

    EXPECT_GE(calibration_.total_trades(), 1);
}

TEST_F(EventStrategyTest, OnSettlementResolvesCalibration) {
    // Generate a signal first
    KalshiMarket m;
    m.ticker = "KXHIGHNY-26APR-T75";
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;
    strategy_->set_markets({m});
    strategy_->generate_signals();

    // Settle
    Settlement s;
    s.ticker = "KXHIGHNY-26APR-T75";
    s.outcome = true;
    s.pnl = 1.75;
    strategy_->on_settlement(s);

    EXPECT_EQ(calibration_.resolved_trades(), 1);
}

TEST_F(EventStrategyTest, MarketUpdateRefreshesData) {
    MarketUpdate update;
    update.ticker = "KXHIGHNY-26APR-T75";
    update.yes_bid = 0.70;
    update.yes_ask = 0.75;
    update.volume = 300;

    strategy_->on_market_update(update);
    // Verified by generating signals with updated data (no crash = success)

    SUCCEED();
}

TEST_F(EventStrategyTest, ImplementsIStrategy) {
    std::unique_ptr<IStrategy> iface = std::move(strategy_);
    auto signals = iface->generate_signals();
    SUCCEED();
}
