#include "risk/risk_manager.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace trader {

RiskManager::RiskManager(const RiskConfig& config)
    : config_(config), balance_(config.max_total_exposure + config.max_daily_loss),
      day_start_balance_(balance_) {}

TradeCheck RiskManager::check_trade(const std::string& ticker, int quantity, double price) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Sells (reducing positions) are always allowed, even when killed
    if (quantity < 0) {
        return {true, "OK (sell/reduce)"};
    }

    if (killed_) {
        return {false, "Kill switch active: " + kill_reason_};
    }

    // Check 1: per-market position limit
    auto it = positions_.find(ticker);
    int current_qty = (it != positions_.end()) ? it->second.quantity : 0;
    if (current_qty + quantity > config_.max_position_per_market) {
        return {false, "Per-market position limit exceeded (" +
                std::to_string(current_qty + quantity) + " > " +
                std::to_string(config_.max_position_per_market) + ")"};
    }

    // Check 2: total exposure
    double order_cost = price * quantity;
    double current_exposure = 0.0;
    for (const auto& [t, pos] : positions_) {
        if (!pos.settled) current_exposure += pos.cost;
    }
    if (current_exposure + order_cost > config_.max_total_exposure) {
        return {false, "Total exposure limit exceeded ($" +
                std::to_string(current_exposure + order_cost) + " > $" +
                std::to_string(config_.max_total_exposure) + ")"};
    }

    // Check 3: daily PnL
    if (daily_pnl_ < -config_.max_daily_loss) {
        return {false, "Daily loss limit exceeded ($" +
                std::to_string(-daily_pnl_) + " > $" +
                std::to_string(config_.max_daily_loss) + ")"};
    }

    // Check 4: active market count (using config max as 20)
    if (active_markets_.find(ticker) == active_markets_.end() &&
        static_cast<int>(active_markets_.size()) >= 20) {
        return {false, "Max active markets reached (20)"};
    }

    // Check 5: available capital (with reserve)
    double reserve = balance_ * config_.cash_reserve_pct;
    if (balance_ - current_exposure - order_cost < reserve) {
        return {false, "Insufficient capital after reserve"};
    }

    return {true, "OK"};
}

void RiskManager::on_fill(const std::string& ticker, const std::string& contract_side,
                           int quantity, double price) {
    // quantity > 0 = buy, quantity < 0 = sell
    std::lock_guard<std::mutex> lock(mutex_);

    auto& pos = positions_[ticker];
    pos.ticker = ticker;
    pos.contract_side = contract_side;

    if (quantity > 0) {
        // Buy: increase position, debit balance
        pos.quantity += quantity;
        pos.cost += price * quantity;
        balance_ -= price * quantity;
    } else {
        // Sell: reduce position, credit balance
        int reduce = -quantity;
        int actual_reduce = std::min(reduce, pos.quantity);
        if (pos.quantity > 0 && actual_reduce > 0) {
            double avg = pos.cost / pos.quantity;
            pos.quantity -= actual_reduce;
            pos.cost = pos.quantity * avg;
        }
        balance_ += price * reduce;
    }

    if (pos.quantity > 0) {
        active_markets_.insert(ticker);
    } else {
        active_markets_.erase(ticker);
    }
}

void RiskManager::on_settlement(const std::string& ticker, double pnl) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = positions_.find(ticker);
    if (it != positions_.end()) {
        it->second.settled = true;
        balance_ += it->second.cost + pnl;  // Return cost + profit/loss
        daily_pnl_ += pnl;
        active_markets_.erase(ticker);
    }

    // Check kill switch trigger
    if (daily_pnl_ <= -config_.kill_switch_loss) {
        killed_ = true;
        kill_reason_ = "Daily loss exceeded kill threshold ($" +
                       std::to_string(-daily_pnl_) + ")";
        spdlog::critical("KILL SWITCH: {}", kill_reason_);
    }
}

double RiskManager::total_exposure() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double total = 0.0;
    for (const auto& [t, pos] : positions_) {
        if (!pos.settled) total += pos.cost;
    }
    return total;
}

double RiskManager::available_capital() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double exposure = 0.0;
    for (const auto& [t, pos] : positions_) {
        if (!pos.settled) exposure += pos.cost;
    }
    return balance_ - exposure;  // Note: balance already reduced by fills
}

int RiskManager::active_market_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(active_markets_.size());
}

void RiskManager::trigger_kill(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    killed_ = true;
    kill_reason_ = reason;
    spdlog::critical("KILL SWITCH triggered: {}", reason);
}

void RiskManager::reset_kill() {
    std::lock_guard<std::mutex> lock(mutex_);
    killed_ = false;
    kill_reason_.clear();
    spdlog::info("Kill switch reset");
}

void RiskManager::reset_daily() {
    std::lock_guard<std::mutex> lock(mutex_);
    daily_pnl_ = 0.0;
    day_start_balance_ = balance_;
    spdlog::info("Daily risk counters reset. Balance: ${:.2f}", balance_);
}

} // namespace trader
