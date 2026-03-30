#pragma once

#include "core/types.hpp"
#include <vector>

namespace trader {

class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Event handlers
    virtual void on_market_update(const MarketUpdate& update) = 0;
    virtual void on_data_signal(const DataSignal& signal) = 0;
    virtual void on_fill(const Fill& fill) = 0;
    virtual void on_settlement(const Settlement& settlement) = 0;

    // Signal generation
    virtual std::vector<TradeSignal> generate_signals() = 0;
};

} // namespace trader
