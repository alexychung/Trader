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

    // Account
    virtual double get_balance() const = 0;
};

} // namespace trader
