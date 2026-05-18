#include "strategy/nba/nba_strategy.hpp"

#include "risk/risk_manager.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/nba/kalshi_nba_parser.hpp"
#include "strategy/nba/nba_score_feed.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

using namespace trader;
using namespace trader::nba;

// Test double — returns a canned set of snapshots, no HTTP.
class MockScoreFeed : public NbaScoreFeed {
public:
    std::vector<NbaGameSnapshot> snapshots;
    int fetch_count = 0;
    std::vector<NbaGameSnapshot> fetch_today_scoreboard() override {
        ++fetch_count;
        return snapshots;
    }
};

// Lightweight CalibrationLogger fixture: routed to a tmp file so log_trade
// doesn't accidentally clobber production data/calibration.json.
class CalibrationFixture {
public:
    CalibrationFixture()
        : path_(std::filesystem::temp_directory_path() /
                "nba_strategy_test_cal.json"),
          logger_(path_.string()) {
        if (std::filesystem::exists(path_)) std::filesystem::remove(path_);
    }
    ~CalibrationFixture() {
        if (std::filesystem::exists(path_)) std::filesystem::remove(path_);
    }
    CalibrationLogger& get() { return logger_; }

private:
    std::filesystem::path path_;
    CalibrationLogger logger_;
};

NbaGameSnapshot make_live_snap(const std::string& away, const std::string& home,
                                int period, int seconds_remaining_total,
                                int away_score, int home_score,
                                const std::string& date_iso = "2026-05-18") {
    NbaGameSnapshot s;
    s.valid = true;
    s.game_id = "0001";
    s.game_status = 2;
    s.game_status_text = "live";
    s.game_date_iso = date_iso;
    s.period = period;
    s.regulation_seconds_remaining = seconds_remaining_total;
    // Reconstruct per-period clock so the test data is internally consistent.
    int per_period = std::max(0, seconds_remaining_total - (4 - period) * 720);
    s.game_clock_seconds = std::min(per_period, 720);
    s.away_tricode = away;
    s.home_tricode = home;
    s.away_score = away_score;
    s.home_score = home_score;
    return s;
}

KalshiMarket make_market(const std::string& ticker,
                          double yes_bid, double yes_ask,
                          int volume = 1000) {
    KalshiMarket m;
    m.ticker = ticker;
    m.title = "NBA game";
    m.category = "Sports";
    m.status = "open";
    m.yes_bid = yes_bid;
    m.yes_ask = yes_ask;
    m.last_price = (yes_bid + yes_ask) * 0.5;
    m.volume = volume;
    m.open_interest = volume;
    return m;
}

// NBA-scaled risk config — the global default (max_position_per_market=10)
// was tuned for $100 weather; sports markets need more headroom. Production
// callers override RiskConfig in config.yaml to match the NBA strategy's
// per-game cap; tests do the same.
RiskConfig nba_risk_config() {
    RiskConfig r;
    r.max_position_per_market = 200;   // ~$100/market at $0.50 price
    r.max_total_exposure = 500.0;
    r.max_daily_loss = 100.0;
    r.kill_switch_loss = 200.0;
    return r;
}

class NbaStrategyTest : public ::testing::Test {
protected:
    MockScoreFeed feed;
    CalibrationFixture cal_fixture;
    RiskManager risk{nba_risk_config()};

    NbaStrategy::Config cfg{};

    NbaStrategy make() {
        return NbaStrategy(risk, cal_fixture.get(), feed, cfg);
    }

    void SetUp() override {
        risk.set_balance(700.0);
    }
};

} // namespace

TEST_F(NbaStrategyTest, FiresOnBigEdgeLateGame) {
    // Q4, 5:00 remaining (t=300s), home up 15. Arcsine WP at this state is
    // very near 1.0. If Kalshi YES = 0.50 mid, edge is ~+0.45 → buy YES.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 95)
    };
    auto market = make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52);

    auto strat = make();
    strat.set_markets({market});
    auto signals = strat.generate_signals();

    ASSERT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].ticker, "KXNBAGAME-26may18sasokc");
    EXPECT_EQ(signals[0].contract_side, "yes");
    EXPECT_GT(signals[0].model_probability, 0.95);
    EXPECT_GT(signals[0].quantity, 0);
    EXPECT_EQ(signals[0].market_price, 0.52);  // ask
}

TEST_F(NbaStrategyTest, FiresOnAwayMispricing) {
    // Home losing big: home_wp very low → market mid 0.50 is overpriced for home
    // → buy NO. NO ask = 1 - yes_bid = 1 - 0.48 = 0.52.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 95, 80)
    };
    auto market = make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52);

    auto strat = make();
    strat.set_markets({market});
    auto signals = strat.generate_signals();

    ASSERT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].contract_side, "no");
    EXPECT_GT(signals[0].model_probability, 0.95);
    EXPECT_EQ(signals[0].market_price, 0.52);  // 1 - 0.48
}

TEST_F(NbaStrategyTest, NoSignalBelowEdgeThreshold) {
    // Q4 12:00 (t=720), home up 5. WP ≈ 0.70.
    // Market mid at 0.69 → edge ≈ 0.01 < default 0.04 threshold.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 720, 60, 65)
    };
    auto market = make_market("KXNBAGAME-26may18sasokc", 0.685, 0.695);
    auto strat = make();
    strat.set_markets({market});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, SkipsPreGame) {
    // game_status 1 → not live; should NOT fire even if a Kalshi market exists.
    NbaGameSnapshot s = make_live_snap("sas", "okc", 0, 0, 0, 0);
    s.game_status = 1;
    feed.snapshots = {s};
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, SkipsFinal) {
    NbaGameSnapshot s = make_live_snap("sas", "okc", 4, 0, 95, 100);
    s.game_status = 3;
    feed.snapshots = {s};
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, SkipsBeforeMinSecondsRemaining) {
    // Q4 0:45 remaining → below default 60s floor.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 45, 80, 100)
    };
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.10, 0.15)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, SkipsBeforeMaxSecondsRemaining) {
    // Q1 12:00 → t = 2880, > default 1440 cap.
    feed.snapshots = {
        make_live_snap("sas", "okc", 1, 2880, 0, 15)  // big early lead
    };
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.50, 0.55)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, SkipsWhenNoKalshiMarket) {
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 95)
    };
    // No KalshiMarket registered for this game.
    auto strat = make();
    strat.set_markets({});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, SkipsPlaceholderBook) {
    // yes_ask = 1.0 is the canonical placeholder — no real liquidity.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 95)
    };
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.01, 1.00)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, SkipsWideSpread) {
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 95)
    };
    auto strat = make();
    // 30¢ spread > 10¢ default cap.
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.30, 0.60)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, SkipsWhenExistingPosition) {
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 95)
    };
    auto market = make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52);
    auto strat = make();
    strat.set_markets({market});

    // Synthesize a prior fill to populate position_quantity.
    risk.on_fill(market.ticker, "yes", 5, 0.50);

    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, RespectsScoreboardPollInterval) {
    cfg.scoreboard_poll_interval = std::chrono::seconds(60);
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 95)
    };
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});

    EXPECT_TRUE(strat.refresh_scoreboard_if_due());   // initial fetch
    EXPECT_FALSE(strat.refresh_scoreboard_if_due());  // rate-limited
    EXPECT_FALSE(strat.refresh_scoreboard_if_due());
    EXPECT_EQ(feed.fetch_count, 1);
}

TEST_F(NbaStrategyTest, CapsPositionByMaxDollarsPerGame) {
    cfg.max_position_per_game_dollars = 5.0;  // very tight cap
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 100)
    };
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});

    auto signals = strat.generate_signals();
    ASSERT_EQ(signals.size(), 1);
    // qty * target_price ≤ 5 → qty ≤ floor(5 / 0.52) = 9
    EXPECT_LE(signals[0].quantity * signals[0].market_price, 5.0 + 1e-6);
    EXPECT_GE(signals[0].quantity, 1);
}
