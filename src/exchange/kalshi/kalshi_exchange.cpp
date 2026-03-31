#include "exchange/kalshi/kalshi_exchange.hpp"
#include "core/types.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace trader::kalshi {

KalshiExchange::KalshiExchange(const std::string& rest_url, const std::string& ws_url,
                                 KalshiAuth& auth)
    : auth_(auth), rest_(rest_url, auth), ws_(ws_url, auth) {

    // Wire up WS fill callback to update local state
    ws_.set_on_fill([this](const WsFill& fill) {
        on_fill(fill);
    });
}

bool KalshiExchange::connect() {
    // Fetch initial balance
    {
        std::lock_guard<std::mutex> lock(balance_mutex_);
        balance_ = rest_.get_balance();
    }
    spdlog::info("KalshiExchange connected, balance: ${:.2f}", get_balance());
    return ws_.connect();
}

void KalshiExchange::disconnect() {
    // Cancel all open orders on disconnect (safety)
    cancel_all_orders();
    ws_.disconnect();
}

bool KalshiExchange::is_connected() const {
    return ws_.is_connected();
}

OrderId KalshiExchange::place_order(const Order& order) {
    // Always enforce maker-only
    OrderId id = rest_.place_order(
        order.ticker, order.contract_side,
        (order.side == Side::Buy) ? "buy" : "sell",
        order.price, order.quantity, true  // post_only = true always
    );

    if (!id.empty()) {
        TrackedOrder tracked;
        tracked.id = id;
        tracked.ticker = order.ticker;
        tracked.side = order.side;
        tracked.contract_side = order.contract_side;
        tracked.price = order.price;
        tracked.quantity = order.quantity;
        tracked.status = OrderStatus::Open;
        tracked.maker_fee = kalshi_maker_fee(order.quantity, order.price);
        tracked.created_at = std::chrono::system_clock::now();

        std::lock_guard<std::mutex> lock(orders_mutex_);
        orders_[id] = tracked;
    }

    return id;
}

bool KalshiExchange::cancel_order(const OrderId& id) {
    bool success = rest_.cancel_order(id);
    if (success) {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(id);
        if (it != orders_.end()) {
            it->second.status = OrderStatus::Cancelled;
        }
    }
    return success;
}

double KalshiExchange::get_balance() const {
    std::lock_guard<std::mutex> lock(balance_mutex_);
    return balance_;
}

const TrackedOrder* KalshiExchange::get_order(const OrderId& id) const {
    std::lock_guard<std::mutex> lock(orders_mutex_);
    auto it = orders_.find(id);
    if (it != orders_.end()) return &it->second;
    return nullptr;
}

std::vector<TrackedOrder> KalshiExchange::get_open_orders() const {
    std::lock_guard<std::mutex> lock(orders_mutex_);
    std::vector<TrackedOrder> open;
    for (const auto& [id, order] : orders_) {
        if (order.status == OrderStatus::Open || order.status == OrderStatus::Pending) {
            open.push_back(order);
        }
    }
    return open;
}

bool KalshiExchange::cancel_all_orders() {
    auto open = get_open_orders();
    bool all_ok = true;
    for (const auto& order : open) {
        if (!cancel_order(order.id)) {
            spdlog::warn("Failed to cancel order {}", order.id);
            all_ok = false;
        }
    }
    return all_ok;
}

MarketPosition KalshiExchange::get_position(const std::string& ticker) const {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    auto it = positions_.find(ticker);
    if (it != positions_.end()) return it->second;
    return MarketPosition{.ticker = ticker};
}

std::vector<MarketPosition> KalshiExchange::get_all_positions() const {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    std::vector<MarketPosition> result;
    for (const auto& [ticker, pos] : positions_) {
        if (pos.quantity > 0 && !pos.is_settled) {
            result.push_back(pos);
        }
    }
    return result;
}

double KalshiExchange::total_exposure() const {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    double total = 0.0;
    for (const auto& [ticker, pos] : positions_) {
        if (!pos.is_settled && pos.quantity > 0) {
            total += pos.total_cost;
        }
    }
    return total;
}

void KalshiExchange::on_fill(const WsFill& fill) {
    spdlog::info("Fill: {} {} {} {}x @ ${:.4f}", fill.action, fill.side,
                 fill.ticker, fill.count, fill.price);

    // Update order tracking
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(fill.order_id);
        if (it != orders_.end()) {
            it->second.filled_quantity += fill.count;
            if (it->second.filled_quantity >= it->second.quantity) {
                it->second.status = OrderStatus::Filled;
            }
        }
    }

    // Update position and balance (including maker fees)
    double fee = kalshi_maker_fee(fill.count, fill.price);
    {
        std::lock_guard<std::mutex> lock(balance_mutex_);
        if (fill.action == "buy") {
            update_position_on_fill(fill.ticker, fill.side, fill.price, fill.count);
            balance_ -= fill.price * fill.count + fee;  // Debit + fee
        } else {
            // Sell reduces position
            update_position_on_fill(fill.ticker, fill.side, fill.price, -fill.count);
            balance_ += fill.price * fill.count - fee;  // Credit - fee
        }
    }
}

void KalshiExchange::update_position_on_fill(const std::string& ticker,
                                                const std::string& contract_side,
                                                double price, int quantity) {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    auto& pos = positions_[ticker];
    pos.ticker = ticker;
    pos.contract_side = contract_side;

    if (quantity > 0) {
        // Adding to position
        pos.total_cost += price * quantity;
        pos.quantity += quantity;
        pos.avg_cost = pos.quantity > 0 ? pos.total_cost / pos.quantity : 0.0;
    } else {
        // Reducing position
        int reduce = -quantity;
        int actual_reduce = std::min(reduce, pos.quantity);
        pos.quantity -= actual_reduce;
        if (pos.quantity > 0) {
            pos.total_cost = pos.avg_cost * pos.quantity;
        } else {
            pos.total_cost = 0.0;
            pos.avg_cost = 0.0;
        }
    }
}

void KalshiExchange::on_settlement(const std::string& ticker, bool outcome) {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    auto it = positions_.find(ticker);
    if (it == positions_.end()) return;

    auto& pos = it->second;
    pos.is_settled = true;
    pos.outcome = outcome;

    bool we_hold_yes = (pos.contract_side == "yes");
    bool yes_won = outcome;

    if ((we_hold_yes && yes_won) || (!we_hold_yes && !yes_won)) {
        // We win: receive $1.00 per contract
        pos.settled_pnl = (1.0 * pos.quantity) - pos.total_cost;
        std::lock_guard<std::mutex> block(balance_mutex_);
        balance_ += 1.0 * pos.quantity;
    } else {
        // We lose: contracts worthless
        pos.settled_pnl = -pos.total_cost;
    }

    spdlog::info("Settlement {}: {} → PnL ${:.2f}", ticker,
                 outcome ? "YES" : "NO", pos.settled_pnl);
}

std::vector<SettlementResult> KalshiExchange::check_settlements() {
    // In production, this would poll GET /portfolio/settlements
    // For now, return settled positions
    std::lock_guard<std::mutex> lock(positions_mutex_);
    std::vector<SettlementResult> results;
    for (const auto& [ticker, pos] : positions_) {
        if (pos.is_settled) {
            results.push_back({
                .ticker = ticker,
                .outcome = pos.outcome,
                .pnl = pos.settled_pnl,
                .contracts = pos.quantity,
                .entry_cost = pos.total_cost
            });
        }
    }
    return results;
}

} // namespace trader::kalshi
