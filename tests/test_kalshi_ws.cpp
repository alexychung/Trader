#include <gtest/gtest.h>
#include "exchange/kalshi/ws_client.hpp"
#include <nlohmann/json.hpp>

using namespace trader::kalshi;

class KalshiWsTest : public ::testing::Test {
protected:
    void SetUp() override {
        auth_.set_api_key_id("test-key");
        ws_ = std::make_unique<KalshiWsClient>("wss://demo-api.kalshi.co/trade-api/ws/v2", auth_);
    }

    KalshiAuth auth_;
    std::unique_ptr<KalshiWsClient> ws_;
};

// ===== Message Parsing =====

TEST_F(KalshiWsTest, ParseTickerMessage) {
    nlohmann::json j = {
        {"market_ticker", "KXHIGHNY-26APR-T75"},
        {"yes_bid", "0.6500"},
        {"yes_ask", "0.7000"},
        {"last_price", "0.6700"},
        {"volume", 350}
    };

    auto update = KalshiWsClient::parse_ticker_message(j);
    ASSERT_TRUE(update.has_value());
    EXPECT_EQ(update->ticker, "KXHIGHNY-26APR-T75");
    EXPECT_DOUBLE_EQ(update->yes_bid, 0.65);
    EXPECT_DOUBLE_EQ(update->yes_ask, 0.70);
    EXPECT_DOUBLE_EQ(update->last_price, 0.67);
    EXPECT_EQ(update->volume, 350);
}

TEST_F(KalshiWsTest, ParseTickerMessageNumericPrices) {
    nlohmann::json j = {
        {"market_ticker", "CPI-26MAR"},
        {"yes_bid", 0.42},
        {"yes_ask", 0.48},
        {"last_price", 0.45},
        {"volume", 100}
    };

    auto update = KalshiWsClient::parse_ticker_message(j);
    ASSERT_TRUE(update.has_value());
    EXPECT_DOUBLE_EQ(update->yes_bid, 0.42);
    EXPECT_DOUBLE_EQ(update->yes_ask, 0.48);
}

TEST_F(KalshiWsTest, ParseTickerMessageMissingTicker) {
    nlohmann::json j = {{"yes_bid", "0.50"}};
    auto update = KalshiWsClient::parse_ticker_message(j);
    EXPECT_FALSE(update.has_value());
}

TEST_F(KalshiWsTest, ParseOrderbookDelta) {
    nlohmann::json j = {
        {"market_ticker", "KXHIGHCHI-26APR-T60"},
        {"yes_bid", "0.7200"},
        {"yes_ask", "0.7600"},
        {"yes_bid_size", 25},
        {"yes_ask_size", 15}
    };

    auto delta = KalshiWsClient::parse_orderbook_delta(j);
    ASSERT_TRUE(delta.has_value());
    EXPECT_EQ(delta->ticker, "KXHIGHCHI-26APR-T60");
    EXPECT_DOUBLE_EQ(delta->yes_bid, 0.72);
    EXPECT_DOUBLE_EQ(delta->yes_ask, 0.76);
    EXPECT_EQ(delta->yes_bid_size, 25);
    EXPECT_EQ(delta->yes_ask_size, 15);
}

TEST_F(KalshiWsTest, ParseFillMessage) {
    nlohmann::json j = {
        {"order_id", "ord-abc-123"},
        {"ticker", "KXHIGHNY-26APR-T75"},
        {"price", "0.6500"},
        {"count", 5},
        {"side", "yes"},
        {"action", "buy"}
    };

    auto fill = KalshiWsClient::parse_fill_message(j);
    ASSERT_TRUE(fill.has_value());
    EXPECT_EQ(fill->order_id, "ord-abc-123");
    EXPECT_EQ(fill->ticker, "KXHIGHNY-26APR-T75");
    EXPECT_DOUBLE_EQ(fill->price, 0.65);
    EXPECT_EQ(fill->count, 5);
    EXPECT_EQ(fill->side, "yes");
    EXPECT_EQ(fill->action, "buy");
}

TEST_F(KalshiWsTest, ParseFillMessageEmpty) {
    nlohmann::json j = {{"some_other_field", "value"}};
    auto fill = KalshiWsClient::parse_fill_message(j);
    EXPECT_FALSE(fill.has_value());
}

TEST_F(KalshiWsTest, ParseFillMessageRejectsPublicTradeTape) {
    // Public trade-tape messages have ticker + price but no order_id. These
    // must not be treated as account fills — doing so would apply their cost
    // to order_id="" and drain balance silently.
    nlohmann::json j = {
        {"ticker", "KXHIGHNY-26APR-T75"},
        {"price", "0.6500"},
        {"count", 5},
        {"taker_side", "yes"}
    };
    auto fill = KalshiWsClient::parse_fill_message(j);
    EXPECT_FALSE(fill.has_value());
}

TEST_F(KalshiWsTest, ParseFillMessageRejectsMissingAction) {
    // A fill without action ("buy"/"sell") has no defined balance direction.
    nlohmann::json j = {
        {"order_id", "ord-abc-123"},
        {"ticker", "KXHIGHNY-26APR-T75"},
        {"price", "0.6500"},
        {"count", 5},
        {"side", "yes"}
    };
    auto fill = KalshiWsClient::parse_fill_message(j);
    EXPECT_FALSE(fill.has_value());
}

// ===== Book State Management =====

TEST_F(KalshiWsTest, BookStateUpdatesOnTickerMessage) {
    std::string msg = R"({
        "type": "ticker",
        "msg": {
            "market_ticker": "KXHIGHNY-26APR-T75",
            "yes_bid": "0.6500",
            "yes_ask": "0.7000",
            "last_price": "0.6700",
            "volume": 350
        }
    })";

    ws_->process_message(msg);

    auto state = ws_->get_book_state("KXHIGHNY-26APR-T75");
    EXPECT_DOUBLE_EQ(state.yes_bid, 0.65);
    EXPECT_DOUBLE_EQ(state.yes_ask, 0.70);
    EXPECT_DOUBLE_EQ(state.last_price, 0.67);
    EXPECT_EQ(state.volume, 350);
}

TEST_F(KalshiWsTest, BookStateUpdatesOnOrderbookDelta) {
    std::string msg = R"({
        "type": "orderbook_delta",
        "msg": {
            "market_ticker": "KXHIGHCHI-26APR-T60",
            "yes_bid": "0.7200",
            "yes_ask": "0.7600",
            "yes_bid_size": 25,
            "yes_ask_size": 15
        }
    })";

    ws_->process_message(msg);

    auto state = ws_->get_book_state("KXHIGHCHI-26APR-T60");
    EXPECT_DOUBLE_EQ(state.yes_bid, 0.72);
    EXPECT_DOUBLE_EQ(state.yes_ask, 0.76);
}

TEST_F(KalshiWsTest, BookStateDefaultsForUnknownTicker) {
    auto state = ws_->get_book_state("NONEXISTENT");
    EXPECT_DOUBLE_EQ(state.yes_bid, 0.0);
    EXPECT_DOUBLE_EQ(state.yes_ask, 0.0);
}

TEST_F(KalshiWsTest, MultipleUpdatesAccumulate) {
    ws_->process_message(R"({"type":"ticker","msg":{"market_ticker":"MKT1","yes_bid":"0.50","yes_ask":"0.55","last_price":"0.52","volume":100}})");
    ws_->process_message(R"({"type":"ticker","msg":{"market_ticker":"MKT2","yes_bid":"0.30","yes_ask":"0.35","last_price":"0.32","volume":50}})");

    auto states = ws_->all_book_states();
    EXPECT_EQ(states.size(), 2u);
    EXPECT_DOUBLE_EQ(states["MKT1"].yes_bid, 0.50);
    EXPECT_DOUBLE_EQ(states["MKT2"].yes_bid, 0.30);
}

TEST_F(KalshiWsTest, BookStateUpdatesIncrementally) {
    // First update sets bid/ask
    ws_->process_message(R"({"type":"ticker","msg":{"market_ticker":"MKT1","yes_bid":"0.50","yes_ask":"0.55","last_price":"0.52","volume":100}})");
    // Second update only changes bid
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.51","yes_ask":"0.55"}})");

    auto state = ws_->get_book_state("MKT1");
    EXPECT_DOUBLE_EQ(state.yes_bid, 0.51);  // Updated
    EXPECT_DOUBLE_EQ(state.yes_ask, 0.55);  // Kept from delta
    EXPECT_DOUBLE_EQ(state.last_price, 0.52);  // Kept from first
    EXPECT_EQ(state.volume, 100);  // Kept from first
}

// ===== Callbacks =====

TEST_F(KalshiWsTest, MarketUpdateCallbackFires) {
    WsMarketUpdate received;
    bool called = false;
    ws_->set_on_market_update([&](const WsMarketUpdate& u) {
        received = u;
        called = true;
    });

    ws_->process_message(R"({"type":"ticker","msg":{"market_ticker":"MKT1","yes_bid":"0.60","yes_ask":"0.65","volume":200}})");

    EXPECT_TRUE(called);
    EXPECT_EQ(received.ticker, "MKT1");
    EXPECT_DOUBLE_EQ(received.yes_bid, 0.60);
}

TEST_F(KalshiWsTest, FillCallbackFires) {
    WsFill received;
    bool called = false;
    ws_->set_on_fill([&](const WsFill& f) {
        received = f;
        called = true;
    });

    ws_->process_message(R"({"type":"fill","msg":{"order_id":"ord-123","ticker":"MKT1","price":"0.65","count":5,"side":"yes","action":"buy"}})");

    EXPECT_TRUE(called);
    EXPECT_EQ(received.order_id, "ord-123");
    EXPECT_DOUBLE_EQ(received.price, 0.65);
    EXPECT_EQ(received.count, 5);
}

TEST_F(KalshiWsTest, ErrorCallbackOnInvalidJson) {
    std::string error_msg;
    ws_->set_on_error([&](const std::string& e) { error_msg = e; });

    ws_->process_message("not valid json {{{");

    EXPECT_FALSE(error_msg.empty());
}

// ===== Timestamp Parsing (ts_ms vs deprecated ts) =====

TEST_F(KalshiWsTest, ParsesTsMsWhenPresent) {
    nlohmann::json j = {
        {"market_ticker", "MKT1"},
        {"yes_bid", "0.50"}, {"yes_ask", "0.55"},
        {"ts_ms", 1744200000123LL}
    };
    auto update = KalshiWsClient::parse_ticker_message(j);
    ASSERT_TRUE(update.has_value());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        update->timestamp.time_since_epoch()).count();
    EXPECT_EQ(ms, 1744200000123LL);
}

TEST_F(KalshiWsTest, FallsBackToSecondsTsWhenTsMsAbsent) {
    nlohmann::json j = {
        {"market_ticker", "MKT1"},
        {"yes_bid", "0.50"}, {"yes_ask", "0.55"},
        {"ts", 1744200000LL}
    };
    auto update = KalshiWsClient::parse_ticker_message(j);
    ASSERT_TRUE(update.has_value());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        update->timestamp.time_since_epoch()).count();
    EXPECT_EQ(ms, 1744200000000LL);
}

TEST_F(KalshiWsTest, TsMsTakesPrecedenceOverDeprecatedTs) {
    nlohmann::json j = {
        {"market_ticker", "MKT1"},
        {"yes_bid", "0.50"}, {"yes_ask", "0.55"},
        {"ts_ms", 1744200000999LL},
        {"ts", 1700000000LL}   // deprecated, should be ignored
    };
    auto update = KalshiWsClient::parse_ticker_message(j);
    ASSERT_TRUE(update.has_value());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        update->timestamp.time_since_epoch()).count();
    EXPECT_EQ(ms, 1744200000999LL);
}

// ===== Orderbook sequence gap detection =====

TEST_F(KalshiWsTest, NoGapAcrossConsecutiveSeqs) {
    int gap_count = 0;
    ws_->set_on_seq_gap([&](const std::string&, const std::string&, int64_t, int64_t) {
        ++gap_count;
    });
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.50","yes_ask":"0.55","seq":10}})");
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.51","yes_ask":"0.55","seq":11}})");
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.52","yes_ask":"0.55","seq":12}})");
    EXPECT_EQ(gap_count, 0);
    EXPECT_EQ(ws_->last_seq("orderbook_delta", "MKT1"), 12);
}

TEST_F(KalshiWsTest, DetectsSeqGap) {
    std::string gap_channel, gap_ticker;
    int64_t expected = 0, received = 0;
    int gap_count = 0;
    ws_->set_on_seq_gap([&](const std::string& ch, const std::string& t, int64_t e, int64_t r) {
        gap_channel = ch; gap_ticker = t; expected = e; received = r; ++gap_count;
    });
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.50","yes_ask":"0.55","seq":10}})");
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.52","yes_ask":"0.55","seq":13}})");

    EXPECT_EQ(gap_count, 1);
    EXPECT_EQ(gap_channel, "orderbook_delta");
    EXPECT_EQ(gap_ticker, "MKT1");
    EXPECT_EQ(expected, 11);
    EXPECT_EQ(received, 13);
}

TEST_F(KalshiWsTest, GapDropsLocalBookUntilResync) {
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.50","yes_ask":"0.55","seq":10}})");
    EXPECT_DOUBLE_EQ(ws_->get_book_state("MKT1").yes_bid, 0.50);

    // Gap — book should be cleared to prevent acting on stale state.
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.99","yes_ask":"1.00","seq":15}})");
    EXPECT_DOUBLE_EQ(ws_->get_book_state("MKT1").yes_bid, 0.0);

    // Fresh snapshot re-seeds the book and seq baseline.
    ws_->process_message(R"({"type":"orderbook_snapshot","msg":{"market_ticker":"MKT1","yes_bid":"0.60","yes_ask":"0.65","seq":20}})");
    EXPECT_DOUBLE_EQ(ws_->get_book_state("MKT1").yes_bid, 0.60);

    int gap_count = 0;
    ws_->set_on_seq_gap([&](const std::string&, const std::string&, int64_t, int64_t) { ++gap_count; });
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.61","yes_ask":"0.65","seq":21}})");
    EXPECT_EQ(gap_count, 0);
}

// ===== Connection State =====

TEST_F(KalshiWsTest, ConnectDisconnect) {
    EXPECT_FALSE(ws_->is_connected());
    EXPECT_TRUE(ws_->connect());
    EXPECT_TRUE(ws_->is_connected());
    ws_->disconnect();
    EXPECT_FALSE(ws_->is_connected());
}
