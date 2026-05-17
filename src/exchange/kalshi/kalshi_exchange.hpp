#pragma once

#include "exchange/iexchange.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include "exchange/kalshi/ws_client.hpp"
#include "core/types.hpp"
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

namespace trader {
class RiskManager;
class ClusterLimiter;
}

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
    double maker_fee = 0.0;  // legacy — kept for wire-format compat with reports
    // Whether the order was submitted post-only. Used by on_fill to book
    // the correct fee: post_only = maker (0.0175), else taker (0.07).
    // Mixing these up understates taker fees ~4x on force-exits.
    bool is_maker = true;
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
    KalshiExchange(const std::string& rest_url, const std::string& ws_url, KalshiAuth& auth,
                   WsConfig ws_cfg);

    // IExchange interface
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    OrderId place_order(const Order& order) override;
    bool cancel_order(const OrderId& id) override;
    bool cancel_all_orders() override;
    double get_balance() const override;

    // Order tracking
    const TrackedOrder* get_order(const OrderId& id) const;
    std::vector<TrackedOrder> get_open_orders() const;

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

    // Fetch any fills with server timestamp > `last_seen_ts_ms` via REST and
    // replay each through on_fill. Called after a WS (re)connect to close
    // the gap — Kalshi does not re-send missed WS fills on subscribe.
    // Returns the new watermark (max server ts seen), unchanged on empty.
    // Safe to call even when last_seen_ts_ms = 0 on first-ever run (with
    // the caveat that it will pull up to `max_pages` pages of history).
    int64_t reconcile_fills_since(int64_t last_seen_ts_ms);

    // Seed internal position state from Kalshi's /portfolio/positions.
    // Complements reconcile_fills_since() — that rebuilds positions from
    // fills since the watermark, which can miss positions acquired before
    // the bot's last known state (e.g., after manual cleanup, a crash that
    // lost state, or a brand-new install against a live account).
    // Calls risk_manager_->on_fill for each non-zero market position so the
    // pre-trade gate sees real exposure on the first tick. Returns the
    // number of positions seeded.
    int seed_positions_from_rest();

    // Current watermark — the server-timestamp of the most recent fill
    // processed, used to persist in PersistedState so reconcile_fills_since
    // picks up where we left off after a crash/restart.
    int64_t last_fill_ts_ms() const { return last_fill_ts_ms_.load(); }

    // Access to sub-clients
    KalshiRestClient& rest() { return rest_; }
    KalshiWsClient& ws() { return ws_; }

    // Wire external position consumers. Set once during startup; on_fill and
    // on_settlement will notify them so their views stay in sync with the
    // exchange's authoritative fill/settlement stream.
    void set_risk_manager(trader::RiskManager* rm) { risk_manager_ = rm; }
    void set_cluster_limiter(trader::ClusterLimiter* cl) { cluster_limiter_ = cl; }

    // When true (default), every order is submitted post-only regardless of
    // what the caller sets on Order.post_only. Set false to honor the
    // per-order flag — required for force-exits that need to cross.
    void set_enforce_maker_only(bool enforce) { enforce_maker_only_ = enforce; }

    // Shadow mode: the exchange accepts read APIs (balance, positions,
    // fills, settlements, market data) as normal but refuses every write.
    // place_order / cancel_order / cancel_all_orders log the intended action
    // and return safe no-op values. Intended for paper-trading against the
    // prod API without risking real money — everything the strategy would
    // have done is still calibration-logged (event_strategy writes records
    // at signal time, not fill time), so Brier tracking continues to run
    // against real prod book dynamics.
    void set_shadow_mode(bool on) { shadow_mode_ = on; }
    bool shadow_mode() const { return shadow_mode_; }

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

    // Non-owning — lifetimes managed by main().
    trader::RiskManager* risk_manager_ = nullptr;
    trader::ClusterLimiter* cluster_limiter_ = nullptr;

    // Default true matches the existing policy (all orders are makers).
    bool enforce_maker_only_ = true;

    // Default false preserves existing real-order behavior. Set via
    // set_shadow_mode() from main() when the session runs with --shadow.
    bool shadow_mode_ = false;

    // Counter for synthetic shadow order IDs (`shadow-1`, `shadow-2`, ...).
    // In shadow mode place_order returns a synthetic ID and immediately
    // synthesizes a fill so the kill switch sees `on_order_success` (not
    // `on_order_error` from an empty ID) and the strategy's position-target
    // sizing sees the held quantity (so it doesn't re-issue the same target
    // every tick). Without this, any persistent signal trips the kill
    // switch within 3 ticks of firing — verified live 2026-05-06.
    uint64_t shadow_order_counter_ = 0;

    // Monotonic watermark of the most-recent fill processed. Updated on
    // every on_fill; persisted via main()'s state save loop.
    std::atomic<int64_t> last_fill_ts_ms_{0};

    // Seen trade_ids (recent) for dedup across the REST-reconcile ↔ WS
    // boundary. Bounded size prevents unbounded growth. Access guarded
    // by orders_mutex_ for simplicity (low-frequency writes).
    std::unordered_set<std::string> seen_trade_ids_;
    std::deque<std::string> trade_id_insertion_;  // LRU-ish eviction queue
};

} // namespace trader::kalshi
