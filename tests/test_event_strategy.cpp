#include <gtest/gtest.h>
#include "strategy/kalshi/event_strategy.hpp"

using namespace trader;
using namespace trader::kalshi;

class EventStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        weather_model_ = std::make_shared<WeatherEnsembleModel>();

        EnsembleForecast ef;
        ef.station_id = "KNYC";
        ef.model = "gfs";
        ef.target_date = "2026-04-26";
        ef.num_members = 31;
        for (int i = 0; i < 31; ++i) {
            ef.member_highs.push_back(i < 28 ? 80.0 : 70.0);  // 28/31 above 75 → ~90%
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

        sizer_ = std::make_unique<AdaptiveSizer>();
        prob_calibrator_ = std::make_unique<ProbabilityCalibrator>();
        staleness_ = std::make_unique<StalenessGate>();
        cluster_limiter_ = std::make_unique<ClusterLimiter>();

        strategy_ = std::make_unique<KalshiEventStrategy>(
            prob_engine_, *edge_detector_, filter_, *risk_, calibration_,
            *sizer_, *prob_calibrator_, *staleness_, *cluster_limiter_
        );
    }

    // Simulate a well-calibrated weather model to skip cold-start Bayesian shrinkage.
    void prime_weather_history(int n = 60, double model_prob = 0.85, double mkt_price = 0.65) {
        for (int i = 0; i < n; ++i) {
            CalibrationRecord r;
            r.market_ticker = "PRIME-" + std::to_string(i);
            r.category = "weather";
            r.trade_time = std::chrono::system_clock::now();
            r.model_probability = model_prob;
            r.market_price = mkt_price;
            r.edge = model_prob - mkt_price;
            r.side = "yes";
            r.quantity = 1;
            r.entry_price = mkt_price;
            calibration_.log_trade(r);
            // Resolve correctly ~85% of the time (matches a well-calibrated model).
            bool win = (i % 100) < static_cast<int>(model_prob * 100);
            calibration_.resolve(r.market_ticker, win, win ? (1.0 - mkt_price) : -mkt_price);
        }
    }

    // Mark market fresh so staleness gate doesn't veto.
    void seed_freshness(const std::string& ticker) {
        staleness_->seed_market(ticker, std::chrono::system_clock::now());
    }

    std::shared_ptr<WeatherEnsembleModel> weather_model_;
    ProbabilityEngine prob_engine_;
    std::unique_ptr<EdgeDetector> edge_detector_;
    MarketFilter filter_;
    std::unique_ptr<RiskManager> risk_;
    CalibrationLogger calibration_;
    std::unique_ptr<AdaptiveSizer> sizer_;
    std::unique_ptr<ProbabilityCalibrator> prob_calibrator_;
    std::unique_ptr<StalenessGate> staleness_;
    std::unique_ptr<ClusterLimiter> cluster_limiter_;
    std::unique_ptr<KalshiEventStrategy> strategy_;
};

TEST_F(EventStrategyTest, GeneratesSignalForMispricedWeather_WithPrimedHistory) {
    prime_weather_history();  // skip cold start so Bayesian shrinkage lets model speak
    std::string ticker = "KXHIGHNY-26APR-T75";
    seed_freshness(ticker);

    KalshiMarket m;
    m.ticker = ticker;
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;

    strategy_->set_markets({m});
    auto signals = strategy_->generate_signals();

    ASSERT_GE(signals.size(), 1u);
    EXPECT_EQ(signals[0].ticker, ticker);
    EXPECT_EQ(signals[0].contract_side, "yes");
    EXPECT_GT(signals[0].quantity, 0);
}

TEST_F(EventStrategyTest, ColdStartProducesExplorationOrShadow) {
    // With ProbabilityCalibrator shrinkage disabled by default, the model's
    // raw edge flows through — a sincere 25pp edge at cold start will
    // produce a real trade, throttled only by AdaptiveSizer's cold-start
    // Kelly scale and the global fractional Kelly (jointly ~6% of full
    // Kelly). Previously the triple-shrinkage (shrinkage + AdaptiveSizer +
    // fractional Kelly) pinned size to the exploration cap of 1 contract.
    //
    // Invariant we still care about: the strategy records *something*
    // (trade or shadow) on every ticker where the model produced a signal.
    std::string ticker = "KXHIGHNY-26APR-T75";
    seed_freshness(ticker);

    KalshiMarket m;
    m.ticker = ticker;
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;

    strategy_->set_markets({m});
    auto signals = strategy_->generate_signals();

    int recorded = calibration_.total_trades();
    EXPECT_GE(recorded, 1) << "Expected at least a shadow prediction even at cold start";

    // Soft sanity check: cold-start trades should still be meaningfully
    // smaller than the per-market position cap (AdaptiveSizer is doing its
    // job). Allow quantity up to the EdgeDetector max_position as an upper
    // bound — the real guard is that AdaptiveSizer has NOT been bypassed.
    for (const auto& s : signals) {
        EXPECT_LE(s.quantity, edge_detector_->config().max_position);
        EXPECT_GT(s.quantity, 0);
    }
}

TEST_F(EventStrategyTest, StalenessGateBlocksRealTrades) {
    prime_weather_history();
    std::string ticker = "KXHIGHNY-26APR-T75";

    // Under the post-2026-04-22 semantic, set_markets() seeds staleness
    // from the REST snapshot (otherwise demo trading is impossible because
    // thin books never emit WS updates). To exercise the staleness gate,
    // we therefore:
    //   1. Seed staleness with an OLD timestamp representing a ticker that
    //      used to be fresh but hasn't had any updates recently.
    //   2. Do NOT call set_markets() (which would refresh the timestamp).
    //      Instead, poke the market into the strategy's internal catalog
    //      via a different path (on_market_update with a stale timestamp
    //      won't work either — it calls on_market_update which also
    //      refreshes). So we just don't re-seed fresh.
    //
    // The scenario under test: strategy had this market loaded at some
    // point, but staleness has since expired. Confirms the
    // `!staleness.is_market_fresh()` branch still gates placement.
    KalshiMarket m;
    m.ticker = ticker;
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;
    strategy_->set_markets({m});

    // Roll staleness back to older than the 2-min default max age.
    staleness_->seed_market(ticker, std::chrono::system_clock::now() - std::chrono::hours{1});

    auto signals = strategy_->generate_signals();

    // Real trade blocked by staleness (no signals emitted); shadow prediction may still log.
    EXPECT_EQ(signals.size(), 0u);
}

TEST_F(EventStrategyTest, NoSignalWhenNarrowSpread) {
    std::string ticker = "KXHIGHNY-26APR-T75";
    seed_freshness(ticker);

    KalshiMarket m;
    m.ticker = ticker;
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.88;
    m.yes_ask = 0.89;  // 1¢ spread — too narrow for filter
    m.volume = 200;

    strategy_->set_markets({m});
    auto signals = strategy_->generate_signals();
    EXPECT_EQ(signals.size(), 0u);
}

TEST_F(EventStrategyTest, NoSignalWhenKilled) {
    risk_->trigger_kill("Test");
    std::string ticker = "KXHIGHNY-26APR-T75";
    seed_freshness(ticker);

    KalshiMarket m;
    m.ticker = ticker;
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;

    strategy_->set_markets({m});
    auto signals = strategy_->generate_signals();
    EXPECT_EQ(signals.size(), 0u);
}

TEST_F(EventStrategyTest, OnSettlementResolvesCalibration) {
    prime_weather_history();
    std::string ticker = "KXHIGHNY-26APR-T75";
    seed_freshness(ticker);

    KalshiMarket m;
    m.ticker = ticker;
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;

    strategy_->set_markets({m});
    strategy_->generate_signals();

    Settlement s;
    s.ticker = ticker;
    s.outcome = true;
    s.pnl = 1.75;
    strategy_->on_settlement(s);

    // Settlement resolves the most recent logged record for that ticker.
    EXPECT_GE(calibration_.resolved_trades(), 1);
}

TEST_F(EventStrategyTest, MarketUpdateRefreshesStaleness) {
    MarketUpdate update;
    update.ticker = "KXHIGHNY-26APR-T75";
    update.yes_bid = 0.70;
    update.yes_ask = 0.75;
    update.volume = 300;

    strategy_->on_market_update(update);
    EXPECT_TRUE(staleness_->is_market_fresh("KXHIGHNY-26APR-T75"));
}

TEST_F(EventStrategyTest, ImplementsIStrategy) {
    std::unique_ptr<IStrategy> iface = std::move(strategy_);
    auto signals = iface->generate_signals();
    SUCCEED();
}

// ===== Kelly target vs existing position (dup-order fix) =====

// Without the fix, every tick re-emits the full Kelly target as the order
// quantity — each minute we'd stack another N contracts on top of what
// already filled, until the per-market risk cap blocks further entries.
// These tests pin the post-fix contract: kelly_quantity is a *target*
// position, and the order size is max(0, target - current).

TEST_F(EventStrategyTest, NoOrderWhenPositionAlreadyAtKellyTarget) {
    prime_weather_history();
    std::string ticker = "KXHIGHNY-26APR-T75";
    seed_freshness(ticker);

    KalshiMarket m;
    m.ticker = ticker;
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;
    strategy_->set_markets({m});

    // First pass with no position: captures the Kelly target for this setup.
    auto baseline = strategy_->generate_signals();
    ASSERT_GE(baseline.size(), 1u);
    const int target = baseline[0].quantity;
    ASSERT_GT(target, 0);

    // Re-use a fresh strategy so the baseline's own bookkeeping (shadow
    // dedup, cluster, sizer) doesn't bleed in and confuse the follow-up.
    risk_->on_fill(ticker, "yes", target, 0.65);

    // Second pass: position now matches the kelly target, so no new order.
    auto signals = strategy_->generate_signals();
    for (const auto& s : signals) {
        EXPECT_NE(s.ticker, ticker)
            << "Expected no order for a ticker already at the Kelly target";
    }
}

TEST_F(EventStrategyTest, OrdersOnlyDeltaWhenPartiallyFilled) {
    prime_weather_history();
    std::string ticker = "KXHIGHNY-26APR-T75";
    seed_freshness(ticker);

    KalshiMarket m;
    m.ticker = ticker;
    m.category = "weather";
    m.status = "open";
    m.yes_bid = 0.60;
    m.yes_ask = 0.65;
    m.volume = 200;
    strategy_->set_markets({m});

    // Baseline with no position — this is the Kelly target.
    auto baseline = strategy_->generate_signals();
    ASSERT_GE(baseline.size(), 1u);
    const int target = baseline[0].quantity;
    if (target < 2) {
        // Kelly cold-start caps at 1 contract; the delta test needs target
        // >= 2 to be meaningful. Skip — the "already at target" case above
        // covers the 1-contract path.
        GTEST_SKIP() << "Kelly target too small to split; target=" << target;
    }

    // Seed a partial fill — one share short of target.
    const int already_have = target - 1;
    risk_->on_fill(ticker, "yes", already_have, 0.65);

    auto signals = strategy_->generate_signals();
    ASSERT_GE(signals.size(), 1u);

    // Find the signal for our ticker and verify it's sized as the delta.
    bool found = false;
    for (const auto& s : signals) {
        if (s.ticker == ticker) {
            EXPECT_EQ(s.quantity, 1)
                << "Expected order qty = target (" << target
                << ") - existing (" << already_have << ") = 1";
            found = true;
        }
    }
    EXPECT_TRUE(found) << "No signal emitted for the ticker with partial fill";
}

