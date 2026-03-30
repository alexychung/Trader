#pragma once

#include "risk/risk_manager.hpp"
#include "exchange/iexchange.hpp"
#include <atomic>
#include <string>
#include <functional>
#include <chrono>
#include <vector>

namespace trader {

class KillSwitch {
public:
    struct Config {
        double loss_threshold = 30.0;
        int max_consecutive_errors = 3;
        std::chrono::seconds connectivity_timeout{60};
    };

    KillSwitch(RiskManager& risk, Config config = {});

    // Check all kill conditions. Returns true if kill triggered.
    bool check();

    // Manual trigger
    void trigger(const std::string& reason);

    // Reset (requires manual action)
    void reset();

    bool is_active() const { return active_.load(); }
    std::string reason() const { return reason_; }

    // Track connectivity
    void on_heartbeat();
    void on_order_error();
    void on_order_success();

    // Execute kill: cancel all orders via exchange
    void execute(IExchange& exchange);

    // Log state snapshot for post-mortem
    struct StateSnapshot {
        double balance;
        double daily_pnl;
        double exposure;
        int active_markets;
        std::string trigger_reason;
        Timestamp timestamp;
    };

    StateSnapshot snapshot() const;

private:
    RiskManager& risk_;
    Config config_;
    std::atomic<bool> active_{false};
    std::string reason_;
    Timestamp last_heartbeat_;
    int consecutive_errors_ = 0;
};

} // namespace trader
