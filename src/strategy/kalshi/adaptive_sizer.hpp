#pragma once

#include "core/types.hpp"
#include "strategy/kalshi/calibration.hpp"
#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>

namespace trader::kalshi {

// Per-category adaptive sizer. Translates raw Kelly fraction into a category-aware
// effective Kelly that:
//   - starts conservative until a category is "proven" (cold-start cap)
//   - rewards categories that realize predicted edge
//   - punishes categories with poor calibration (Brier high) or negative realized PnL
//   - auto-disables categories that look broken, with periodic micro-probes to allow recovery
//
// The sizer also enforces a global bankroll cap so a lucky streak doesn't escalate Kelly
// bets unboundedly.
class AdaptiveSizer {
public:
    struct Config {
        // Cold start: while resolved_real < cold_start_threshold, cap scale at cold_start_scale.
        int cold_start_threshold = 10;
        double cold_start_scale = 0.25;

        // Brier-based scaling: linear ramp between brier_full (full Kelly)
        // and brier_zero (zero Kelly → category disabled).
        //
        // Why these values:
        //
        //   brier_zero = 0.25. A degenerate predictor that always reports
        //   p=0.5 on binary outcomes gets Brier = (0.5 − outcome)² = 0.25
        //   regardless of the outcome distribution. So 0.25 = indistinguishable
        //   from a coin flip; anything at or above this threshold carries no
        //   information and should not size real positions.
        //
        //   brier_full = 0.15. Published calibration studies on operational
        //   ensemble forecasts (GFS/ECMWF ensemble daily-high > threshold,
        //   0–3 day horizon) land around 0.12–0.18 depending on the
        //   threshold's position relative to climatology. 0.15 sits
        //   comfortably in that range — a well-calibrated weather ensemble
        //   should hit it, and any category tracking at 0.15 is earning
        //   full Kelly sizing. Between 0.15 and 0.25 the scale ramps
        //   linearly toward zero so underperforming categories shrink
        //   gradually rather than binary-flipping.
        //
        // Review these if you ever add an econ/fed model: the same numeric
        // bands apply (Brier = 0.25 is still random for any binary outcome)
        // but the achievable brier_full floor may differ by category.
        double brier_full = 0.15;
        double brier_zero = 0.25;

        // Auto-disable: if (brier >= brier_zero OR realized_pnl < -|expected_edge|)
        // after >= disable_min_resolved samples, disable for disable_cooldown.
        int disable_min_resolved = 20;
        std::chrono::hours disable_cooldown{24 * 7};

        // Drift detection: compare a rolling-window Brier to the all-time Brier.
        // If the rolling window is >= drift_min_window samples AND rolling_brier
        // exceeds all-time by drift_threshold, the model is drifting. Fires
        // disable even if all-time looks fine — the most recent calibration is
        // what matters for the next trade.
        int rolling_window = 30;
        int drift_min_window = 15;
        double drift_threshold = 0.05;   // rolling_brier - all_time_brier

        // Bankroll cap: effective Kelly bankroll is min(current_balance, starting * bankroll_cap_mult).
        double bankroll_cap_mult = 1.5;

        // Exploration budget: fraction of bankroll allowed for tiny exploratory trades on
        // unproven or sub-edge opportunities.
        double exploration_budget_pct = 0.05;
        int exploration_max_qty = 1;

        // Micro-probe (auto-disable recovery): after disable_cooldown elapses, allow one probe trade.
        int probe_max_qty = 1;
    };

    explicit AdaptiveSizer(Config config = {}) : config_(config) {}

    // Compute effective Kelly fraction for a category given its aggregate stats and base Kelly.
    // Returns 0 if category is currently disabled.
    double effective_kelly(const std::string& category,
                            double base_kelly,
                            const CategoryAggregate& agg) const;

    // Compute effective bankroll = min(current, starting * bankroll_cap_mult).
    double effective_bankroll(double current_balance, double starting_bankroll) const;

    // Is this category currently auto-disabled?
    bool is_disabled(const std::string& category) const;

    // Should we run an exploratory probe in this disabled category right now?
    // (Uses last_probe_ time and config_.disable_cooldown.)
    bool should_probe(const std::string& category);

    // Record that we placed a real trade in this category (advances probe clock).
    void on_trade(const std::string& category);

    // Refresh per-category disable state from current aggregates.
    // Call every tick before sizing.
    void refresh(const CalibrationLogger& logger);

    // Exploration budget remaining today (in dollars).
    double exploration_remaining(double bankroll, double spent_today) const;

    // For diagnostics:
    struct CategoryState {
        bool disabled = false;
        Timestamp disabled_at;
        Timestamp last_probe;
        std::string disable_reason;
    };
    CategoryState state(const std::string& category) const;

    const Config& config() const { return config_; }

private:
    double brier_factor(double brier) const;
    double pnl_factor(const CategoryAggregate& agg) const;

    Config config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CategoryState> states_;
};

} // namespace trader::kalshi
