#pragma once

#include "exchange/iexchange.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include "exchange/kalshi/ws_client.hpp"
#include "core/types.hpp"
#include <unordered_map>
#include <mutex>
#include <vector>

namespace trader::kalshi {

// Order tracking
struct TrackedOrder {
    OrderId id;
    std::string ticker;
    Side side = Side::Buy;
    std::string contract_side;
    double price = 0.0;
    int quantity = 0;
    int filled_quantity = 0;
    OrderStatus status = OrderStatus::Pending;
    double maker_fee = 0.0;
    Timestamp created_at;
};

// Position tracking per market
struct MarketPosition {
    std::string ticker;
    std::string contract_side;  // "yes" or "no"
    int quantity = 0;
    double avg_cost = 0.0;
    double total_cost = 0.0;    // sum of entry prices * quantities
    bool is_settled = false;
    bool outcome = false;
    double settled_pnl = 0.0;
};

// Settlement result
struct SettlementResult {
    std::string ticker;
    bool outcome = false;       // true = YES won
    double pnl = 0.0;
    int contracts = 0;
    double entry_cost = 0.0;
};

class KalshiExchange : public IExchange {
public:
    KalshiExchange(const std::string& rest_url, const std::string& ws_url, KalshiAuth& auth);

    // IExchange interface
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    OrderId place_order(const Order& order) override;
    bool cancel_order(const OrderId& id) override;
    double get_balance() const override;

    // Order tracking
    const TrackedOrder* get_order(const OrderId& id) const;
    std::vector<TrackedOrder> get_open_orders() const;
    bool cancel_all_orders();

    // Position tracking
    MarketPosition get_position(const std::string& ticker) const;
    std::vector<MarketPosition> get_all_positions() const;
    double total_exposure() const;

    // Settlement
    std::vector<SettlementResult> check_settlements();

    // Process a fill (from WS or polling)
    void on_fill(const WsFill& fill);

    // Process a settlement
    void on_settlement(const std::string& ticker, bool outcome);

    // Access to sub-clients
    KalshiRestClient& rest() { return rest_; }
    KalshiWsClient& ws() { return ws_; }

private:
    void update_position_on_fill(const std::string& ticker, const std::string& contract_side,
                                  double price, int quantity);

    KalshiAuth& auth_;
    KalshiRestClient rest_;
    KalshiWsClient ws_;

    mutable std::mutex orders_mutex_;
    std::unordered_map<OrderId, TrackedOrder> orders_;

    mutable std::mutex positions_mutex_;
    std::unordered_map<std::string, MarketPosition> positions_;

    mutable std::mutex balance_mutex_;
    double balance_ = 0.0;
};

} // namespace trader::kalshi
