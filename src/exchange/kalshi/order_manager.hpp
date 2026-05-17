#pragma once

#include "core/types.hpp"
#include "exchange/kalshi/kalshi_exchange.hpp"
#include <chrono>
#include <unordered_map>
#include <string>
#include <mutex>

namespace trader::kalshi {

// Layered on top of KalshiExchange. Manages the post-order lifecycle:
//   - Cancel orders that haven't filled within `max_unfilled_age` (default 5min).
//   - Cancel + reprice orders if the market has moved away by `reprice_threshold`
//     (default 2¢) — the original limit price would just sit and rot.
//   - Force-exit (market sell) a position if the current model probability has
//     flipped strongly against the position (default: 10pp move toward losing side).
//
// All decisions are made on tick(). The strategy passes in the current market
// snapshot and the latest model probabilities per ticker.
class OrderManager {
public:
    struct Config {
        std::chrono::seconds max_unfilled_age{300};   // 5 minutes
        double reprice_threshold = 0.02;               // 2¢ move triggers reprice

        // Force-exit thresholds. A position exits only when BOTH:
        //   1. Current model win-probability falls below `force_exit_win_prob`
        //      (default 0.45 — we've become an underdog to settle profitably).
        //   2. The current market bid for our side exceeds the model's fair
        //      value by at least `force_exit_prob_flip` (default 0.10).
        //      This is the "selling is still +EV" guard: if the book already
        //      agrees with our bearish model, exiting realizes an unnecessary
        //      loss. The prior implementation compared entry price to model
        //      probability, which ignored where the market currently was and
        //      routinely force-sold into pessimistic books.
        double force_exit_win_prob = 0.45;
        double force_exit_prob_flip = 0.10;
    };

    struct TickStats {
        int cancelled_stale = 0;
        int cancelled_repriced = 0;
        int force_exit_attempts = 0;
    };

    // Pure decision helpers. Extracted from tick() so test code can drive
    // them without needing a KalshiExchange instance. tick() composes these
    // with the side-effecting cancel/place calls.
    enum class OrderAction {
        None,
        CancelStale,
        CancelReprice,
    };

    struct ForceExitDecision {
        bool should_exit = false;
        double exit_price = 0.0;
        int quantity = 0;
    };

    // Decide what to do with one open order given current market state and age.
    // `latest` may be null if no market snapshot has arrived for that ticker
    // yet (→ never triggers reprice; still returns CancelStale on age).
    static OrderAction decide_order_action(const TrackedOrder& order,
                                            const MarketUpdate* latest,
                                            Timestamp now,
                                            const Config& cfg);

    // Decide whether to force-exit an open position given the current model
    // probability. Returns a decision struct with `should_exit=false` when
    // we hold. `latest` required for the exit price — nullptr → no exit.
    static ForceExitDecision decide_force_exit(const MarketPosition& pos,
                                                 const MarketUpdate* latest,
                                                 Probability current_prob,
                                                 const Config& cfg);

    OrderManager(KalshiExchange& exchange, Config config = {})
        : exchange_(exchange), config_(config) {}

    // Run the lifecycle pass. Returns counters for this tick.
    TickStats tick(const std::unordered_map<std::string, MarketUpdate>& latest_updates,
                    const std::unordered_map<std::string, Probability>& current_model_probs);

    // Cumulative counters since startup.
    int total_cancelled_stale() const;
    int total_cancelled_repriced() const;
    int total_force_exits() const;

    const Config& config() const { return config_; }

private:
    KalshiExchange& exchange_;
    Config config_;

    mutable std::mutex mutex_;
    int cum_cancelled_stale_ = 0;
    int cum_cancelled_repriced_ = 0;
    int cum_force_exits_ = 0;
};

} // namespace trader::kalshi
