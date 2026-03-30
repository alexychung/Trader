#include <gtest/gtest.h>
#include "exchange/kalshi/rest_client.hpp"
#include <nlohmann/json.hpp>

using namespace trader::kalshi;

// Test JSON parsing of market data (mocked responses, no network)

TEST(KalshiRest, ParseMarketWithStringPrices) {
    nlohmann::json j = {
        {"ticker", "KXHIGHNY-26APR-T75"},
        {"title", "NYC High Above 75°F on Apr 26?"},
        {"category", "weather"},
        {"status", "open"},
        {"yes_bid", "0.6500"},
        {"yes_ask", "0.7000"},
        {"last_price", "0.6700"},
        {"volume", 350},
        {"open_interest", 1200},
        {"close_time", "2026-04-26T21:00:00Z"},
        {"expiration_time", "2026-04-27T12:00:00Z"},
        {"result", ""}
    };

    auto market = KalshiRestClient::parse_market(j);

    EXPECT_EQ(market.ticker, "KXHIGHNY-26APR-T75");
    EXPECT_EQ(market.title, "NYC High Above 75°F on Apr 26?");
    EXPECT_EQ(market.category, "weather");
    EXPECT_EQ(market.status, "open");
    EXPECT_DOUBLE_EQ(market.yes_bid, 0.65);
    EXPECT_DOUBLE_EQ(market.yes_ask, 0.70);
    EXPECT_DOUBLE_EQ(market.last_price, 0.67);
    EXPECT_EQ(market.volume, 350);
    EXPECT_EQ(market.open_interest, 1200);
    EXPECT_EQ(market.close_time, "2026-04-26T21:00:00Z");
    EXPECT_EQ(market.result, "");
}

TEST(KalshiRest, ParseMarketWithNumericPrices) {
    // Some API versions return numbers instead of strings
    nlohmann::json j = {
        {"ticker", "CPI-26MAR-T3.5"},
        {"title", "CPI YoY > 3.5% in March 2026?"},
        {"category", "economics"},
        {"status", "open"},
        {"yes_bid", 0.42},
        {"yes_ask", 0.48},
        {"last_price", 0.45},
        {"volume", 500},
        {"open_interest", 800}
    };

    auto market = KalshiRestClient::parse_market(j);

    EXPECT_EQ(market.ticker, "CPI-26MAR-T3.5");
    EXPECT_DOUBLE_EQ(market.yes_bid, 0.42);
    EXPECT_DOUBLE_EQ(market.yes_ask, 0.48);
    EXPECT_DOUBLE_EQ(market.last_price, 0.45);
}

TEST(KalshiRest, ParseMarketMissingFields) {
    nlohmann::json j = {
        {"ticker", "MINIMAL-MARKET"}
    };

    auto market = KalshiRestClient::parse_market(j);

    EXPECT_EQ(market.ticker, "MINIMAL-MARKET");
    EXPECT_EQ(market.title, "");
    EXPECT_DOUBLE_EQ(market.yes_bid, 0.0);
    EXPECT_DOUBLE_EQ(market.yes_ask, 0.0);
    EXPECT_EQ(market.volume, 0);
}

TEST(KalshiRest, ParseMarketSettled) {
    nlohmann::json j = {
        {"ticker", "KXHIGHCHI-26MAR-T50"},
        {"status", "settled"},
        {"result", "yes"},
        {"volume", 1500}
    };

    auto market = KalshiRestClient::parse_market(j);

    EXPECT_EQ(market.status, "settled");
    EXPECT_EQ(market.result, "yes");
}

TEST(KalshiRest, OrderbookBestBid) {
    KalshiOrderbook book;
    book.ticker = "TEST";

    // Ascending order, best bid last
    book.yes_bids = {{0.55, 10}, {0.58, 20}, {0.60, 15}};

    EXPECT_DOUBLE_EQ(book.best_yes_bid(), 0.60);
}

TEST(KalshiRest, OrderbookBestBidEmpty) {
    KalshiOrderbook book;
    EXPECT_DOUBLE_EQ(book.best_yes_bid(), 0.0);
}

TEST(KalshiRest, ParseMultipleMarkets) {
    nlohmann::json markets_array = nlohmann::json::array({
        {{"ticker", "MKT1"}, {"status", "open"}, {"volume", 100}},
        {{"ticker", "MKT2"}, {"status", "open"}, {"volume", 200}},
        {{"ticker", "MKT3"}, {"status", "closed"}, {"volume", 50}}
    });

    std::vector<KalshiMarket> parsed;
    for (const auto& j : markets_array) {
        parsed.push_back(KalshiRestClient::parse_market(j));
    }

    ASSERT_EQ(parsed.size(), 3u);
    EXPECT_EQ(parsed[0].ticker, "MKT1");
    EXPECT_EQ(parsed[1].volume, 200);
    EXPECT_EQ(parsed[2].status, "closed");
}
