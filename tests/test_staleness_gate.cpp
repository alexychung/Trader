#include <gtest/gtest.h>
#include "core/staleness_gate.hpp"
#include <thread>

using namespace trader;

TEST(StalenessGate, FreshMarketAfterUpdate) {
    StalenessGate g;
    g.on_market_update("MKT1");
    EXPECT_TRUE(g.is_market_fresh("MKT1"));
    EXPECT_TRUE(g.stale_reason_market("MKT1").empty());
}

TEST(StalenessGate, MarketStaleByDefault) {
    StalenessGate g;
    EXPECT_FALSE(g.is_market_fresh("MKT1"));
    EXPECT_FALSE(g.stale_reason_market("MKT1").empty());
}

TEST(StalenessGate, MarketBecomesStaleAfterMaxAge) {
    StalenessGate::Config cfg;
    cfg.market_max_age = std::chrono::seconds{1};
    StalenessGate g(cfg);
    g.seed_market("MKT1", std::chrono::system_clock::now() - std::chrono::seconds{2});
    EXPECT_FALSE(g.is_market_fresh("MKT1"));
}

TEST(StalenessGate, FeedFreshAfterUpdate) {
    StalenessGate g;
    g.on_feed_update("weather");
    EXPECT_TRUE(g.is_feed_fresh("weather"));
}

TEST(StalenessGate, FeedOverridePerKey) {
    StalenessGate::Config cfg;
    cfg.default_feed_max_age = std::chrono::seconds{3600};
    cfg.feed_overrides["weather"] = std::chrono::seconds{1};
    StalenessGate g(cfg);

    g.seed_feed("weather", std::chrono::system_clock::now() - std::chrono::seconds{2});
    g.seed_feed("econ",   std::chrono::system_clock::now() - std::chrono::seconds{2});

    EXPECT_FALSE(g.is_feed_fresh("weather"));  // 2s > 1s override
    EXPECT_TRUE(g.is_feed_fresh("econ"));      // 2s < 3600s default
}

TEST(StalenessGate, SeedMarket) {
    StalenessGate g;
    auto now = std::chrono::system_clock::now();
    g.seed_market("MKT1", now);
    auto map = g.last_market_updates();
    ASSERT_TRUE(map.count("MKT1"));
    EXPECT_EQ(map["MKT1"], now);
}
