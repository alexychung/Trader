#include "exchange/kalshi/kalshi_exchange.hpp"
#include "core/types.hpp"
#include "risk/risk_manager.hpp"
#include "risk/cluster_limiter.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace trader::kalshi {

KalshiExchange::KalshiExchange(const std::string& rest_url, const std::string& ws_url,
                                 KalshiAuth& auth)
    : auth_(auth), rest_(rest_url, auth), ws_(ws_url, auth) {

    // Wire up WS fill callback to update local state
    ws_.set_on_fill([this](const WsFill& fill) {
        on_fill(fill);
    });
}

KalshiExchange::KalshiExchange(const std::string& rest_url, const std::string& ws_url,
                                 KalshiAuth& auth, WsConfig ws_cfg)
    : auth_(auth), rest_(rest_url, auth), ws_(ws_url, auth, std::move(ws_cfg)) {

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
    // Shadow mode: simulate "instant fill at posted price". Without this,
    // returning empty (the original behavior) caused (1) main()'s caller to
    // treat empty IDs as kill-switch order errors, tripping after 3 ticks
    // of any persistent signal, and (2) the strategy's position-target
    // sizing to re-issue the same target every tick because no fill ever
    // updated risk_manager.position_quantity. The instant-fill simulation
    // is optimistic — a real post-only order may rest unfilled forever —
    // but it makes shadow runs observable and is the right baseline for
    // "what would this strategy DO over time" (the question shadow mode
    // exists to answer).
    if (shadow_mode_) {
        const bool effective_post_only = enforce_maker_only_ ? true : order.post_only;
        const OrderId synthetic_id = "shadow-" + std::to_string(++shadow_order_counter_);

        spdlog::info("SHADOW: would place {} {} {} {}x @ ${:.4f} post_only={} (id={})",
                     order.ticker, order.contract_side,
                     (order.side == Side::Buy) ? "buy" : "sell",
                     order.quantity, order.price,
                     effective_post_only, synthetic_id);

        // Register the order locally so get_open_orders / order_manager.tick
        // see it. Marking Filled immediately matches the synthesized fill
        // below — a Pending status would leave the order_manager re-checking
        // a never-resolving open order forever.
        {
            TrackedOrder tracked;
            tracked.id = synthetic_id;
            tracked.ticker = order.ticker;
            tracked.side = order.side;
            tracked.contract_side = order.contract_side;
            tracked.price = order.price;
            tracked.quantity = order.quantity;
            tracked.filled_quantity = order.quantity;
            tracked.status = OrderStatus::Filled;
            tracked.is_maker = effective_post_only;
            tracked.maker_fee = effective_post_only
                ? kalshi_maker_fee(order.quantity, order.price)
                : kalshi_taker_fee(order.quantity, order.price);
            tracked.created_at = std::chrono::system_clock::now();
            std::lock_guard<std::mutex> lock(orders_mutex_);
            orders_[synthetic_id] = tracked;
        }

        // Synthesize the fill. Reusing on_fill() routes the simulated trade
        // through every consumer that a real fill would touch — position
        // map, balance debit/credit, risk_manager, cluster_limiter, and the
        // last_fill_ts_ms_ watermark — so the rest of the system sees a
        // consistent picture without duplicate accounting paths.
        WsFill fake_fill;
        fake_fill.order_id = synthetic_id;
        fake_fill.trade_id = "shadow-trade-" + std::to_string(shadow_order_counter_);
        fake_fill.ticker = order.ticker;
        fake_fill.price = order.price;
        fake_fill.count = order.quantity;
        fake_fill.side = order.contract_side;
        fake_fill.action = (order.side == Side::Buy) ? "buy" : "sell";
        fake_fill.timestamp = std::chrono::system_clock::now();
        on_fill(fake_fill);

        return synthetic_id;
    }

    // When enforce_maker_only_ is on (default), every order is post-only
    // regardless of the caller's request. When off, honor Order.post_only —
    // the force-exit path in order_manager.cpp relies on this to actually
    // cross the book when the model flips against an open position.
    const bool effective_post_only = enforce_maker_only_ ? true : order.post_only;

    OrderId id = rest_.place_order(
        order.ticker, order.contract_side,
        (order.side == Side::Buy) ? "buy" : "sell",
        order.price, order.quantity, effective_post_only
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
        tracked.is_maker = effective_post_only;
        tracked.maker_fee = effective_post_only
            ? kalshi_maker_fee(order.quantity, order.price)
            : kalshi_taker_fee(order.quantity, order.price);
        tracked.created_at = std::chrono::system_clock::now();

        std::lock_guard<std::mutex> lock(orders_mutex_);
        orders_[id] = tracked;
    }

    return id;
}

bool KalshiExchange::cancel_order(const OrderId& id) {
    if (shadow_mode_) {
        // No real order to cancel — any id here was already synthetic.
        spdlog::debug("SHADOW: would cancel order {}", id);
        return true;
    }
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
    if (shadow_mode_) {
        spdlog::info("SHADOW: would cancel all orders");
        return true;
    }
    auto open = get_open_orders();
    if (open.empty()) return true;

    // Try batch first — ~5x cheaper on write-limit (0.2 tx/cancel).
    std::vector<OrderId> ids;
    ids.reserve(open.size());
    for (const auto& o : open) ids.push_back(o.id);

    // Build a fast lookup of server-confirmed cancels so we only mark those.
    // nullopt = endpoint unavailable → fall straight to serial.
    std::unordered_set<OrderId> confirmed_cancelled;
    auto batch_result = rest_.batch_cancel_orders(ids);
    if (batch_result.has_value()) {
        for (const auto& id : *batch_result) confirmed_cancelled.insert(id);
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            for (const auto& id : confirmed_cancelled) {
                auto it = orders_.find(id);
                if (it != orders_.end()) it->second.status = OrderStatus::Cancelled;
            }
        }
        if (confirmed_cancelled.size() == ids.size()) return true;
        spdlog::warn("batch cancel partial ({}/{}), retrying unconfirmed serially",
                     confirmed_cancelled.size(), ids.size());
    }

    // Serial fallback for endpoint-unavailable OR any id the server did not
    // explicitly confirm cancelled. Anything the serial cancel fails on
    // remains non-Cancelled locally, so the caller (kill switch) sees
    // a truthful failure and surfaces it to the operator.
    bool all_ok = true;
    for (const auto& order : open) {
        if (confirmed_cancelled.count(order.id)) continue;
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
    // Dedup: the same fill can arrive twice (WS replay after reconnect AND
    // REST reconcile). trade_id is server-unique per fill. Keep a bounded
    // history so long-running sessions don't grow the set unboundedly.
    constexpr std::size_t kTradeIdHistoryCap = 4096;
    if (!fill.trade_id.empty()) {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        if (seen_trade_ids_.count(fill.trade_id)) {
            spdlog::debug("Duplicate fill trade_id={}; ignoring", fill.trade_id);
            return;
        }
        seen_trade_ids_.insert(fill.trade_id);
        trade_id_insertion_.push_back(fill.trade_id);
        while (trade_id_insertion_.size() > kTradeIdHistoryCap) {
            seen_trade_ids_.erase(trade_id_insertion_.front());
            trade_id_insertion_.pop_front();
        }
    }

    spdlog::info("Fill: {} {} {} {}x @ ${:.4f}", fill.action, fill.side,
                 fill.ticker, fill.count, fill.price);

    // Look up the tracked order to recover the correct fee category. If the
    // order is unknown (reconnection race, or cross-process state loss),
    // default to maker — under the current policy every placed order is
    // post-only, so the real-world mismatch case is always maker. Log a
    // warning so operators can correlate any fee-accounting drift.
    bool was_maker = true;
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(fill.order_id);
        if (it != orders_.end()) {
            was_maker = it->second.is_maker;
            it->second.filled_quantity += fill.count;
            if (it->second.filled_quantity >= it->second.quantity) {
                it->second.status = OrderStatus::Filled;
            }
        } else {
            spdlog::warn("Fill for unknown order_id={}; defaulting to maker fee",
                         fill.order_id);
        }
    }

    // Update position and balance. Apply maker or taker fee based on how
    // the originating order was placed (post_only → maker). Force-exits
    // cross the book as takers; booking them as maker understates cost ~4x
    // on fills near $0.50.
    double fee = was_maker ? kalshi_maker_fee(fill.count, fill.price)
                            : kalshi_taker_fee(fill.count, fill.price);
    const bool is_buy = (fill.action == "buy");
    const int signed_qty = is_buy ? fill.count : -fill.count;
    {
        std::lock_guard<std::mutex> lock(balance_mutex_);
        update_position_on_fill(fill.ticker, fill.side, fill.price, signed_qty, fee);
        if (is_buy) {
            balance_ -= fill.price * fill.count + fee;  // Debit + fee
        } else {
            balance_ += fill.price * fill.count - fee;  // Credit - fee
        }
    }

    // Notify external consumers so their views stay in sync with the fill
    // stream. Without these, the pre-trade risk gate and cluster exposure
    // cap both operate on stale data after the first fill.
    if (risk_manager_) {
        // Pass `fee` so RiskManager's balance stays in lockstep with the
        // exchange-side balance. Without this, risk_manager.balance() drifts
        // upward by accumulated fees over a session — Kelly sizing overshoots.
        risk_manager_->on_fill(fill.ticker, fill.side, signed_qty, fill.price, fee);
    }
    if (cluster_limiter_) {
        // Buy adds cost; sell reduces cluster exposure by the same cash amount.
        const double cost_delta = is_buy ? (fill.price * fill.count)
                                          : -(fill.price * fill.count);
        cluster_limiter_->on_fill(fill.ticker, cost_delta);
    }

    // Bump the reconciliation watermark monotonically — callers persist
    // this and pass it back to reconcile_fills_since() after a restart so
    // the REST fill backfill knows where to resume.
    const int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        fill.timestamp.time_since_epoch()).count();
    if (ts_ms > 0) {
        int64_t prev = last_fill_ts_ms_.load();
        while (ts_ms > prev &&
               !last_fill_ts_ms_.compare_exchange_weak(prev, ts_ms)) {}
    }
}

int64_t KalshiExchange::reconcile_fills_since(int64_t last_seen_ts_ms) {
    auto fills = rest_.get_fills_since(last_seen_ts_ms);
    if (fills.empty()) {
        spdlog::info("Fill reconcile: no missed fills since ts={}", last_seen_ts_ms);
        return last_fill_ts_ms_.load();
    }
    spdlog::info("Fill reconcile: replaying {} missed fills", fills.size());
    for (const auto& f : fills) {
        WsFill ws;
        ws.order_id = f.order_id;
        ws.trade_id = f.trade_id;
        ws.ticker = f.ticker;
        ws.price = f.price;
        ws.count = f.count;
        ws.side = f.side;
        ws.action = f.action;
        ws.timestamp = Timestamp(std::chrono::milliseconds(f.created_ts_ms));
        on_fill(ws);  // dedups via trade_id
    }
    // on_fill debits balance for each reconciled fill — but if connect() just
    // fetched a fresh balance from Kalshi, those fills are already reflected
    // server-side and our debits double-count. Re-snap to the server value.
    // (Positions are still built correctly because update_position_on_fill
    // runs per-fill inside on_fill.)
    {
        std::lock_guard<std::mutex> lock(balance_mutex_);
        balance_ = rest_.get_balance();
    }
    spdlog::info("Fill reconcile: balance re-synced to Kalshi: ${:.2f}", get_balance());
    return last_fill_ts_ms_.load();
}

int KalshiExchange::seed_positions_from_rest() {
    auto positions = rest_.get_market_positions();
    int seeded = 0;
    for (const auto& p : positions) {
        if (p.position == 0) continue;

        // Kalshi's /portfolio/positions does not expose whether the holding
        // is on the YES or NO side of a market — it reports a signed share
        // count. Under our current strategy (buy-YES-only at entry), every
        // open position is YES. The day we start buying NO, this needs to
        // consult the event's market config or a richer REST endpoint.
        const std::string contract_side = "yes";

        // Compute implied average price so internal position cost math
        // matches what a replay would produce: cost / quantity.
        const double avg_price = (p.position > 0 && p.cost > 0.0)
            ? (p.cost / p.position) : 0.0;

        {
            std::lock_guard<std::mutex> lock(positions_mutex_);
            auto& pos = positions_[p.ticker];
            pos.ticker = p.ticker;
            pos.contract_side = contract_side;
            pos.quantity = p.position;
            pos.avg_cost = avg_price;
            pos.total_cost = p.cost;
        }

        if (risk_manager_) {
            // on_fill does the cost accounting; pass the average price and
            // the full share count so the derived cost matches Kalshi's
            // total_traded_dollars.
            risk_manager_->on_fill(p.ticker, contract_side, p.position, avg_price);
        }
        if (cluster_limiter_) {
            cluster_limiter_->on_fill(p.ticker, p.cost);
        }
        spdlog::info("Seeded position: {} {}x @ ${:.4f} (cost ${:.2f})",
                     p.ticker, p.position, avg_price, p.cost);
        ++seeded;
    }
    if (seeded == 0) {
        spdlog::info("Position seed: no open positions on Kalshi");
    } else {
        spdlog::info("Position seed: {} position(s) seeded from Kalshi", seeded);
    }
    return seeded;
}

void KalshiExchange::update_position_on_fill(const std::string& ticker,
                                                const std::string& contract_side,
                                                double price, int quantity,
                                                double fee) {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    auto& pos = positions_[ticker];
    pos.ticker = ticker;
    pos.contract_side = contract_side;

    // Accumulate fees so settled_pnl can subtract them later (fix #5).
    // Settlement is fee-free; this is entry-side only.
    pos.fees_paid += fee;

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
    double settled_pnl = 0.0;
    {
        std::lock_guard<std::mutex> lock(positions_mutex_);
        auto it = positions_.find(ticker);
        if (it == positions_.end()) return;

        auto& pos = it->second;
        pos.is_settled = true;
        pos.outcome = outcome;

        bool we_hold_yes = (pos.contract_side == "yes");
        bool yes_won = outcome;

        if ((we_hold_yes && yes_won) || (!we_hold_yes && !yes_won)) {
            // We win: receive $1.00 per contract. Net PnL subtracts entry
            // cost AND the cumulative fees we paid to build the position.
            // Without -fees_paid here, calibration logs (and reported PnL)
            // overstate profit by the fee total — see fix #5 (2026-05-20).
            pos.settled_pnl = (1.0 * pos.quantity) - pos.total_cost - pos.fees_paid;
            std::lock_guard<std::mutex> block(balance_mutex_);
            balance_ += 1.0 * pos.quantity;
        } else {
            // We lose: contracts worthless. PnL is the full cost AND fees.
            pos.settled_pnl = -pos.total_cost - pos.fees_paid;
        }
        settled_pnl = pos.settled_pnl;

        spdlog::info("Settlement {}: {} → PnL ${:.2f}", ticker,
                     outcome ? "YES" : "NO", pos.settled_pnl);
    }

    // Notify external consumers outside the positions_ lock to avoid holding
    // two locks when a consumer implementation takes its own.
    if (risk_manager_) risk_manager_->on_settlement(ticker, settled_pnl);
    if (cluster_limiter_) cluster_limiter_->on_settlement(ticker);
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
