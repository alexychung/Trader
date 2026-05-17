#include <gtest/gtest.h>
#include "strategy/kalshi/market_filter.hpp"

using namespace trader::kalshi;

static KalshiMarket make_market(const std::string& ticker, const std::string& category,
                                 double yes_bid, double yes_ask, int volume,
                                 const std::string& status = "open") {
    KalshiMarket m;
    m.ticker = ticker;
    m.category = category;
    m.status = status;
    m.yes_bid = yes_bid;
    m.yes_ask = yes_ask;
    m.volume = volume;
    return m;
}

// ===== Market Filter =====

TEST(MarketFilter, PassesGoodWeatherMarket) {
    MarketFilter filter;
    auto m = make_market("KXHIGHNY-26APR-T75", "weather", 0.60, 0.70, 200);
    auto result = filter.check(m);
    EXPECT_TRUE(result.passes);
}

TEST(MarketFilter, FractionalTradingEnabledPassesByDefault) {
    // Default policy: allow fractional-enabled markets, since on demo every
    // weather market is fractional-enabled. We submit whole-integer counts
    // via count_fp="N.00", so inbound truncation is bounded (<1 contract/fill).
    MarketFilter filter;
    auto m = make_market("KXHIGHNY-26APR-T75", "weather", 0.60, 0.70, 200);
    m.fractional_trading_enabled = true;
    auto result = filter.check(m);
    EXPECT_TRUE(result.passes);
}

TEST(MarketFilter, RejectsFractionalTradingEnabledWhenOptedIn) {
    // With skip_fractional_markets=true, the fractional flag becomes a hard
    // reject — an escape hatch for operators chasing unexplained P&L drift.
    FilterConfig cfg;
    cfg.skip_fractional_markets = true;
    MarketFilter filter(cfg);
    auto m = make_market("KXHIGHNY-26APR-T75", "weather", 0.60, 0.70, 200);
    m.fractional_trading_enabled = true;
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
    EXPECT_NE(result.reject_reason.find("Fractional"), std::string::npos);
}

TEST(MarketFilter, RejectsBlockedCategory) {
    MarketFilter filter;
    auto m = make_market("NBA-GAME", "sports", 0.50, 0.60, 500);
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
    EXPECT_TRUE(result.reject_reason.find("Blocked") != std::string::npos);
}

TEST(MarketFilter, RejectsClosedMarket) {
    MarketFilter filter;
    auto m = make_market("MKT1", "weather", 0.60, 0.70, 200, "closed");
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
}

TEST(MarketFilter, RejectsLowVolume) {
    MarketFilter filter;
    auto m = make_market("MKT1", "weather", 0.60, 0.70, 10);  // volume < 50
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
    EXPECT_TRUE(result.reject_reason.find("Volume") != std::string::npos);
}

TEST(MarketFilter, RejectsNarrowSpread) {
    MarketFilter filter;
    auto m = make_market("MKT1", "weather", 0.64, 0.65, 200);  // spread = 0.01
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
    EXPECT_TRUE(result.reject_reason.find("Spread") != std::string::npos);
}

TEST(MarketFilter, RejectsWideSpreadPlaceholderBook) {
    // bid=0.00 / ask=1.00 is a common Kalshi-demo placeholder — it's not
    // a real two-sided market, just the venue rendering "no quotes".
    // Spread=1.00 passes the old min_spread=0.03 check trivially; the new
    // max_spread guard must reject it.
    MarketFilter filter;
    auto m = make_market("MKT1", "weather", 0.00, 1.00, 200);
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
    EXPECT_TRUE(result.reject_reason.find("wide") != std::string::npos)
        << "got reject_reason=" << result.reject_reason;
}

TEST(MarketFilter, AllowsNormalSpreadAtMid) {
    // 50¢ / 60¢ = 10¢ spread. Within [min_spread=3, max_spread=30].
    MarketFilter filter;
    auto m = make_market("MKT1", "weather", 0.50, 0.60, 200);
    auto result = filter.check(m);
    EXPECT_TRUE(result.passes) << result.reject_reason;
}

TEST(MarketFilter, MaxSpreadIsConfigurable) {
    // A tighter max_spread (e.g. 0.05) rejects what the default lets through.
    FilterConfig cfg;
    cfg.max_spread = 0.05;
    MarketFilter filter(cfg);
    auto m = make_market("MKT1", "weather", 0.50, 0.60, 200);  // spread=0.10
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
    EXPECT_TRUE(result.reject_reason.find("wide") != std::string::npos);
}

TEST(MarketFilter, RejectsCheapContract) {
    MarketFilter filter;
    auto m = make_market("MKT1", "weather", 0.05, 0.10, 200);  // mid = 0.075 < 0.15
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
    EXPECT_TRUE(result.reject_reason.find("Price") != std::string::npos);
}

TEST(MarketFilter, RejectsExpensiveContract) {
    MarketFilter filter;
    auto m = make_market("MKT1", "weather", 0.90, 0.95, 200);  // mid = 0.925 > 0.85
    auto result = filter.check(m);
    EXPECT_FALSE(result.passes);
}

TEST(MarketFilter, FilterListReturnsPassing) {
    MarketFilter filter;
    std::vector<KalshiMarket> markets = {
        make_market("MKT1", "weather", 0.60, 0.70, 200),  // Pass
        make_market("MKT2", "sports", 0.50, 0.60, 500),   // Blocked
        make_market("MKT3", "economics", 0.40, 0.50, 100), // Pass
        make_market("MKT4", "weather", 0.05, 0.10, 200),   // Cheap
    };

    auto filtered = filter.filter(markets);
    EXPECT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0].market.ticker, "MKT1");
    EXPECT_EQ(filtered[1].market.ticker, "MKT3");
}

// ===== Market Maker =====

TEST(MarketMaker, GeneratesValidQuote) {
    MarketMaker mm;
    auto quote = mm.generate_quote("MKT1", 0.55, 0.45, 0.60, 0.8);
    // Spread = 0.15 > 0.08 threshold, confidence 0.8 > 0.7

    EXPECT_TRUE(quote.valid);
    EXPECT_GT(quote.bid_price, 0.45);  // Better than market bid
    EXPECT_LT(quote.ask_price, 0.60);  // Better than market ask
    EXPECT_LT(quote.bid_price, quote.ask_price);  // Doesn't cross
    EXPECT_EQ(quote.quantity, 3);
}

TEST(MarketMaker, RejectsNarrowSpread) {
    MarketMaker mm;
    auto quote = mm.generate_quote("MKT1", 0.55, 0.53, 0.56, 0.8);
    // Spread = 0.03 < 0.08 threshold
    EXPECT_FALSE(quote.valid);
}

TEST(MarketMaker, RejectsLowConfidence) {
    MarketMaker mm;
    auto quote = mm.generate_quote("MKT1", 0.55, 0.45, 0.60, 0.5);
    // Confidence 0.5 < 0.7 threshold
    EXPECT_FALSE(quote.valid);
}

TEST(MarketMaker, QuoteDontCross) {
    MarketMaker::Config cfg;
    cfg.half_spread_target = 0.01;  // Very tight target
    MarketMaker mm(cfg);

    // When model is near market mid, quotes might cross
    auto quote = mm.generate_quote("MKT1", 0.50, 0.49, 0.57, 0.9);
    if (quote.valid) {
        EXPECT_LT(quote.bid_price, quote.ask_price);
    }
}
