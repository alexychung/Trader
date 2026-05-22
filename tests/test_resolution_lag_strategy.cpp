#include "strategy/nba/resolution_lag_strategy.hpp"

#include "risk/risk_manager.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/nba/nba_score_feed.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

using namespace trader;
using namespace trader::nba;

// Same tmp-file pattern as test_nba_strategy.cpp.
class CalibrationFixture {
public:
    CalibrationFixture()
        : path_(std::filesystem::temp_directory_path() /
                "reslag_strategy_test_cal.json"),
          logger_(path_.string()) {
        if (std::filesystem::exists(path_)) std::filesystem::remove(path_);
    }
    ~CalibrationFixture() {
        if (std::filesystem::exists(path_)) std::filesystem::remove(path_);
    }
    ::trader::kalshi::CalibrationLogger& get() { return logger_; }

private:
    std::filesystem::path path_;
    ::trader::kalshi::CalibrationLogger logger_;
};

NbaGameSnapshot make_live_snap(const std::string& away, const std::string& home,
                                int period, int seconds_total_remaining,
                                int away_score, int home_score,
                                const std::string& date_iso = "2026-05-18") {
    NbaGameSnapshot s;
    s.valid = true;
    s.game_id = "0001";
    s.game_status = 2;
    s.game_status_text = "live";
    s.game_date_iso = date_iso;
    s.period = period;
    s.regulation_seconds_remaining = seconds_total_remaining;
    int per_period =
        std::max(0, seconds_total_remaining - (4 - period) * 720);
    s.game_clock_seconds = std::min(per_period, 720);
    s.away_tricode = away;
    s.home_tricode = home;
    s.away_score = away_score;
    s.home_score = home_score;
    return s;
}

NbaGameSnapshot make_final_snap(const std::string& away, const std::string& home,
                                 int away_score, int home_score,
                                 const std::string& date_iso = "2026-05-18") {
    NbaGameSnapshot s = make_live_snap(away, home, 4, 0, away_score, home_score,
                                        date_iso);
    s.game_status = 3;
    s.game_status_text = "Final";
    return s;
}

::trader::kalshi::KalshiMarket make_market(const std::string& ticker,
                                            double yes_bid, double yes_ask,
                                            int volume = 1000) {
    ::trader::kalshi::KalshiMarket m;
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

RiskConfig wide_risk_config() {
    RiskConfig r;
    r.max_position_per_market = 500;
    r.max_total_exposure = 1000.0;
    r.max_daily_loss = 200.0;
    r.kill_switch_loss = 500.0;
    return r;
}

class ResolutionLagTest : public ::testing::Test {
protected:
    CalibrationFixture cal_fixture;
    RiskManager risk{wide_risk_config()};
    ResolutionLagStrategy::Config cfg{};

    ResolutionLagStrategy make() {
        return ResolutionLagStrategy(risk, cal_fixture.get(), cfg);
    }

    void SetUp() override { risk.set_balance(500.0); }
};

} // namespace

TEST_F(ResolutionLagTest, FiresOnQ4BlowoutInsideEntryWindow) {
    // Q4, 1:00 left (60s ≤ 120s), home up 20 (≥ 15) → effectively decided.
    auto snap = make_live_snap("sas", "okc", 4, 60, 80, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    // YES=home ticker (OKC) with ask=0.97 — inside the 0.94–0.99 window.
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc-OKC", 0.95, 0.97)});

    auto signals = strat.generate_signals();
    ASSERT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].ticker, "KXNBAGAME-26may18sasokc-OKC");
    EXPECT_EQ(signals[0].contract_side, "yes");
    EXPECT_NEAR(signals[0].market_price, 0.97, 1e-9);
    EXPECT_GT(signals[0].quantity, 0);
}

TEST_F(ResolutionLagTest, FiresOnFinalStatus) {
    // Game over (status=3), home won 100-95. Kalshi book still at 0.96 ask
    // because settlement hasn't processed yet.
    auto snap = make_final_snap("sas", "okc", 95, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc-OKC", 0.94, 0.96)});

    auto signals = strat.generate_signals();
    ASSERT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].ticker, "KXNBAGAME-26may18sasokc-OKC");
}

TEST_F(ResolutionLagTest, SkipsWhenAskBelowMinEntryPrice) {
    // ask=0.92 below default min_entry_price 0.94 — the implied prob
    // suggests real comeback risk, refuse.
    auto snap = make_live_snap("sas", "okc", 4, 60, 80, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc-OKC", 0.90, 0.92)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(ResolutionLagTest, SkipsWhenAskAboveMaxEntryPrice) {
    // ask=0.995 above default max_entry_price 0.99 — paying 99.5¢ to win
    // $1 is not worth the tail. Refuse.
    auto snap = make_live_snap("sas", "okc", 4, 60, 80, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc-OKC", 0.99, 0.995)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(ResolutionLagTest, SkipsWhenLeadBelowCutoff) {
    // Q4 1:00 left but home only up 10 — below default cutoff_lead_points
    // of 15. The "effectively decided" rule doesn't apply.
    auto snap = make_live_snap("sas", "okc", 4, 60, 90, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc-OKC", 0.95, 0.97)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(ResolutionLagTest, SkipsWhenClockAboveCutoff) {
    // Q4 5:00 left, home up 20. Clock 300s > cutoff_clock_seconds 120 →
    // not "effectively decided" yet by the rule.
    auto snap = make_live_snap("sas", "okc", 4, 300, 80, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc-OKC", 0.95, 0.97)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(ResolutionLagTest, SkipsWhenAlreadyPositioned) {
    auto snap = make_live_snap("sas", "okc", 4, 60, 80, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    auto market = make_market("KXNBAGAME-26may18sasokc-OKC", 0.95, 0.97);
    strat.set_markets({market});
    risk.on_fill(market.ticker, "yes", 5, 0.97);
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(ResolutionLagTest, RespectsMaxPositionPerGameCap) {
    cfg.max_position_per_game_dollars = 10.0;
    auto snap = make_live_snap("sas", "okc", 4, 60, 80, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc-OKC", 0.95, 0.97)});

    auto signals = strat.generate_signals();
    ASSERT_EQ(signals.size(), 1);
    // qty = floor(10 / 0.97) = 10.
    EXPECT_EQ(signals[0].quantity, 10);
}

TEST_F(ResolutionLagTest, SkipsWhenQtyBelowMinLotSize) {
    cfg.max_position_per_game_dollars = 4.0;  // qty = floor(4/0.97) = 4 < min 5
    auto snap = make_live_snap("sas", "okc", 4, 60, 80, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc-OKC", 0.95, 0.97)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(ResolutionLagTest, SkipsLowVolumeMarket) {
    auto snap = make_live_snap("sas", "okc", 4, 60, 80, 100);
    auto strat = make();
    strat.set_snapshots({snap});
    auto stale = make_market("KXNBAGAME-26may18sasokc-OKC", 0.95, 0.97,
                              /*volume=*/5);
    strat.set_markets({stale});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(ResolutionLagTest, PicksWinningSideTicker) {
    // Away (SAS) leading 100-80. Strategy must pick the -SAS ticker,
    // not -OKC, regardless of which appears first in the cache iteration.
    auto snap = make_live_snap("sas", "okc", 4, 60, 100, 80);
    auto strat = make();
    strat.set_snapshots({snap});
    strat.set_markets({
        make_market("KXNBAGAME-26may18sasokc-OKC", 0.02, 0.05),
        make_market("KXNBAGAME-26may18sasokc-SAS", 0.95, 0.97),
    });

    auto signals = strat.generate_signals();
    ASSERT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].ticker, "KXNBAGAME-26may18sasokc-SAS");
}
