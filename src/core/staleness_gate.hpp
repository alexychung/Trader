#pragma once

#include "core/types.hpp"
#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>

namespace trader {

// Tracks freshness of per-market price updates and per-feed data signals.
// The strategy must consult this gate before generating a trade — stale data
// is the #1 source of unattended-bot losses.
class StalenessGate {
public:
    struct Config {
        std::chrono::seconds market_max_age{120};         // 2 min: WS book updates
        std::chrono::seconds default_feed_max_age{24*3600};
        // Per-feed overrides (key = feed name)
        std::unordered_map<std::string, std::chrono::seconds> feed_overrides;
    };

    explicit StalenessGate(Config config = {}) : config_(config) {}

    void on_market_update(const std::string& ticker);
    void on_feed_update(const std::string& feed);

    // Force a market or feed timestamp to a specific value (used at startup
    // when reloading from a known-good time).
    void seed_market(const std::string& ticker, Timestamp ts);
    void seed_feed(const std::string& feed, Timestamp ts);

    bool is_market_fresh(const std::string& ticker) const;
    bool is_feed_fresh(const std::string& feed) const;

    // Returns reason string if stale (empty if fresh).
    std::string stale_reason_market(const std::string& ticker) const;
    std::string stale_reason_feed(const std::string& feed) const;

    // Get last update times (for diagnostics / dashboard).
    std::unordered_map<std::string, Timestamp> last_market_updates() const;
    std::unordered_map<std::string, Timestamp> last_feed_updates() const;

    const Config& config() const { return config_; }

private:
    std::chrono::seconds feed_max_age(const std::string& feed) const;

    Config config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Timestamp> market_last_;
    std::unordered_map<std::string, Timestamp> feed_last_;
};

} // namespace trader
