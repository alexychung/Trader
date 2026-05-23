#include <gtest/gtest.h>

#include "backtest/replay_engine.hpp"

using trader::backtest::BacktestReplay;
using trader::backtest::candle_window_for;
using trader::backtest::CandleWindowBounds;
using trader::backtest::IKalshiPriceProvider;
using trader::backtest::pbp_wall_clock_ts_sec;
using trader::backtest::PbpEvent;
using trader::backtest::PbpGameSummary;
using trader::backtest::ReplayConfig;

namespace {

PbpGameSummary make_game(const std::string& date_iso) {
    PbpGameSummary g;
    g.game_id = "0022501234";
    g.game_date_iso = date_iso;
    g.home_tricode = "OKC";
    g.away_tricode = "SAS";
    g.game_status = 3;
    return g;
}

PbpEvent make_event(int regulation_seconds_remaining) {
    PbpEvent e;
    e.regulation_seconds_remaining = regulation_seconds_remaining;
    return e;
}

} // namespace

// =========================================================================
// hotfix-bugs task #1: every PBP event's wall_clock must land INSIDE the
// candle provider's fetch window. Otherwise IKalshiPriceProvider::get_quote
// falls back to candles.begin() and the strategy sees a static price for
// the entire game.
// =========================================================================

TEST(ReplayAlignment, WallClockForTipoffIsInsideCandleWindow) {
    auto game = make_game("2026-04-01");
    auto tipoff = make_event(2880);  // 0 elapsed, regulation starting
    auto bounds = candle_window_for(game.game_date_iso);
    auto ts = pbp_wall_clock_ts_sec(game, tipoff);

    EXPECT_GE(ts, bounds.start_ts_sec)
        << "Tipoff wall_clock " << ts << " is BEFORE candle window start "
        << bounds.start_ts_sec
        << " — get_quote will always return the first (pre-tipoff) bar.";
    EXPECT_LE(ts, bounds.end_ts_sec)
        << "Tipoff wall_clock " << ts << " is AFTER candle window end "
        << bounds.end_ts_sec;
}

TEST(ReplayAlignment, WallClockForMidGameIsInsideCandleWindow) {
    auto game = make_game("2026-04-01");
    // Q3, 12:00 remaining = 720 + 360 wait — actually regulation_seconds_remaining
    // measures regulation seconds left, so Q3 with 12:00 = 720 + 720 = 1440 left.
    // Q3 mid (~6:00 left in Q3): regulation_seconds_remaining = 720 + 360 = 1080.
    auto q3_mid = make_event(1080);
    auto bounds = candle_window_for(game.game_date_iso);
    auto ts = pbp_wall_clock_ts_sec(game, q3_mid);

    EXPECT_GE(ts, bounds.start_ts_sec);
    EXPECT_LE(ts, bounds.end_ts_sec);
}

TEST(ReplayAlignment, WallClockForFinalBuzzerIsInsideCandleWindow) {
    auto game = make_game("2026-04-01");
    // Regulation ends: regulation_seconds_remaining = 0.
    auto final_buzzer = make_event(0);
    auto bounds = candle_window_for(game.game_date_iso);
    auto ts = pbp_wall_clock_ts_sec(game, final_buzzer);

    EXPECT_GE(ts, bounds.start_ts_sec);
    EXPECT_LE(ts, bounds.end_ts_sec);
}

TEST(ReplayAlignment, CandleWindowSpansEightHours) {
    auto bounds = candle_window_for("2026-04-01");
    EXPECT_EQ(bounds.end_ts_sec - bounds.start_ts_sec, 8 * 3600);
}

TEST(ReplayAlignment, WallClockMonotonicAcrossGameProgress) {
    // As game elapses, wall_clock should advance (tip → mid → end).
    auto game = make_game("2026-04-01");
    auto tip_ts = pbp_wall_clock_ts_sec(game, make_event(2880));
    auto mid_ts = pbp_wall_clock_ts_sec(game, make_event(1440));
    auto end_ts = pbp_wall_clock_ts_sec(game, make_event(0));
    EXPECT_LT(tip_ts, mid_ts);
    EXPECT_LT(mid_ts, end_ts);
}

// =========================================================================
// hotfix-bugs task #3: ResolutionLag must fire on FINAL status during
// backtest. The replay engine used to emit only game_status=2 (live)
// snapshots, so reslag's is_final() branch never triggered and the
// strategy's most reliable setup (buy YES at $0.94-0.99 on settled book)
// was untested.
// =========================================================================

namespace {

// Provider that returns fixed YES quotes — winning side near settle
// price, losing side near zero — so ResolutionLag is in its entry
// window without any synthetic-noise variance.
class FixedPriceProvider : public IKalshiPriceProvider {
public:
    FixedPriceProvider(const std::string& winning_tricode_upper,
                        double winning_bid, double winning_ask)
        : winning_upper_(winning_tricode_upper),
          winning_bid_(winning_bid), winning_ask_(winning_ask) {}
    std::optional<Quote> get_quote(const std::string& ticker,
                                    int64_t /*ts*/) override {
        // Ticker suffix tells us which side; if winning side, return the
        // ~settle price; otherwise return near-zero.
        std::string upper = ticker;
        for (auto& c : upper) c = static_cast<char>(std::toupper(c));
        bool is_winning = upper.size() >= winning_upper_.size() &&
            upper.compare(upper.size() - winning_upper_.size(),
                           winning_upper_.size(), winning_upper_) == 0;
        Quote q;
        if (is_winning) {
            q.yes_bid = winning_bid_;
            q.yes_ask = winning_ask_;
        } else {
            q.yes_bid = 1.0 - winning_ask_;
            q.yes_ask = 1.0 - winning_bid_;
        }
        q.volume = 500;
        return q;
    }

private:
    std::string winning_upper_;
    double winning_bid_;
    double winning_ask_;
};

PbpEvent make_pbp_event(int regulation_seconds_remaining,
                         int period, int home_score, int away_score) {
    PbpEvent e;
    e.regulation_seconds_remaining = regulation_seconds_remaining;
    e.period = period;
    e.clock_seconds = std::max(0, regulation_seconds_remaining -
                                   (4 - period) * 720);
    e.score_home = home_score;
    e.score_away = away_score;
    return e;
}

} // namespace

TEST(ReplayAlignment, ResolutionLagFiresOnFinalSnapshot) {
    // Build a CLOSE game so the Q4-blowout branch never triggers:
    //   regulation seconds = [2880 .. 0], home leads by 5 throughout.
    // Reslag's only way to fire here is via game_status=3 (final) emitted
    // by the replay engine after the PBP event loop.
    PbpGameSummary g;
    g.game_id = "0022500001";
    g.game_date_iso = "2026-04-01";
    g.home_tricode = "OKC";
    g.away_tricode = "SAS";
    g.home_final_score = 110;
    g.away_final_score = 105;
    g.game_status = 3;

    std::vector<PbpEvent> events;
    events.push_back(make_pbp_event(2880, 1, 0, 0));       // tipoff
    events.push_back(make_pbp_event(1440, 3, 55, 50));     // halftime-ish
    events.push_back(make_pbp_event(120, 4, 105, 100));    // close Q4 (lead < 15)
    events.push_back(make_pbp_event(0, 4, 110, 105));      // final buzzer

    ReplayConfig cfg;
    cfg.simulated_bankroll = 10000.0;
    cfg.run_resolution_lag = true;
    // Reslag entry window covers 0.95 → signal should fire on OKC ticker.
    cfg.reslag.min_entry_price = 0.94;
    cfg.reslag.max_entry_price = 0.99;
    cfg.reslag.min_market_volume = 100;
    cfg.reslag.min_lot_size = 5;
    // Disable NbaStrategy to keep the test focused: an unreachable edge.
    cfg.nba.min_edge_threshold = 1.0;

    FixedPriceProvider provider("OKC", /*bid=*/0.94, /*ask=*/0.95);
    BacktestReplay engine(cfg);
    auto result = engine.replay_game(g, events, provider);

    EXPECT_GE(result.signals_resolution_lag, 1)
        << "ResolutionLag did not fire on game_status=3 snapshot — replay "
        << "engine may not be emitting the final-status snapshot after the "
        << "PBP event loop.";
}
