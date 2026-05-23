#pragma once

#include "core/types.hpp"
#include "core/config.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace trader {

struct RiskPosition {
    std::string ticker;
    std::string contract_side;
    int quantity = 0;
    double cost = 0.0;
    // Cumulative maker/taker fees paid to build/reduce this position.
    // RiskManager debits both `cost` AND `fees_paid` from balance at fill
    // time. On settlement we add back `cost + fees_paid` and then apply
    // the settled_pnl (which the exchange computes *including* fees) —
    // without tracking fees here, the fee was subtracted twice. See
    // hotfix-bugs #2.
    double fees_paid = 0.0;
    bool settled = false;
};

struct TradeCheck {
    bool allowed = false;
    std::string reason;
};

class RiskManager {
public:
    explicit RiskManager(const RiskConfig& config);

    // Pre-trade gate: ALL checks must pass
    TradeCheck check_trade(const std::string& ticker, int quantity, double price) const;

    // Position updates. `fee` is the maker/taker fee paid on this fill
    // (positive number, in dollars). Defaults to 0.0 for back-compat with
    // tests; production callers (KalshiExchange) always pass the real fee
    // so RiskManager's balance stays in sync with the exchange-side balance.
    // Without this, sizing math drifts upward by accumulated fees over the
    // course of a session.
    void on_fill(const std::string& ticker, const std::string& contract_side,
                 int quantity, double price, double fee = 0.0);
    void on_settlement(const std::string& ticker, double pnl);

    // State queries
    double total_exposure() const;
    double available_capital() const;
    double daily_pnl() const { return daily_pnl_; }
    int active_market_count() const;
    double balance() const { return balance_; }
    int position_quantity(const std::string& ticker) const;

    // Set/update balance
    void set_balance(double b) { balance_ = b; }
    void adjust_balance(double delta) { balance_ += delta; }

    // Kill switch state
    bool is_killed() const { return killed_; }
    void trigger_kill(const std::string& reason);
    void reset_kill();

    // Register a callback invoked whenever the kill flag flips from false to
    // true. Used by main() to bind KillSwitch::trigger so the switch sees the
    // event immediately rather than on the next tick-loop poll.
    void set_on_kill(std::function<void(const std::string&)> cb) {
        on_kill_ = std::move(cb);
    }

    // Daily reset (called at midnight UTC)
    void reset_daily();

    const RiskConfig& config() const { return config_; }

private:
    RiskConfig config_;
    double balance_ = 100.0;
    double daily_pnl_ = 0.0;
    double day_start_balance_ = 100.0;
    bool killed_ = false;
    std::string kill_reason_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RiskPosition> positions_;
    std::unordered_set<std::string> active_markets_;

    std::function<void(const std::string&)> on_kill_;
};

} // namespace trader
