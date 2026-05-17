#include "risk/kill_switch.hpp"
#include <spdlog/spdlog.h>

namespace trader {

KillSwitch::KillSwitch(RiskManager& risk, Config config)
    : risk_(risk), config_(config),
      last_heartbeat_(std::chrono::system_clock::now()) {}

bool KillSwitch::check() {
    if (active_) return true;

    // Check 1: Daily loss threshold
    if (risk_.daily_pnl() <= -config_.loss_threshold) {
        trigger("Daily loss exceeded threshold ($" +
                std::to_string(-risk_.daily_pnl()) + " >= $" +
                std::to_string(config_.loss_threshold) + ")");
        return true;
    }

    // Check 2: Connectivity timeout
    auto now = std::chrono::system_clock::now();
    auto since_heartbeat = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_heartbeat_);
    if (since_heartbeat > config_.connectivity_timeout) {
        trigger("Connectivity lost for " +
                std::to_string(since_heartbeat.count()) + " seconds");
        return true;
    }

    // Check 3: Consecutive order errors
    if (consecutive_errors_ >= config_.max_consecutive_errors) {
        trigger("Consecutive order errors: " + std::to_string(consecutive_errors_));
        return true;
    }

    // Check 4: Risk manager's own kill state
    if (risk_.is_killed()) {
        trigger("Risk manager triggered kill");
        return true;
    }

    return false;
}

void KillSwitch::trigger(const std::string& reason) {
    if (active_) return;  // Already triggered

    active_ = true;
    reason_ = reason;
    risk_.trigger_kill(reason);

    spdlog::critical("KILL SWITCH TRIGGERED: {}", reason);
    spdlog::critical("Snapshot - Balance: ${:.2f}, PnL: ${:.2f}, Exposure: ${:.2f}",
                     risk_.balance(), risk_.daily_pnl(), risk_.total_exposure());
}

void KillSwitch::reset() {
    active_ = false;
    reason_.clear();
    consecutive_errors_ = 0;
    risk_.reset_kill();
    spdlog::info("Kill switch reset");
}

void KillSwitch::on_heartbeat() {
    last_heartbeat_ = std::chrono::system_clock::now();
}

void KillSwitch::on_order_error() {
    ++consecutive_errors_;
}

void KillSwitch::on_order_success() {
    consecutive_errors_ = 0;
}

void KillSwitch::execute(IExchange& exchange) {
    spdlog::critical("KILL SWITCH: Cancelling all orders");
    // Don't flatten positions — binary contracts resolve on their own;
    // selling at a loss is usually worse than holding to settlement.
    if (!exchange.cancel_all_orders()) {
        spdlog::critical("KILL SWITCH: cancel_all_orders reported failures; "
                         "operator must inspect venue state manually");
    } else {
        spdlog::critical("KILL SWITCH: All orders cancelled. Manual restart required.");
    }
}

KillSwitch::StateSnapshot KillSwitch::snapshot() const {
    return {
        .balance = risk_.balance(),
        .daily_pnl = risk_.daily_pnl(),
        .exposure = risk_.total_exposure(),
        .active_markets = risk_.active_market_count(),
        .trigger_reason = reason_,
        .timestamp = std::chrono::system_clock::now()
    };
}

} // namespace trader
