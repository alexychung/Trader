#pragma once

#include "risk/risk_manager.hpp"
#include "risk/kill_switch.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "exchange/kalshi/kalshi_exchange.hpp"
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <vector>

namespace trader {

class Dashboard {
public:
    Dashboard(const RiskManager& risk, const KillSwitch& kill,
              const kalshi::CalibrationLogger& calibration,
              const kalshi::KalshiExchange& exchange);

    // Render dashboard to string (for testing and display)
    std::string render() const;

    // Print to stdout
    void display() const;

    // Handle keyboard input (non-blocking) — returns action
    enum class Action { None, Quit, Kill, Refresh, CalibrationReport };

    // Set uptime start
    void set_start_time(Timestamp t) { start_time_ = t; }

    // Signal/trade counts (from strategy)
    void set_signal_count(int n) { signal_count_ = n; }
    void set_trade_count(int n) { trade_count_ = n; }

private:
    std::string format_uptime() const;
    std::string format_dollars(double v) const;

    const RiskManager& risk_;
    const KillSwitch& kill_;
    const kalshi::CalibrationLogger& calibration_;
    const kalshi::KalshiExchange& exchange_;
    Timestamp start_time_;
    int signal_count_ = 0;
    int trade_count_ = 0;
};

} // namespace trader
