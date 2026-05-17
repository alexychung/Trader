#pragma once

#include "core/types.hpp"
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <chrono>
#include <functional>

namespace trader {

// Fire-and-forget alerting via a generic JSON webhook (Discord and Slack
// both accept the formats we send). Posts run on a background worker thread
// so that a slow webhook never blocks the trading loop.
//
// Configure with a webhook URL in YAML (alerts.webhook_url). Empty = silent.
//
// Alert categories:
//   - kill switch fired
//   - large balance change (> balance_change_pct since last summary)
//   - daily summary (once per UTC day)
//   - crash / panic
class AlertManager {
public:
    enum class Format { Discord, Slack, Generic };

    struct Config {
        std::string webhook_url;             // empty disables all alerts
        Format format = Format::Discord;     // Discord by default (most common for retail)
        bool send_kill_switch = true;
        bool send_daily_summary = true;
        bool send_balance_changes = true;
        double balance_change_pct = 0.05;    // alert when balance moves >= 5%
        std::chrono::seconds throttle{60};   // min interval between balance-change alerts
    };

    explicit AlertManager(Config config = {});
    ~AlertManager();

    // Test hook: when set, the worker calls this instead of posting HTTP.
    // Returns true if the payload was "accepted" (simulated 2xx). Install
    // before the worker dequeues (i.e. immediately after construction).
    // Not thread-safe — intended for unit tests that set once and drain.
    using PostHandler = std::function<bool(const std::string& payload)>;
    void set_post_handler_for_tests(PostHandler handler) {
        post_handler_ = std::move(handler);
    }

    AlertManager(const AlertManager&) = delete;
    AlertManager& operator=(const AlertManager&) = delete;

    bool enabled() const { return !config_.webhook_url.empty(); }

    // Generic alert.
    void send(const std::string& title, const std::string& message);

    // Convenience methods.
    void on_kill_switch(const std::string& reason, double balance, double daily_pnl);
    void on_balance_change(double old_balance, double new_balance);
    void daily_summary(double balance, double daily_pnl, int trades, double brier);
    void crash(const std::string& what);

    const Config& config() const { return config_; }

private:
    void worker_loop();
    bool post_payload(const std::string& payload);
    std::string build_payload(const std::string& title, const std::string& message);

    Config config_;
    PostHandler post_handler_;  // test-only override for post_payload
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::pair<std::string, std::string>> queue_;
    std::atomic<bool> stop_{false};
    Timestamp last_balance_alert_;
    double last_alerted_balance_ = 0.0;
};

} // namespace trader
