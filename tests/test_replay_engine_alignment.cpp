#include <gtest/gtest.h>

#include "backtest/replay_engine.hpp"

using trader::backtest::candle_window_for;
using trader::backtest::CandleWindowBounds;
using trader::backtest::pbp_wall_clock_ts_sec;
using trader::backtest::PbpEvent;
using trader::backtest::PbpGameSummary;

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
