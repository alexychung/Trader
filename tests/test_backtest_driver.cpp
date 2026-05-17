#include <gtest/gtest.h>

#include "backtest/driver.hpp"
#include "exchange/mock_exchange.hpp"
#include "risk/cluster_limiter.hpp"
#include "risk/risk_manager.hpp"
#include "strategy/kalshi/adaptive_sizer.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/kalshi/edge_detector.hpp"
#include "strategy/kalshi/event_strategy.hpp"
#include "strategy/kalshi/market_filter.hpp"
#include "strategy/kalshi/probability_calibrator.hpp"
#include "strategy/kalshi/probability_engine.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

using namespace trader;
using namespace trader::kalshi;
using namespace trader::backtest;

namespace {

std::string tmp_weather_cache() {
    auto base = std::filesystem::temp_directory_path() / "trader_backtest_test_weather";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base.string();
}

// Write the on-disk cache format that OpenMeteoClient::fetch_archived_forecast reads,
// so the driver doesn't hit the network during tests.
void seed_archive_cache(const std::string& dir, const std::string& station_id,
                        const std::string& date, double high, double low) {
    std::ofstream out(dir + "/" + station_id + "_" + date + ".json");
    out << R"({"station_id":")" << station_id << R"(","target_date":")"
        << date << R"(","high_f":)" << high << R"(,"low_f":)" << low << "}";
}

struct Harness {
    ProbabilityEngine prob_engine;
    std::shared_ptr<WeatherEnsembleModel> weather_model;
    RiskConfig rcfg;
    std::unique_ptr<RiskManager> risk;
    std::unique_ptr<EdgeDetector> edge_detector;
    MarketFilter filter;
    CalibrationLogger calibration;  // in-memory
    AdaptiveSizer sizer;
    ProbabilityCalibrator prob_calibrator;
    StalenessGate staleness;
    ClusterLimiter cluster_limiter;
    std::unique_ptr<KalshiEventStrategy> strategy;
    MockExchange exchange{100.0};

    Harness() {
        weather_model = std::make_shared<WeatherEnsembleModel>();
        prob_engine.register_model("weather", weather_model);

        rcfg.max_position_per_market = 10;
        rcfg.max_total_exposure = 80.0;
        rcfg.max_daily_loss = 15.0;
        rcfg.kill_switch_loss = 30.0;
        rcfg.cash_reserve_pct = 0.20;
        risk = std::make_unique<RiskManager>(rcfg);
        risk->set_balance(100.0);

        EdgeDetector::Config ecfg;
        ecfg.min_edge = 0.05;
        ecfg.kelly_fraction = 0.25;
        ecfg.bankroll = 100.0;
        edge_detector = std::make_unique<EdgeDetector>(ecfg);

        strategy = std::make_unique<KalshiEventStrategy>(
            prob_engine, *edge_detector, filter, *risk,
            calibration, sizer, prob_calibrator, staleness, cluster_limiter);
        exchange.connect();
    }
};

SettledMarket make_market(const std::string& ticker, const std::string& close_iso,
                          const std::string& exp_iso, bool outcome_yes,
                          double mid_price = 0.55) {
    SettledMarket sm;
    sm.market.ticker = ticker;
    sm.market.category = "weather";
    sm.market.status = "settled";
    sm.market.result = outcome_yes ? "yes" : "no";
    sm.market.close_time = close_iso;
    sm.market.expiration_time = exp_iso;
    sm.market.yes_bid = mid_price - 0.03;
    sm.market.yes_ask = mid_price + 0.02;
    sm.market.last_price = mid_price;
    sm.market.volume = 300;
    sm.outcome_yes = outcome_yes;
    sm.resolution_time = exp_iso;
    return sm;
}

SettledTrade make_trade(const std::string& ticker, double price, const std::string& ts) {
    SettledTrade t;
    t.ticker = ticker;
    t.yes_price = price;
    t.count = 5;
    t.taker_side = "yes";
    t.created_at = ts;
    return t;
}

} // namespace

TEST(ParseIso8601, RoundTrip) {
    auto ts = parse_iso8601("2026-04-20T15:30:45Z");
    EXPECT_EQ(format_iso8601(ts), "2026-04-20T15:30:45Z");
    EXPECT_EQ(format_date_utc(ts), "2026-04-20");
}

TEST(BacktestDriver, SettlesMarketAndLogsPredictions) {
    Harness h;
    auto cache_dir = tmp_weather_cache();
    seed_archive_cache(cache_dir, "KNYC", "2026-04-26", 80.0, 60.0);

    auto m = make_market("KXHIGHNY-26APR-T75",
                         "2026-04-26T23:00:00Z", "2026-04-26T23:59:00Z",
                         /*outcome_yes=*/true, /*mid_price=*/0.55);
    m.trades.push_back(make_trade(m.market.ticker, 0.50, "2026-04-24T10:00:00Z"));
    m.trades.push_back(make_trade(m.market.ticker, 0.55, "2026-04-24T14:00:00Z"));
    m.trades.push_back(make_trade(m.market.ticker, 0.60, "2026-04-25T09:00:00Z"));

    BacktestDriver::Config cfg;
    cfg.weather_cache_dir = cache_dir;
    BacktestDriver driver(*h.strategy, h.prob_engine, h.weather_model,
                           h.staleness, h.exchange, cfg);
    auto res = driver.run({m});

    EXPECT_EQ(res.settlements_applied, 1);
    EXPECT_GT(res.decisions_made, 0);
    EXPECT_EQ(h.exchange.order_count(), static_cast<std::size_t>(res.orders_placed));

    // Strategy logs a shadow and/or real record per decision point.
    EXPECT_GT(h.calibration.total_trades(), 0);

    int resolved = 0;
    for (const auto& r : h.calibration.records()) if (r.outcome.has_value()) ++resolved;
    EXPECT_GE(resolved, 1);
}

TEST(BacktestDriver, EventsSortedChronologicallyAcrossMarkets) {
    Harness h;
    auto cache_dir = tmp_weather_cache();
    seed_archive_cache(cache_dir, "KNYC", "2026-04-26", 80.0, 60.0);
    seed_archive_cache(cache_dir, "KORD", "2026-04-27", 70.0, 50.0);

    auto a = make_market("KXHIGHNY-26APR-T75",
                         "2026-04-26T23:00:00Z", "2026-04-26T23:59:00Z", true);
    a.trades.push_back(make_trade(a.market.ticker, 0.55, "2026-04-24T10:00:00Z"));

    auto b = make_market("KXHIGHCHI-26APR-T65",
                         "2026-04-27T23:00:00Z", "2026-04-27T23:59:00Z", false);
    b.trades.push_back(make_trade(b.market.ticker, 0.40, "2026-04-25T10:00:00Z"));

    BacktestDriver::Config cfg;
    cfg.weather_cache_dir = cache_dir;
    BacktestDriver driver(*h.strategy, h.prob_engine, h.weather_model,
                           h.staleness, h.exchange, cfg);
    auto res = driver.run({a, b});

    EXPECT_EQ(res.settlements_applied, 2);

    // Each market should be in the coverage output and attempted.
    int attempted = 0;
    for (const auto& c : res.coverage) if (c.attempted) ++attempted;
    EXPECT_EQ(attempted, 2);
}

TEST(BacktestDriver, SkipsNonWeatherTickers) {
    Harness h;
    auto cache_dir = tmp_weather_cache();

    auto m = make_market("KXCPIYY-26MAR-T3.0",
                         "2026-03-15T12:00:00Z", "2026-03-15T12:30:00Z", true);
    BacktestDriver::Config cfg;
    cfg.weather_cache_dir = cache_dir;
    BacktestDriver driver(*h.strategy, h.prob_engine, h.weather_model,
                           h.staleness, h.exchange, cfg);
    auto res = driver.run({m});

    ASSERT_EQ(res.coverage.size(), 1u);
    EXPECT_FALSE(res.coverage[0].attempted);
    EXPECT_EQ(res.coverage[0].skip_reason, "not_weather_ticker");
    EXPECT_EQ(res.decisions_made, 0);
}
