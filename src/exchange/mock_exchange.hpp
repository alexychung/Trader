#pragma once

#include "exchange/iexchange.hpp"
#include "core/types.hpp"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace trader {

// In-memory IExchange used by the backtest harness. `place_order` records the
// order and immediately returns a synthetic order id; it does not simulate
// fills. The backtest treats every placed order as a hypothetical entry at its
// quoted price, and the CalibrationLogger scores the prediction against the
// settled outcome. Fill realism is out of scope until tick data exists.
class MockExchange : public IExchange {
public:
    struct LoggedOrder {
        OrderId id;
        Order order;
        Timestamp placed_at;
        bool canceled = false;
    };

    explicit MockExchange(double initial_balance = 100.0);

    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    OrderId place_order(const Order& order) override;
    bool cancel_order(const OrderId& id) override;
    double get_balance() const override;

    // Set the time returned to synthetic orders (so decision_time comes from
    // the backtest clock, not wall-clock).
    void set_clock(Timestamp now);

    // Diagnostics / assertions.
    std::vector<LoggedOrder> orders() const;
    std::size_t order_count() const;
    std::size_t canceled_count() const;
    void clear_orders();

private:
    mutable std::mutex mutex_;
    double balance_ = 0.0;
    bool connected_ = false;
    Timestamp clock_;
    std::uint64_t next_id_ = 1;
    std::vector<LoggedOrder> orders_;
    std::unordered_map<OrderId, std::size_t> index_;
};

} // namespace trader
