#pragma once

#include "core/types.hpp"
#include <string>

namespace trader {

class IExchange {
public:
    virtual ~IExchange() = default;

    // Connection
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    // Orders
    virtual OrderId place_order(const Order& order) = 0;
    virtual bool cancel_order(const OrderId& id) = 0;

    // Cancel every open order at once. Implementations should use the venue's
    // batch cancel endpoint when available (Kalshi /portfolio/orders/batched
    // counts each cancel as 0.2 write-limit transactions — ~5x cheaper than
    // serial cancels during a kill-switch storm). Default falls back to
    // serial cancels of the venue's known open-order set.
    // Returns true iff every cancel succeeded.
    virtual bool cancel_all_orders() { return true; }

    // Account
    virtual double get_balance() const = 0;
};

} // namespace trader
