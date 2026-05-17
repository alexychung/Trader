#pragma once

#include <cmath>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace trader::kalshi {

// Online aggregator for K binary-probability experts, loss = Brier. Uses
// Vovk's Aggregating Algorithm (AA) with the closed-form substitution
// function for Brier loss (Vovk 2009, "Prediction with Expert Advice for
// the Brier Game", JMLR). Adds fixed-share mixing (Herbster & Warmuth
// 1998) to prevent winner-takes-all weight collapse — when one expert's
// cumulative loss dominates, the aggregator otherwise becomes brittle to
// regime changes (e.g. a weather model upgrade mid-season pushes an
// "old winner" into a losing regime it can never recover from).
//
// Why AA not vanilla Hedge: Brier loss is η-mixable for η ≤ 2, so the AA
// achieves constant regret (≤ ln K / η Brier units) independent of horizon
// T. Vanilla Hedge on Brier only gets O(sqrt(T ln K)); at K=2, T=100 the
// delta is ~5 Brier units worst case.
//
// Usage:
//   BrierAggregator agg(2, 2.0, 0.01);   // 2 experts, eta=2, share=0.01
//   double p_hat = agg.predict({p_gfs, p_ecmwf});  // aggregate prediction
//   // ... place trade, wait for settlement ...
//   agg.observe({p_gfs, p_ecmwf}, outcome);         // outcome ∈ {0, 1}
//
// Thread-safe: all public methods hold a mutex. Low-frequency usage.
class BrierAggregator {
public:
    // K       — number of experts (fixed at construction).
    // eta     — learning rate. Vovk's mixability bound requires eta ≤ 2 for
    //           Brier loss; eta = 2.0 is the regret-optimal constant.
    // share   — fixed-share mix-in fraction per update. 0 disables (pure AA),
    //           higher values track regime changes faster at the cost of
    //           asymptotic regret. Herbster & Warmuth 1998 suggest ~1/T for
    //           horizon T; ε=0.01 is a reasonable default for 100-step horizons.
    explicit BrierAggregator(std::size_t K,
                              double eta = 2.0,
                              double share = 0.01);

    // Produce the aggregate probability given each expert's prediction.
    // `expert_probs.size()` must equal K. Inputs are clamped to [0,1] before
    // mixing (defensive — a rogue model returning 1.5 would blow up the
    // exp(−η(p−y)²) calculation below).
    double predict(const std::vector<double>& expert_probs);

    // Update weights after observing the binary outcome. Must be called AT
    // MOST ONCE per (market, prediction-round). Applies fixed-share mixing
    // after the exponential update. No-op if expert_probs.size() != K.
    void observe(const std::vector<double>& expert_probs, int outcome);

    // Current normalized weights (diagnostics). Length K, sums to 1.
    std::vector<double> weights() const;

    // Total observations seen (diagnostic).
    std::size_t num_observations() const;

    std::size_t K() const { return K_; }
    double eta() const { return eta_; }
    double share() const { return share_; }

private:
    // Stable log-space normalization. Returns normalized linear weights.
    std::vector<double> normalized_weights_locked() const;

    std::size_t K_;
    double eta_;
    double share_;

    // Log-weights, unnormalized. Updated additively on each observe() to
    // avoid underflow as cumulative loss grows.
    std::vector<double> log_weights_;
    std::size_t n_obs_ = 0;

    mutable std::mutex mutex_;
};

// Per-context aggregator registry. Several markets may want their own
// independent aggregator (e.g., one per category). Lazy-created on first
// use; lifetime owned by the registry.
class BrierAggregatorRegistry {
public:
    // K and default config come from the registry; each context re-uses them.
    BrierAggregatorRegistry(std::size_t K, double eta = 2.0, double share = 0.01);

    BrierAggregator& get(const std::string& context);

    // For diagnostics / persistence (read all contexts + their weights).
    std::vector<std::pair<std::string, std::vector<double>>> snapshot() const;

private:
    std::size_t K_;
    double eta_;
    double share_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, BrierAggregator> aggregators_;
};

} // namespace trader::kalshi
