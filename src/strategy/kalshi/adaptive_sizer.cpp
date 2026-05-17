#include "strategy/kalshi/adaptive_sizer.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace trader::kalshi {

double AdaptiveSizer::brier_factor(double brier) const {
    double range = config_.brier_zero - config_.brier_full;
    if (range <= 0) return 1.0;
    return std::clamp((config_.brier_zero - brier) / range, 0.0, 1.0);
}

double AdaptiveSizer::pnl_factor(const CategoryAggregate& agg) const {
    // If no real expected edge yet, neutral factor.
    if (std::abs(agg.expected_edge) < 1e-6) return 1.0;

    // Realization ratio: realized / expected. >= 1 means model delivers; < 0 means worse than nothing.
    double ratio = agg.realized_pnl / std::abs(agg.expected_edge);
    // Map ratio to factor: ratio<=-0.5 → 0; ratio>=0.5 → 1; linear between.
    double factor = (ratio + 0.5);   // -0.5→0, 0.5→1
    return std::clamp(factor, 0.0, 1.0);
}

double AdaptiveSizer::effective_bankroll(double current_balance, double starting_bankroll) const {
    double cap = starting_bankroll * config_.bankroll_cap_mult;
    return std::min(current_balance, cap);
}

double AdaptiveSizer::effective_kelly(const std::string& category,
                                       double base_kelly,
                                       const CategoryAggregate& agg) const {
    if (is_disabled(category)) return 0.0;

    // Cold start: small fixed cap until we've seen enough resolved real trades.
    if (agg.resolved_real < config_.cold_start_threshold) {
        return base_kelly * config_.cold_start_scale;
    }

    double bf = brier_factor(agg.brier);
    double pf = pnl_factor(agg);
    double scale = bf * pf;
    return base_kelly * scale;
}

void AdaptiveSizer::refresh(const CalibrationLogger& logger) {
    auto cats = logger.categories();
    auto now = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& cat : cats) {
        auto agg = logger.category_aggregate(cat, config_.rolling_window);
        auto& st = states_[cat];

        // If currently disabled, check if cooldown expired (allow probe).
        if (st.disabled) {
            auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - st.disabled_at);
            if (elapsed >= config_.disable_cooldown) {
                // Re-enable if calibration recovered — require BOTH all-time and
                // rolling Brier below the threshold, so a category that dropped
                // to good all-time-Brier but still has a bad recent tail stays
                // disabled. Shadows keep rolling Brier updated during disable.
                bool all_time_ok = (agg.brier < config_.brier_zero);
                bool rolling_ok  = (agg.rolling_window < config_.drift_min_window) ||
                                   (agg.rolling_brier < config_.brier_zero);
                bool pnl_ok      = (agg.realized_pnl >= -std::abs(agg.expected_edge));
                if (all_time_ok && rolling_ok && pnl_ok) {
                    st.disabled = false;
                    st.disable_reason.clear();
                    spdlog::info("AdaptiveSizer: category '{}' re-enabled "
                                 "(brier={:.3f}, rolling={:.3f} n={}, pnl=${:.2f})",
                                 cat, agg.brier, agg.rolling_brier,
                                 agg.rolling_window, agg.realized_pnl);
                }
            }
            continue;
        }

        // Disable check: enough samples + (bad brier OR drift OR realized PnL collapse).
        if (agg.resolved_real >= config_.disable_min_resolved) {
            bool bad_brier = (agg.brier >= config_.brier_zero);
            bool bad_pnl = (agg.expected_edge > 0) && (agg.realized_pnl < -agg.expected_edge);
            // Drift: rolling window is full enough AND has visibly worse Brier
            // than the all-time baseline. Catches models that were calibrated
            // on old regimes but broke under the current one.
            bool drifted = (agg.rolling_window >= config_.drift_min_window) &&
                           (agg.rolling_brier - agg.brier >= config_.drift_threshold) &&
                           (agg.rolling_brier >= config_.brier_zero);
            if (bad_brier || bad_pnl || drifted) {
                st.disabled = true;
                st.disabled_at = now;
                if (drifted && !bad_brier && !bad_pnl) {
                    st.disable_reason = "drift — rolling brier " +
                        std::to_string(agg.rolling_brier) +
                        " exceeds all-time " + std::to_string(agg.brier) +
                        " (window " + std::to_string(agg.rolling_window) + ")";
                } else if (bad_brier) {
                    st.disable_reason = "brier " + std::to_string(agg.brier) +
                        " >= " + std::to_string(config_.brier_zero);
                } else {
                    st.disable_reason = "realized PnL collapse vs expected edge";
                }
                spdlog::warn("AdaptiveSizer: category '{}' DISABLED — {}", cat, st.disable_reason);
            }
        }
    }
}

bool AdaptiveSizer::is_disabled(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(category);
    return it != states_.end() && it->second.disabled;
}

bool AdaptiveSizer::should_probe(const std::string& category) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(category);
    if (it == states_.end() || !it->second.disabled) return false;

    auto now = std::chrono::system_clock::now();
    auto since_probe = std::chrono::duration_cast<std::chrono::hours>(now - it->second.last_probe);
    if (since_probe >= config_.disable_cooldown) {
        it->second.last_probe = now;
        return true;
    }
    return false;
}

void AdaptiveSizer::on_trade(const std::string& category) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& st = states_[category];
    st.last_probe = std::chrono::system_clock::now();
}

double AdaptiveSizer::exploration_remaining(double bankroll, double spent_today) const {
    double budget = bankroll * config_.exploration_budget_pct;
    return std::max(0.0, budget - spent_today);
}

AdaptiveSizer::CategoryState AdaptiveSizer::state(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(category);
    if (it == states_.end()) return CategoryState{};
    return it->second;
}

} // namespace trader::kalshi
