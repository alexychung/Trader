#include <gtest/gtest.h>
#include "core/alert_manager.hpp"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

using namespace trader;

namespace {

// Collects payloads the worker would have posted. Thread-safe; signals each
// capture so tests can block until delivery without racing on sleeps.
class PayloadCollector {
public:
    AlertManager::PostHandler handler() {
        return [this](const std::string& p) {
            std::lock_guard<std::mutex> lock(mutex_);
            payloads_.push_back(p);
            cv_.notify_all();
            return true;
        };
    }

    bool wait_for_count(std::size_t n, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return payloads_.size() >= n; });
    }

    std::size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return payloads_.size();
    }

    std::vector<std::string> drain() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return payloads_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::string> payloads_;
};

AlertManager::Config with_webhook(AlertManager::Format fmt = AlertManager::Format::Discord) {
    AlertManager::Config c;
    c.webhook_url = "https://webhook.example/test";  // non-empty → enables worker
    c.format = fmt;
    c.throttle = std::chrono::seconds(0);  // disable throttle by default for tests
    return c;
}

}  // namespace

// ===== Enable / disable gating =====

TEST(AlertManager, EmptyWebhookDisablesWorker) {
    AlertManager::Config cfg;  // webhook_url empty
    AlertManager mgr(cfg);
    EXPECT_FALSE(mgr.enabled());

    // Calls are no-ops; nothing queued, nothing crashes.
    mgr.send("title", "msg");
    mgr.on_kill_switch("reason", 100.0, -5.0);
    mgr.on_balance_change(100.0, 50.0);
}

TEST(AlertManager, WebhookUrlEnablesWorker) {
    auto cfg = with_webhook();
    AlertManager mgr(cfg);
    EXPECT_TRUE(mgr.enabled());
}

// ===== Category toggles =====

TEST(AlertManager, SendKillSwitchToggleSuppresses) {
    PayloadCollector coll;
    auto cfg = with_webhook();
    cfg.send_kill_switch = false;
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.on_kill_switch("test", 100.0, -30.0);
    // No payload should flow because the category is gated off.
    EXPECT_FALSE(coll.wait_for_count(1, std::chrono::milliseconds(100)));
    EXPECT_EQ(coll.count(), 0u);
}

TEST(AlertManager, DailySummaryToggleSuppresses) {
    PayloadCollector coll;
    auto cfg = with_webhook();
    cfg.send_daily_summary = false;
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.daily_summary(100.0, 5.0, 3, 0.12);
    EXPECT_FALSE(coll.wait_for_count(1, std::chrono::milliseconds(100)));
}

TEST(AlertManager, BalanceChangeToggleSuppresses) {
    PayloadCollector coll;
    auto cfg = with_webhook();
    cfg.send_balance_changes = false;
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.on_balance_change(100.0, 50.0);  // 50% drop — clearly above threshold
    EXPECT_FALSE(coll.wait_for_count(1, std::chrono::milliseconds(100)));
}

// ===== Balance-change threshold =====

TEST(AlertManager, BalanceChangeBelowThresholdSuppressed) {
    PayloadCollector coll;
    auto cfg = with_webhook();
    cfg.balance_change_pct = 0.05;  // 5%
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.on_balance_change(100.0, 102.0);  // 2% move, below threshold
    EXPECT_FALSE(coll.wait_for_count(1, std::chrono::milliseconds(100)));
}

TEST(AlertManager, BalanceChangeAboveThresholdFires) {
    PayloadCollector coll;
    auto cfg = with_webhook();
    cfg.balance_change_pct = 0.05;
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.on_balance_change(100.0, 110.0);  // 10% up, above threshold
    ASSERT_TRUE(coll.wait_for_count(1, std::chrono::seconds(1)));
    EXPECT_EQ(coll.count(), 1u);
}

TEST(AlertManager, BalanceChangeWithNonPositiveOldBalanceSuppressed) {
    PayloadCollector coll;
    auto cfg = with_webhook();
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.on_balance_change(0.0, 10.0);
    mgr.on_balance_change(-5.0, 10.0);
    // Dividing by zero/negative is meaningless — suppress silently.
    EXPECT_FALSE(coll.wait_for_count(1, std::chrono::milliseconds(100)));
}

// ===== Throttle =====

TEST(AlertManager, BalanceChangeThrottleBlocksRepeatWithinWindow) {
    PayloadCollector coll;
    auto cfg = with_webhook();
    cfg.balance_change_pct = 0.05;
    cfg.throttle = std::chrono::seconds(60);  // 1-minute window
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.on_balance_change(100.0, 110.0);
    ASSERT_TRUE(coll.wait_for_count(1, std::chrono::seconds(1)));

    // Second call within the throttle window: suppressed.
    mgr.on_balance_change(110.0, 130.0);
    EXPECT_FALSE(coll.wait_for_count(2, std::chrono::milliseconds(100)));
    EXPECT_EQ(coll.count(), 1u);
}

// ===== Payload format =====

TEST(AlertManager, DiscordPayloadUsesContentField) {
    PayloadCollector coll;
    auto cfg = with_webhook(AlertManager::Format::Discord);
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.send("kill", "bang");
    ASSERT_TRUE(coll.wait_for_count(1, std::chrono::seconds(1)));

    auto payloads = coll.drain();
    auto j = nlohmann::json::parse(payloads.front());
    ASSERT_TRUE(j.contains("content"));
    auto content = j["content"].get<std::string>();
    EXPECT_NE(content.find("kill"), std::string::npos);
    EXPECT_NE(content.find("bang"), std::string::npos);
}

TEST(AlertManager, SlackPayloadUsesTextField) {
    PayloadCollector coll;
    auto cfg = with_webhook(AlertManager::Format::Slack);
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.send("kill", "bang");
    ASSERT_TRUE(coll.wait_for_count(1, std::chrono::seconds(1)));

    auto j = nlohmann::json::parse(coll.drain().front());
    ASSERT_TRUE(j.contains("text"));
    EXPECT_NE(j["text"].get<std::string>().find("kill"), std::string::npos);
}

TEST(AlertManager, GenericPayloadUsesTitleAndMessage) {
    PayloadCollector coll;
    auto cfg = with_webhook(AlertManager::Format::Generic);
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.send("kill", "bang");
    ASSERT_TRUE(coll.wait_for_count(1, std::chrono::seconds(1)));

    auto j = nlohmann::json::parse(coll.drain().front());
    EXPECT_EQ(j.value("title", ""), "kill");
    EXPECT_EQ(j.value("message", ""), "bang");
}

// ===== Kill switch and summary smoke =====

TEST(AlertManager, KillSwitchSendsWithReason) {
    PayloadCollector coll;
    auto cfg = with_webhook();
    AlertManager mgr(cfg);
    mgr.set_post_handler_for_tests(coll.handler());

    mgr.on_kill_switch("daily loss", 50.0, -30.0);
    ASSERT_TRUE(coll.wait_for_count(1, std::chrono::seconds(1)));
    auto payload = coll.drain().front();
    EXPECT_NE(payload.find("KILL SWITCH"), std::string::npos);
    EXPECT_NE(payload.find("daily loss"), std::string::npos);
}

TEST(AlertManager, ShutdownDrainsInFlightAndJoins) {
    // Destructor must join the worker cleanly even with queued work. If the
    // worker leaked or a cv.wait deadlocked, this test would hang gtest.
    PayloadCollector coll;
    auto cfg = with_webhook();
    {
        AlertManager mgr(cfg);
        mgr.set_post_handler_for_tests(coll.handler());
        for (int i = 0; i < 10; ++i) {
            mgr.send("title" + std::to_string(i), "msg");
        }
    }  // ~dtor waits for worker
    // At least the first few should have been processed before shutdown.
    EXPECT_GT(coll.count(), 0u);
}
