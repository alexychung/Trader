// Tests for the pure reconnect scaffolding and subscription-tracking policy.
// Covers:
//   - Exponential backoff curve: doubles each attempt, caps, handles overflow.
//   - Jitter envelope: always within [1-j, 1+j] * base, never zero/negative.
//   - Subscription tracking: dedup, unsubscribe, replay on reconnect.
//   - Gap-triggered auto-resubscribe.

#include <gtest/gtest.h>
#include "exchange/kalshi/ws_reconnect.hpp"
#include "exchange/kalshi/ws_client.hpp"

using namespace trader::kalshi;
using namespace std::chrono_literals;

// ===== Backoff curve =====

TEST(WsBackoff, DoublesEachAttempt) {
    BackoffConfig cfg{.base = 1000ms, .max = 30000ms, .jitter_pct = 0.0};
    EXPECT_EQ(backoff_delay_no_jitter(0, cfg).count(), 1000);
    EXPECT_EQ(backoff_delay_no_jitter(1, cfg).count(), 2000);
    EXPECT_EQ(backoff_delay_no_jitter(2, cfg).count(), 4000);
    EXPECT_EQ(backoff_delay_no_jitter(3, cfg).count(), 8000);
    EXPECT_EQ(backoff_delay_no_jitter(4, cfg).count(), 16000);
}

TEST(WsBackoff, CapsAtMax) {
    BackoffConfig cfg{.base = 1000ms, .max = 30000ms, .jitter_pct = 0.0};
    EXPECT_EQ(backoff_delay_no_jitter(5, cfg).count(), 30000);    // 32s→cap
    EXPECT_EQ(backoff_delay_no_jitter(10, cfg).count(), 30000);
    EXPECT_EQ(backoff_delay_no_jitter(100, cfg).count(), 30000);  // no overflow
}

TEST(WsBackoff, NegativeAttemptTreatedAsZero) {
    BackoffConfig cfg{.base = 1000ms, .max = 30000ms, .jitter_pct = 0.0};
    EXPECT_EQ(backoff_delay_no_jitter(-5, cfg).count(), 1000);
}

TEST(WsBackoff, JitterStaysInEnvelope) {
    BackoffConfig cfg{.base = 1000ms, .max = 30000ms, .jitter_pct = 0.25};
    // At rand_01=0: lowest point -> 0.75x; rand_01=1: highest -> 1.25x; rand=0.5: exact.
    EXPECT_EQ(apply_jitter(1000ms, 0.0, 0.25).count(), 750);
    EXPECT_EQ(apply_jitter(1000ms, 1.0, 0.25).count(), 1250);
    EXPECT_EQ(apply_jitter(1000ms, 0.5, 0.25).count(), 1000);
}

TEST(WsBackoff, JitterNeverZero) {
    // Even with huge jitter_pct, result must remain positive.
    EXPECT_GT(apply_jitter(1000ms, 0.0, 1.0).count(), 0);
    EXPECT_GT(apply_jitter(1000ms, 0.0, 0.99).count(), 0);
}

TEST(WsBackoff, ZeroJitterIsNoop) {
    EXPECT_EQ(apply_jitter(1234ms, 0.0, 0.0).count(), 1234);
    EXPECT_EQ(apply_jitter(1234ms, 1.0, 0.0).count(), 1234);
}

TEST(WsBackoff, CombinedDelayStaysInEnvelope) {
    BackoffConfig cfg{.base = 1000ms, .max = 30000ms, .jitter_pct = 0.25};
    for (int attempt = 0; attempt < 20; ++attempt) {
        auto base = backoff_delay_no_jitter(attempt, cfg).count();
        for (double r : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            auto d = backoff_delay(attempt, r, cfg).count();
            EXPECT_GE(d, base * 0.74);
            EXPECT_LE(d, base * 1.26);
        }
    }
}

// ===== Subscription tracking =====

class WsSubsTest : public ::testing::Test {
protected:
    void SetUp() override {
        auth_.set_api_key_id("k");
        ws_ = std::make_unique<KalshiWsClient>("wss://test/ws", auth_);
    }
    KalshiAuth auth_;
    std::unique_ptr<KalshiWsClient> ws_;
};

TEST_F(WsSubsTest, SubscribeOrderbookIsRecorded) {
    ws_->subscribe_orderbook("MKT1");
    auto subs = ws_->active_subscriptions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].channel, "orderbook_delta");
    EXPECT_EQ(subs[0].ticker, "MKT1");
}

TEST_F(WsSubsTest, SubscribeTickerIsRecorded) {
    ws_->subscribe_ticker("MKT1");
    auto subs = ws_->active_subscriptions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].channel, "ticker");
    EXPECT_EQ(subs[0].ticker, "MKT1");
}

TEST_F(WsSubsTest, SubscribeFillsHasNoTicker) {
    ws_->subscribe_fills();
    auto subs = ws_->active_subscriptions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].channel, "fill");
    EXPECT_EQ(subs[0].ticker, "");
}

TEST_F(WsSubsTest, DuplicateSubscribesAreDeduped) {
    ws_->subscribe_orderbook("MKT1");
    ws_->subscribe_orderbook("MKT1");
    ws_->subscribe_orderbook("MKT1");
    EXPECT_EQ(ws_->active_subscriptions().size(), 1u);
}

TEST_F(WsSubsTest, DifferentTickersSameChannelCoexist) {
    ws_->subscribe_orderbook("MKT1");
    ws_->subscribe_orderbook("MKT2");
    EXPECT_EQ(ws_->active_subscriptions().size(), 2u);
}

TEST_F(WsSubsTest, DifferentChannelsSameTickerCoexist) {
    ws_->subscribe_orderbook("MKT1");
    ws_->subscribe_ticker("MKT1");
    EXPECT_EQ(ws_->active_subscriptions().size(), 2u);
}

TEST_F(WsSubsTest, UnsubscribeSpecificTicker) {
    ws_->subscribe_orderbook("MKT1");
    ws_->subscribe_orderbook("MKT2");
    ws_->unsubscribe("orderbook_delta", "MKT1");
    auto subs = ws_->active_subscriptions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].ticker, "MKT2");
}

TEST_F(WsSubsTest, UnsubscribeEmptyTickerRemovesAllInChannel) {
    ws_->subscribe_orderbook("MKT1");
    ws_->subscribe_orderbook("MKT2");
    ws_->subscribe_ticker("MKT3");
    ws_->unsubscribe("orderbook_delta", "");
    auto subs = ws_->active_subscriptions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].channel, "ticker");
}

TEST_F(WsSubsTest, ReplayDoesNotCrashWithNoSubscriptions) {
    ws_->replay_subscriptions();   // just must not throw
    EXPECT_TRUE(ws_->active_subscriptions().empty());
}

TEST_F(WsSubsTest, ReplayPreservesSubscriptions) {
    ws_->subscribe_orderbook("MKT1");
    ws_->subscribe_ticker("MKT2");
    ws_->subscribe_fills();
    ws_->replay_subscriptions();
    EXPECT_EQ(ws_->active_subscriptions().size(), 3u);
}

// ===== Gap-triggered recovery =====

// ===== Live-mode lifecycle =====

TEST(WsLive, LiveModeStartStopCleanExit) {
    // Opt into live mode pointed at an unreachable URL. connect() returns
    // eagerly; the reader thread cycles through the backoff loop. disconnect()
    // must stop the thread and return in bounded time even though no
    // connection ever succeeded.
    KalshiAuth auth;
    auth.set_api_key_id("k");
    WsConfig cfg;
    cfg.enable_live = true;
    // Keep delays tiny so the test isn't slow and never actually sleeps
    // past the disconnect() deadline.
    cfg.backoff.base = std::chrono::milliseconds{50};
    cfg.backoff.max  = std::chrono::milliseconds{100};
    cfg.handshake_timeout = std::chrono::seconds{1};

    auto start = std::chrono::steady_clock::now();
    {
        KalshiWsClient ws("wss://127.0.0.1:1/ws", auth, cfg);  // guaranteed-closed port
        EXPECT_TRUE(ws.connect());
        // Let the reader thread attempt at least one connect cycle.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        ws.disconnect();
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    // Full cycle (connect attempt + backoff + teardown) must finish quickly.
    // If disconnect() hangs, this fires.
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(WsLive, LiveModeSubscribeSurvivesDisconnect) {
    // Subscriptions queued before connect should still be tracked after
    // disconnect — they're the inventory that replay_subscriptions replays.
    KalshiAuth auth;
    auth.set_api_key_id("k");
    WsConfig cfg;
    cfg.enable_live = true;
    cfg.backoff.base = std::chrono::milliseconds{50};
    cfg.backoff.max  = std::chrono::milliseconds{100};
    cfg.handshake_timeout = std::chrono::seconds{1};

    KalshiWsClient ws("wss://127.0.0.1:1/ws", auth, cfg);
    ws.subscribe_orderbook("MKT1");
    ws.subscribe_ticker("MKT2");
    EXPECT_TRUE(ws.connect());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ws.disconnect();

    auto subs = ws.active_subscriptions();
    EXPECT_EQ(subs.size(), 2u);
}

TEST_F(WsSubsTest, GapDropsBookAndLeavesSubscriptionIntactForReplay) {
    ws_->subscribe_orderbook("MKT1");
    // First snapshot establishes baseline.
    ws_->process_message(R"({"type":"orderbook_snapshot","msg":{"market_ticker":"MKT1","yes_bid":"0.50","yes_ask":"0.55","seq":10}})");
    EXPECT_DOUBLE_EQ(ws_->get_book_state("MKT1").yes_bid, 0.50);

    int gap_count = 0;
    ws_->set_on_seq_gap([&](const std::string&, const std::string&, int64_t, int64_t) { ++gap_count; });

    // Gap — delta with seq=15 instead of 11. Book should clear; subscription
    // record remains so replay/auto-resubscribe can rebuild.
    ws_->process_message(R"({"type":"orderbook_delta","msg":{"market_ticker":"MKT1","yes_bid":"0.99","yes_ask":"1.00","seq":15}})");
    EXPECT_EQ(gap_count, 1);
    EXPECT_DOUBLE_EQ(ws_->get_book_state("MKT1").yes_bid, 0.0);   // cleared
    auto subs = ws_->active_subscriptions();
    ASSERT_EQ(subs.size(), 1u);                                    // still tracked
    EXPECT_EQ(subs[0].ticker, "MKT1");
}
