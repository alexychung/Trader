#include <gtest/gtest.h>

#include "backtest/settled_markets_fetcher.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/rest_client.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

using namespace trader::backtest;
using trader::kalshi::KalshiAuth;
using trader::kalshi::KalshiRestClient;

namespace {

// Temp dir unique per test so ctest -j runs don't collide.
std::string make_test_cache_dir() {
    auto base = std::filesystem::temp_directory_path() /
                ("trader_smf_test_" +
                 std::to_string(
                     std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count()));
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base.string();
}

struct Fixture : ::testing::Test {
    std::string cache_dir;
    KalshiAuth auth;
    // Point at a closed localhost port so cache-miss paths (which fall
    // through to the real REST client) fail connect immediately instead of
    // burning 500ms resolving demo-api.kalshi.co. Keeps the suite fast
    // without introducing a full REST mock.
    KalshiRestClient rest{"https://127.0.0.1:9/trade-api/v2", auth};

    void SetUp() override {
        cache_dir = make_test_cache_dir();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(cache_dir, ec);
    }

    void seed_markets_cache(const std::string& start_date,
                             const std::string& end_date,
                             const std::string& category,
                             const nlohmann::json& markets_array) {
        // Recreate the fetcher's sanitize+filename rule inline.
        std::string file = cache_dir + "/markets_" + start_date + "_" +
                           end_date + "_" +
                           (category.empty() ? "all" : category) + ".json";
        std::ofstream out(file);
        out << markets_array.dump();
    }

    void seed_trades_cache(const std::string& ticker,
                            const nlohmann::json& trades_array) {
        std::string file = cache_dir + "/trades_" + ticker + ".json";
        std::ofstream out(file);
        out << trades_array.dump();
    }
};

}  // namespace

// ===== Markets cache =====

TEST_F(Fixture, CacheHitParsesMarketsAndIncrementsCounter) {
    nlohmann::json markets = nlohmann::json::array({
        {
            {"ticker", "KXHIGHNY-26APR26-T75"},
            {"category", "weather"},
            {"status", "settled"},
            {"yes_bid_dollars", "0.6500"},
            {"yes_ask_dollars", "0.6800"},
            {"last_price_dollars", "0.6700"},
            {"volume", 1200},
            {"result", "yes"}
        }
    });
    seed_markets_cache("2026-04-01", "2026-04-30", "weather", markets);

    SettledMarketsFetcher fetcher(rest, cache_dir);
    auto result = fetcher.fetch_settled_markets("2026-04-01", "2026-04-30", "weather", 50);

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].ticker, "KXHIGHNY-26APR26-T75");
    EXPECT_EQ(result[0].status, "settled");
    EXPECT_DOUBLE_EQ(result[0].yes_bid, 0.65);
    EXPECT_DOUBLE_EQ(result[0].yes_ask, 0.68);
    EXPECT_EQ(result[0].result, "yes");
    EXPECT_EQ(fetcher.cache_hits(), 1);
    EXPECT_EQ(fetcher.cache_misses(), 0);
}

TEST_F(Fixture, MissingCacheFileIsMissNotCrash) {
    SettledMarketsFetcher fetcher(rest, cache_dir);
    // Nothing seeded. Different date range than the (absent) file.
    auto result = fetcher.fetch_settled_markets("2026-05-01", "2026-05-02", "weather", 50);
    // The fetch path then falls through to REST which fails (no network)
    // and returns empty. What we verify: no crash, cache_miss counted.
    EXPECT_EQ(result.size(), 0u);
    EXPECT_GE(fetcher.cache_misses(), 1);
    EXPECT_EQ(fetcher.cache_hits(), 0);
}

TEST_F(Fixture, CorruptCacheFileFallsThroughToMiss) {
    // Non-JSON content in the cache path. Parser should swallow, log, and
    // fall through to the refetch branch (which will also fail offline —
    // the point is that corrupt cache doesn't kill the process).
    std::string file = cache_dir + "/markets_2026-04-01_2026-04-30_weather.json";
    std::ofstream(file) << "<<not json>>";

    SettledMarketsFetcher fetcher(rest, cache_dir);
    auto result = fetcher.fetch_settled_markets("2026-04-01", "2026-04-30", "weather", 50);
    EXPECT_EQ(result.size(), 0u);
    EXPECT_GE(fetcher.cache_misses(), 1);
}

TEST_F(Fixture, DifferentCategoriesKeyDifferentCacheFiles) {
    // Seed two distinct category caches for the same date range. Each must
    // be independently retrievable — no cross-category leakage.
    nlohmann::json weather_only = nlohmann::json::array({
        {{"ticker", "KXHIGHNY-26APR26-T75"}, {"category", "weather"},
         {"status", "settled"}, {"yes_bid_dollars", "0.10"},
         {"yes_ask_dollars", "0.12"}, {"last_price_dollars", "0.11"}}
    });
    nlohmann::json econ_only = nlohmann::json::array({
        {{"ticker", "KXCPIYY-26MAY-T3.0"}, {"category", "economics"},
         {"status", "settled"}, {"yes_bid_dollars", "0.40"},
         {"yes_ask_dollars", "0.42"}, {"last_price_dollars", "0.41"}}
    });
    seed_markets_cache("2026-04-01", "2026-04-30", "weather", weather_only);
    seed_markets_cache("2026-04-01", "2026-04-30", "economics", econ_only);

    SettledMarketsFetcher fetcher(rest, cache_dir);

    auto w = fetcher.fetch_settled_markets("2026-04-01", "2026-04-30", "weather", 50);
    ASSERT_EQ(w.size(), 1u);
    EXPECT_EQ(w[0].ticker, "KXHIGHNY-26APR26-T75");

    auto e = fetcher.fetch_settled_markets("2026-04-01", "2026-04-30", "economics", 50);
    ASSERT_EQ(e.size(), 1u);
    EXPECT_EQ(e[0].ticker, "KXCPIYY-26MAY-T3.0");

    EXPECT_EQ(fetcher.cache_hits(), 2);
}

// ===== Trade cache =====

TEST_F(Fixture, TradeCacheHitReturnsFields) {
    nlohmann::json trades = nlohmann::json::array({
        {{"yes_price", 0.55}, {"count", 3}, {"taker_side", "yes"},
         {"created_at", "2026-04-01T10:00:00Z"}},
        {{"yes_price", 0.60}, {"count", 1}, {"taker_side", "no"},
         {"created_at", "2026-04-01T11:00:00Z"}}
    });
    seed_trades_cache("MKT1", trades);

    SettledMarketsFetcher fetcher(rest, cache_dir);
    auto out = fetcher.fetch_trades("MKT1");

    ASSERT_EQ(out.size(), 2u);
    // Cache returns oldest-first regardless of stored order.
    EXPECT_EQ(out[0].ticker, "MKT1");
    EXPECT_DOUBLE_EQ(out[0].yes_price, 0.55);
    EXPECT_EQ(out[0].count, 3);
    EXPECT_EQ(out[0].taker_side, "yes");
    EXPECT_EQ(out[0].created_at, "2026-04-01T10:00:00Z");
    EXPECT_EQ(fetcher.cache_hits(), 1);
}

TEST_F(Fixture, TradeCacheReSortsOldestFirstOnHit) {
    // Regression: the live cache writes fills in API order (newest first).
    // On cache hit we must re-sort so the backtest driver sees
    // chronological order. A buggy implementation would return the stored
    // order and decision timelines would iterate in reverse — we hit this
    // in production (see driver fallback path).
    nlohmann::json trades_newest_first = nlohmann::json::array({
        {{"yes_price", 0.60}, {"count", 1}, {"taker_side", "no"},
         {"created_at", "2026-04-01T12:00:00Z"}},     // newest
        {{"yes_price", 0.50}, {"count", 2}, {"taker_side", "yes"},
         {"created_at", "2026-04-01T09:00:00Z"}},     // oldest
        {{"yes_price", 0.55}, {"count", 3}, {"taker_side", "yes"},
         {"created_at", "2026-04-01T10:30:00Z"}}
    });
    seed_trades_cache("MKT1", trades_newest_first);

    SettledMarketsFetcher fetcher(rest, cache_dir);
    auto out = fetcher.fetch_trades("MKT1");
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].created_at, "2026-04-01T09:00:00Z");
    EXPECT_EQ(out[1].created_at, "2026-04-01T10:30:00Z");
    EXPECT_EQ(out[2].created_at, "2026-04-01T12:00:00Z");
}

TEST_F(Fixture, TradeCacheMissDoesNotCrash) {
    SettledMarketsFetcher fetcher(rest, cache_dir);
    // Nothing seeded for this ticker. Expect: empty result, a miss counted,
    // no crash on the subsequent network fallback (which will fail offline).
    auto out = fetcher.fetch_trades("UNKNOWN-MKT");
    EXPECT_EQ(out.size(), 0u);
    EXPECT_GE(fetcher.cache_misses(), 1);
}

TEST_F(Fixture, TradeCacheCorruptFallsThroughToMiss) {
    std::string file = cache_dir + "/trades_MKT1.json";
    std::ofstream(file) << "<<not json>>";

    SettledMarketsFetcher fetcher(rest, cache_dir);
    auto out = fetcher.fetch_trades("MKT1");
    EXPECT_EQ(out.size(), 0u);
}

// ===== Ticker sanitization in filename =====

TEST_F(Fixture, TickerWithSlashSanitizedIntoCachePath) {
    // The underlying sanitize() rule replaces non [A-Za-z0-9_-] with '_'.
    // A ticker with an odd character must still write to a readable file,
    // not crash or leak path traversal.
    SettledMarketsFetcher fetcher(rest, cache_dir);
    // Can't easily verify the path without exposing it, but calling
    // fetch_trades on such a ticker must not throw or escape cache_dir.
    EXPECT_NO_THROW({
        auto out = fetcher.fetch_trades("KX/EVIL-..\\..\\path");
        (void)out;
    });
}
