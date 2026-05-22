#include "strategy/nba/nba_strategy.hpp"

#include "feeds/sharp_book_provider.hpp"
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

// Sharp-book provider that returns a fixed home_win_prob for every call.
// Used to exercise the CLV gate in isolation.
class FixedSharpProvider : public ::trader::feeds::ISharpBookProvider {
public:
    double home_win_prob = 0.5;
    bool has_fair = true;
    std::optional<::trader::feeds::SharpFair> get_fair(
        const std::string&, const std::string&, const std::string&) override {
        if (!has_fair) return std::nullopt;
        ::trader::feeds::SharpFair sf;
        sf.home_win_prob = home_win_prob;
        sf.confidence = 0.9;
        sf.source = "fixed-test";
        return sf;
    }
    std::string name() const override { return "fixed-test"; }
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
    // Home losing big: home_wp very low → on the YES=home (-OKC) ticker our
    // fair sits below the 0.50 mid (no positive edge). On the YES=away
    // (-SAS) ticker our fair (1 − home_wp) sits well above 0.52 → buy YES
    // SAS at the ask. This mirrors Kalshi's real two-ticker-per-game layout.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 95, 80)
    };
    auto okc_market = make_market("KXNBAGAME-26MAY18SASOKC-OKC", 0.48, 0.52);
    auto sas_market = make_market("KXNBAGAME-26MAY18SASOKC-SAS", 0.48, 0.52);

    auto strat = make();
    strat.set_markets({okc_market, sas_market});
    auto signals = strat.generate_signals();

    ASSERT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].ticker, "KXNBAGAME-26MAY18SASOKC-SAS");
    EXPECT_EQ(signals[0].contract_side, "yes");
    EXPECT_GT(signals[0].model_probability, 0.95);
    EXPECT_EQ(signals[0].market_price, 0.52);  // SAS yes_ask
}

TEST_F(NbaStrategyTest, ParsesSideSuffixHomeAndAway) {
    // Real Kalshi format: two tickers per game with team-tricode suffix. Home
    // up 15 in Q4 5:00 → home_wp ≈ 1.0. -OKC (YES=home) at 0.48/0.52 → big
    // positive edge, buy YES. -SAS (YES=away) at 0.48/0.52 → fair ≈ 0, no
    // signal. Exactly one signal expected.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 95)
    };
    auto okc_market = make_market("KXNBAGAME-26MAY18SASOKC-OKC", 0.48, 0.52);
    auto sas_market = make_market("KXNBAGAME-26MAY18SASOKC-SAS", 0.48, 0.52);

    auto strat = make();
    strat.set_markets({okc_market, sas_market});
    auto signals = strat.generate_signals();

    ASSERT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].ticker, "KXNBAGAME-26MAY18SASOKC-OKC");
    EXPECT_EQ(signals[0].contract_side, "yes");
    EXPECT_GT(signals[0].model_probability, 0.95);
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

TEST_F(NbaStrategyTest, HighConfidenceGate_SkipsTightScoreGap) {
    // Q4, 5:00 left, home up only 3 (below default min_abs_score_diff=8).
    // Even though the arcsine wp may favor home, the gate should drop the
    // signal entirely.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 90, 93)
    };
    auto market = make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52);
    auto strat = make();
    strat.set_markets({market});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, HighConfidenceGate_SkipsUncertainWp) {
    // Long time left + small lead → wp lands inside the uncertainty band
    // [0.30, 0.70]. Bot should refuse to trade even with sufficient score
    // gap, because the arcsine model is unreliable in that range.
    cfg.min_abs_score_diff = 4;   // relax score gate so wp gate is the binding one
    // Q3 start (t=1440), home up 4 → wp ≈ 0.61 (in band)
    feed.snapshots = {
        make_live_snap("sas", "okc", 3, 1440, 54, 58)
    };
    auto market = make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52);
    auto strat = make();
    strat.set_markets({market});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, HighConfidenceGate_FiresOnStrongLeadOutsideBand) {
    // Q4 5:00, home up 15. wp very near 1.0 (outside [0.30, 0.70]) AND
    // score gap 15 (above 8) → both gates pass.
    feed.snapshots = {
        make_live_snap("sas", "okc", 4, 300, 80, 95)
    };
    auto market = make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52);
    auto strat = make();
    strat.set_markets({market});
    auto signals = strat.generate_signals();
    EXPECT_EQ(signals.size(), 1);
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

// ===== CLV gate (#A3) =====

TEST_F(NbaStrategyTest, ClvGate_NoProvider_IsNoOp) {
    // Default setup uses no sharp provider — the gate should never fire.
    // The setup already covers this since FiresOnBigEdgeLateGame works
    // without a sharp provider. This test makes the no-op explicit.
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 95)};
    auto strat = make();
    // Note: not setting sharp_book_provider.
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});
    EXPECT_EQ(strat.generate_signals().size(), 1);
}

TEST_F(NbaStrategyTest, ClvGate_NullopFair_IsNoOp) {
    // Provider is wired but returns nullopt → gate still no-op.
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 95)};
    FixedSharpProvider sharp;
    sharp.has_fair = false;
    auto strat = make();
    strat.set_sharp_book_provider(&sharp);
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});
    EXPECT_EQ(strat.generate_signals().size(), 1);
}

TEST_F(NbaStrategyTest, ClvGate_DirectionMismatch_Skips) {
    // Our model says home wins (lead +15, wp≈1.0). Sharp says home loses
    // (home_win_prob=0.10 → away favored). Direction mismatch → skip
    // regardless of edge.
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 95)};
    FixedSharpProvider sharp;
    sharp.home_win_prob = 0.10;  // sharp: home is heavy dog
    auto strat = make();
    strat.set_sharp_book_provider(&sharp);
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, ClvGate_KalshiNotCheaperThanSharp_Skips) {
    // Sharp agrees: home is favored (sharp_wp=0.55). But Kalshi mid is
    // 0.50, only 5¢ below sharp. min_clv_edge default is 0.02 — wait,
    // 0.55 - 0.50 = 0.05 ≥ 0.02 → that WOULD fire. Tighten so the gate
    // is the binding constraint: bump min_clv_edge to 0.10 in cfg and
    // confirm we now skip.
    cfg.min_clv_edge = 0.10;
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 95)};
    FixedSharpProvider sharp;
    sharp.home_win_prob = 0.55;  // sharp agrees home is slight favorite
    auto strat = make();
    strat.set_sharp_book_provider(&sharp);
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, ClvGate_KalshiCheaperThanSharp_Fires) {
    // Sharp says home_wp = 0.95 (very high). Kalshi mid is 0.50. Deviation
    // = 0.95 - 0.50 = 0.45 ≥ default 0.02. Our model also agrees (lead+15
    // ⇒ wp ≈ 1.0). All gates pass → fires.
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 95)};
    FixedSharpProvider sharp;
    sharp.home_win_prob = 0.95;
    auto strat = make();
    strat.set_sharp_book_provider(&sharp);
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52)});
    EXPECT_EQ(strat.generate_signals().size(), 1);
}

// ===== Fee-aware edge gate (#A1+A2) =====

TEST_F(NbaStrategyTest, FeeAwareEdge_SkipsWhenAskEatsEdge) {
    // home up 10 in Q4 5:00 → wp ≈ 0.94. Market bid=0.85, ask=0.93
    // (mid=0.89, spread=8¢ < max 10¢).
    //   edge_above_mid = 0.94 - 0.89 = 0.05  (would have fired under old 4¢)
    //   edge_after_costs = 0.94 - 0.93 - ~0.002 ≈ 0.008 < new 2¢ → skip.
    // Confirms the new gate refuses trades the old gate would have taken.
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 90)};
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.85, 0.93)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, FeeAwareEdge_FiresWhenAskHasEdge) {
    // home up 15 in Q4 5:00, wp ≈ 1.0. Ask 0.85.
    // edge_after_costs ≈ 1.0 - 0.85 - small_fee = ~0.14 > 0.02 → fire.
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 95)};
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.80, 0.85)});
    auto signals = strat.generate_signals();
    ASSERT_EQ(signals.size(), 1);
    EXPECT_EQ(signals[0].market_price, 0.85);
}

// ===== Volume filter (#B4) =====

TEST_F(NbaStrategyTest, VolumeFilter_SkipsLowVolumeBook) {
    // Default min_market_volume is 100. Set volume to 10 → skip.
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 95)};
    auto strat = make();
    auto low_vol = make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52,
                                /*volume=*/10);
    strat.set_markets({low_vol});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, VolumeFilter_FiresAtBoundary) {
    // Volume == min_market_volume should NOT skip (gate is < not <=).
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 95)};
    auto strat = make();
    auto exactly_at = make_market("KXNBAGAME-26may18sasokc", 0.48, 0.52,
                                   /*volume=*/100);
    strat.set_markets({exactly_at});
    EXPECT_EQ(strat.generate_signals().size(), 1);
}

// ===== Pregame spread adjustment (#B2) =====

TEST_F(NbaStrategyTest, PregameSpread_HeavyHomeFavorite_KillsSmallLeadEdge) {
    // Home is a 12-point favorite pregame. With Q4 5:00 left (75% of game
    // elapsed) and home up only 5, the adjusted_lead = 5 - 12*0.896 ≈ -5.7.
    // That flips the directional signal — home is UNDERperforming the line,
    // not overperforming. wp goes from ~0.81 (raw) to ~0.30 (adjusted).
    // With market mid at 0.78 the raw edge would be ~+0.03; the adjusted
    // edge is ~-0.48. Strategy skips on either:
    //   (a) the high-confidence gate (wp now ∈ [0.30, 0.70] band)
    //   (b) the fee-aware edge gate (negative after costs)
    cfg.default_pregame_spread = 12.0;  // home favored by 12
    feed.snapshots = {make_live_snap("sas", "okc", 4, 300, 80, 85)};
    auto strat = make();
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.76, 0.80)});
    EXPECT_TRUE(strat.generate_signals().empty());
}

TEST_F(NbaStrategyTest, PregameSpread_PerGameOverridesDefault) {
    // Strategy default spread = 0, but per-game spread says home is a
    // 10-point dog (negative spread). With home up 5 in Q4 5:00, adjusted
    // lead = 5 - (-10)*0.896 = 5 + 8.96 ≈ 14. Now wp is much higher than
    // the raw-lead computation would give, so a market at mid=0.55 looks
    // strongly mispriced.
    cfg.default_pregame_spread = 0.0;
    auto snap = make_live_snap("sas", "okc", 4, 300, 80, 85);
    snap.pregame_spread = -10.0;  // home is 10-point dog pregame
    feed.snapshots = {snap};
    auto strat = make();
    // Use a market mid where the raw-lead model would NOT fire but the
    // adjusted model does. Raw wp(+5, 300) ≈ 0.81 → fair_yes=0.81,
    // ask=0.80, edge_after_costs ~= 0.005 → skip. Adjusted wp(+14, 300)
    // ≈ 0.995 → fair_yes=0.995, ask=0.80, edge_after_costs ~= 0.19 → fire.
    strat.set_markets({make_market("KXNBAGAME-26may18sasokc", 0.78, 0.80)});
    auto signals = strat.generate_signals();
    EXPECT_EQ(signals.size(), 1);
    if (!signals.empty()) {
        EXPECT_GT(signals[0].model_probability, 0.95);
    }
}
