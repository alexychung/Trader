#include "exchange/mock_exchange.hpp"

namespace trader {

MockExchange::MockExchange(double initial_balance)
    : balance_(initial_balance), clock_(std::chrono::system_clock::now()) {}

bool MockExchange::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
    return true;
}

void MockExchange::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
}

bool MockExchange::is_connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

OrderId MockExchange::place_order(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    LoggedOrder entry;
    entry.id = "mock-" + std::to_string(next_id_++);
    entry.order = order;
    entry.placed_at = clock_;
    index_[entry.id] = orders_.size();
    orders_.push_back(std::move(entry));
    return orders_.back().id;
}

bool MockExchange::cancel_order(const OrderId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(id);
    if (it == index_.end()) return false;
    auto& entry = orders_[it->second];
    if (entry.canceled) return false;
    entry.canceled = true;
    return true;
}

double MockExchange::get_balance() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return balance_;
}

void MockExchange::set_clock(Timestamp now) {
    std::lock_guard<std::mutex> lock(mutex_);
    clock_ = now;
}

std::vector<MockExchange::LoggedOrder> MockExchange::orders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_;
}

std::size_t MockExchange::order_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_.size();
}

std::size_t MockExchange::canceled_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (const auto& o : orders_) if (o.canceled) ++n;
    return n;
}

void MockExchange::clear_orders() {
    std::lock_guard<std::mutex> lock(mutex_);
    orders_.clear();
    index_.clear();
    next_id_ = 1;
}

} // namespace trader
