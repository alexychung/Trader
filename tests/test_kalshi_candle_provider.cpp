#include <gtest/gtest.h>

#include "backtest/kalshi_candle_provider.hpp"

#include <optional>
#include <vector>

using trader::backtest::IKalshiPriceProvider;
using trader::backtest::KalshiCandlePriceProvider;

namespace {

KalshiCandlePriceProvider::KalshiCandle make_candle(int64_t end_ts,
                                                    double bid, double ask,
                                                    int volume = 100) {
    KalshiCandlePriceProvider::KalshiCandle c;
    c.end_period_ts = end_ts;
    c.yes_bid_close = bid;
    c.yes_ask_close = ask;
    c.volume = volume;
    return c;
}

} // namespace

TEST(KalshiCandleSelectQuote, ReturnsQuoteForCoveringBar) {
    std::vector<KalshiCandlePriceProvider::KalshiCandle> candles = {
        make_candle(1000, 0.50, 0.52),
        make_candle(1060, 0.51, 0.53),
        make_candle(1120, 0.52, 0.54),
    };
    auto q = KalshiCandlePriceProvider::select_quote(candles, 1030);
    ASSERT_TRUE(q.has_value());
    // bar covering ts=1030 ends at 1060 (lower_bound returns first end>=ts).
    EXPECT_DOUBLE_EQ(q->yes_bid, 0.51);
    EXPECT_DOUBLE_EQ(q->yes_ask, 0.53);
}

TEST(KalshiCandleSelectQuote, FallsBackToLastBarPastEnd) {
    std::vector<KalshiCandlePriceProvider::KalshiCandle> candles = {
        make_candle(1000, 0.50, 0.52),
        make_candle(1060, 0.51, 0.53),
    };
    auto q = KalshiCandlePriceProvider::select_quote(candles, 9999);
    ASSERT_TRUE(q.has_value());
    EXPECT_DOUBLE_EQ(q->yes_bid, 0.51);
    EXPECT_DOUBLE_EQ(q->yes_ask, 0.53);
}

TEST(KalshiCandleSelectQuote, EmptyCandlesReturnsNullopt) {
    auto q = KalshiCandlePriceProvider::select_quote({}, 1000);
    EXPECT_FALSE(q.has_value());
}

TEST(KalshiCandleSelectQuote, FiltersZeroBidAndAsk) {
    // No-trade bar: Kalshi reports the bar but with zero bid/ask. Common
    // pre-tipoff and post-settle.
    std::vector<KalshiCandlePriceProvider::KalshiCandle> candles = {
        make_candle(1000, 0.0, 0.0),
    };
    EXPECT_FALSE(KalshiCandlePriceProvider::select_quote(candles, 500).has_value());
}

TEST(KalshiCandleSelectQuote, FiltersOneSidedBook) {
    // Bid=0, ask>0 — one-sided book, almost always a stale artifact.
    std::vector<KalshiCandlePriceProvider::KalshiCandle> candles_no_bid = {
        make_candle(1000, 0.0, 0.55),
    };
    EXPECT_FALSE(KalshiCandlePriceProvider::select_quote(candles_no_bid, 500).has_value());

    std::vector<KalshiCandlePriceProvider::KalshiCandle> candles_no_ask = {
        make_candle(1000, 0.50, 0.0),
    };
    EXPECT_FALSE(KalshiCandlePriceProvider::select_quote(candles_no_ask, 500).has_value());
}

TEST(KalshiCandleSelectQuote, FiltersWideSpreadBars) {
    // > 25¢ spread = broken/parked book. Without this filter the strategy
    // fires phantom 50-70% edge signals against bars like {bid=0.04,
    // ask=0.05} when the reality was {0.49, 0.51}.
    std::vector<KalshiCandlePriceProvider::KalshiCandle> candles = {
        make_candle(1000, 0.04, 0.95),  // 91¢ spread — clearly broken
    };
    EXPECT_FALSE(KalshiCandlePriceProvider::select_quote(candles, 500).has_value());
}

TEST(KalshiCandleSelectQuote, AcceptsNormalSpread) {
    // 24¢ spread — just below the filter threshold. Wide but plausible
    // for a thin pre-tipoff NBA book.
    std::vector<KalshiCandlePriceProvider::KalshiCandle> candles = {
        make_candle(1000, 0.30, 0.54),
    };
    auto q = KalshiCandlePriceProvider::select_quote(candles, 500);
    ASSERT_TRUE(q.has_value());
    EXPECT_DOUBLE_EQ(q->yes_bid, 0.30);
    EXPECT_DOUBLE_EQ(q->yes_ask, 0.54);
}

TEST(KalshiCandleSelectQuote, FiltersInvertedSpread) {
    // bid > ask is impossible in a real book — treat as broken.
    std::vector<KalshiCandlePriceProvider::KalshiCandle> candles = {
        make_candle(1000, 0.60, 0.55),
    };
    EXPECT_FALSE(KalshiCandlePriceProvider::select_quote(candles, 500).has_value());
}
