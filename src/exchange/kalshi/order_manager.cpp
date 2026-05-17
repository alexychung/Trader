#include "exchange/kalshi/order_manager.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

namespace trader::kalshi {

OrderManager::OrderAction OrderManager::decide_order_action(
    const TrackedOrder& o, const MarketUpdate* latest,
    Timestamp now, const Config& cfg) {
    if (o.status == OrderStatus::Filled || o.status == OrderStatus::Cancelled) {
        return OrderAction::None;
    }

    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - o.created_at);
    if (age >= cfg.max_unfilled_age) return OrderAction::CancelStale;

    if (!latest) return OrderAction::None;

    // For a YES buy: we set price ~yes_bid; if yes_bid moves up away from our
    // price, reprice. For a NO buy: our price was 1 - yes_ask; if yes_ask
    // moves down (NO price up), reprice.
    if (o.contract_side == "yes") {
        if (latest->yes_bid > 0 &&
            std::abs(latest->yes_bid - o.price) > cfg.reprice_threshold) {
            return OrderAction::CancelReprice;
        }
    } else {  // "no"
        double current_no_price = 1.0 - latest->yes_ask;
        if (latest->yes_ask > 0 &&
            std::abs(current_no_price - o.price) > cfg.reprice_threshold) {
            return OrderAction::CancelReprice;
        }
    }
    return OrderAction::None;
}

OrderManager::ForceExitDecision OrderManager::decide_force_exit(
    const MarketPosition& pos, const MarketUpdate* latest,
    Probability current_prob, const Config& cfg) {
    ForceExitDecision d;
    if (pos.is_settled || pos.quantity <= 0 || !latest) return d;

    // Convert position-side prob: holding YES → P(YES), holding NO → P(NO).
    const double side_prob = (pos.contract_side == "yes") ? current_prob
                                                           : (1.0 - current_prob);
    // Price we could sell into right now (we take the bid on our side).
    const double side_market_bid = (pos.contract_side == "yes")
                                   ? latest->yes_bid
                                   : (1.0 - latest->yes_ask);

    // Condition 1: model says we're below the break-even win probability.
    // Above this threshold the position is still EV-positive to hold.
    if (side_prob >= cfg.force_exit_win_prob) return d;
    // Condition 2: selling into the book is materially +EV vs holding. The
    // signed gap side_market_bid - side_prob is the per-contract EV of
    // exiting vs settling. We require at least force_exit_prob_flip of
    // positive gap — otherwise exiting just locks in a loss the market has
    // already priced. This replaces the prior entry-vs-model comparison,
    // which didn't reference the current book at all.
    if ((side_market_bid - side_prob) < cfg.force_exit_prob_flip) return d;

    if (side_market_bid <= 0.01 || side_market_bid >= 0.99) return d;  // sanity

    d.should_exit = true;
    d.exit_price = side_market_bid;
    d.quantity = pos.quantity;
    return d;
}

OrderManager::TickStats OrderManager::tick(
    const std::unordered_map<std::string, MarketUpdate>& latest_updates,
    const std::unordered_map<std::string, Probability>& current_model_probs) {

    TickStats stats;
    auto now = std::chrono::system_clock::now();

    auto open = exchange_.get_open_orders();
    for (const auto& o : open) {
        const MarketUpdate* latest = nullptr;
        auto mu_it = latest_updates.find(o.ticker);
        if (mu_it != latest_updates.end()) latest = &mu_it->second;

        switch (decide_order_action(o, latest, now, config_)) {
            case OrderAction::CancelStale: {
                if (exchange_.cancel_order(o.id)) {
                    ++stats.cancelled_stale;
                    auto age = std::chrono::duration_cast<std::chrono::seconds>(
                        now - o.created_at);
                    spdlog::info("OrderManager: cancelled stale order {} ({} {} {}x @${:.4f}, age {}s)",
                                 o.id, o.ticker, o.contract_side, o.quantity, o.price, age.count());
                }
                break;
            }
            case OrderAction::CancelReprice: {
                if (exchange_.cancel_order(o.id)) {
                    ++stats.cancelled_repriced;
                    spdlog::info("OrderManager: cancelled for reprice {} ({} {} @${:.4f}, market moved)",
                                 o.id, o.ticker, o.contract_side, o.price);
                }
                break;
            }
            case OrderAction::None:
                break;
        }
    }

    // Force-exit pass: model flipped hard against an open position.
    auto positions = exchange_.get_all_positions();
    for (const auto& pos : positions) {
        auto pit = current_model_probs.find(pos.ticker);
        if (pit == current_model_probs.end()) continue;

        const MarketUpdate* latest = nullptr;
        auto mu_it = latest_updates.find(pos.ticker);
        if (mu_it != latest_updates.end()) latest = &mu_it->second;

        auto d = decide_force_exit(pos, latest, pit->second, config_);
        if (!d.should_exit) continue;

        Order exit_order;
        exit_order.ticker = pos.ticker;
        exit_order.side = Side::Sell;
        exit_order.contract_side = pos.contract_side;
        exit_order.price = d.exit_price;
        exit_order.quantity = d.quantity;
        exit_order.post_only = false;  // accept liquidity to actually exit
        ++stats.force_exit_attempts;
        spdlog::warn("OrderManager: force-exit {} {} {}x @${:.4f} (model flipped to {:.1f}%)",
                      pos.ticker, pos.contract_side, pos.quantity, d.exit_price,
                      pit->second * 100);
        exchange_.place_order(exit_order);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        cum_cancelled_stale_ += stats.cancelled_stale;
        cum_cancelled_repriced_ += stats.cancelled_repriced;
        cum_force_exits_ += stats.force_exit_attempts;
    }
    return stats;
}

int OrderManager::total_cancelled_stale() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cum_cancelled_stale_;
}
int OrderManager::total_cancelled_repriced() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cum_cancelled_repriced_;
}
int OrderManager::total_force_exits() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cum_force_exits_;
}

} // namespace trader::kalshi
