#include "strategy/kalshi/brier_aggregator.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace trader::kalshi {

namespace {

// Clamp to [0, 1]. A rogue model returning 1.5 would otherwise pass through
// into exp(−η(p−y)²) where (p−y)² can be arbitrarily large and wreck the
// log-sum-exp accumulator.
inline double clip01(double x) {
    return std::clamp(x, 0.0, 1.0);
}

}  // namespace

BrierAggregator::BrierAggregator(std::size_t K, double eta, double share)
    : K_(K), eta_(eta), share_(share), log_weights_(K, 0.0) {
    if (K_ == 0) throw std::invalid_argument("BrierAggregator: K must be > 0");
    if (eta_ <= 0.0) throw std::invalid_argument("BrierAggregator: eta must be > 0");
    if (share_ < 0.0 || share_ >= 1.0) {
        throw std::invalid_argument("BrierAggregator: share must be in [0, 1)");
    }
}

std::vector<double> BrierAggregator::normalized_weights_locked() const {
    // log-sum-exp for numerical stability. max-shift avoids overflow when
    // one log-weight is much larger than the others.
    double max_lw = *std::max_element(log_weights_.begin(), log_weights_.end());
    double denom = 0.0;
    for (double lw : log_weights_) denom += std::exp(lw - max_lw);
    std::vector<double> w(K_);
    for (std::size_t i = 0; i < K_; ++i) {
        w[i] = std::exp(log_weights_[i] - max_lw) / denom;
    }
    return w;
}

double BrierAggregator::predict(const std::vector<double>& expert_probs) {
    if (expert_probs.size() != K_) return 0.5;
    std::lock_guard<std::mutex> lock(mutex_);
    auto w = normalized_weights_locked();

    // Vovk 2009 AA substitution function for Brier loss:
    //   G(y) = -log( Σ_i w_i * exp(-η (p_i - y)²) ) / η
    //   p̂ = (1 + G(0) - G(1)) / 2, clipped to [0, 1].
    // (Derivation in Vovk 2009, §3; closed-form for Brier game at η ≤ 2.)
    double sum0 = 0.0, sum1 = 0.0;
    for (std::size_t i = 0; i < K_; ++i) {
        double p = clip01(expert_probs[i]);
        sum0 += w[i] * std::exp(-eta_ * p * p);
        sum1 += w[i] * std::exp(-eta_ * (p - 1.0) * (p - 1.0));
    }
    // Guard against catastrophic cancellation if a weight + expert combo
    // produces a near-zero sum. log(0) would be −inf.
    constexpr double kFloor = 1e-300;
    double G0 = -std::log(std::max(sum0, kFloor)) / eta_;
    double G1 = -std::log(std::max(sum1, kFloor)) / eta_;
    double p_hat = (1.0 + G0 - G1) / 2.0;
    return clip01(p_hat);
}

void BrierAggregator::observe(const std::vector<double>& expert_probs, int outcome) {
    if (expert_probs.size() != K_) return;
    if (outcome != 0 && outcome != 1) return;
    const double y = static_cast<double>(outcome);

    std::lock_guard<std::mutex> lock(mutex_);

    // Exponential weight update in log-space:
    //   w_i ← w_i * exp(-η * (p_i - y)²)
    //   log w_i ← log w_i - η * (p_i - y)²
    for (std::size_t i = 0; i < K_; ++i) {
        double p = clip01(expert_probs[i]);
        double loss = (p - y) * (p - y);
        log_weights_[i] -= eta_ * loss;
    }

    // Re-center to keep log-weights bounded (numerical hygiene; does not
    // change normalized weights).
    double max_lw = *std::max_element(log_weights_.begin(), log_weights_.end());
    for (auto& lw : log_weights_) lw -= max_lw;

    // Fixed-share mixing (Herbster-Warmuth 1998). After the exponential
    // update, blend each weight toward uniform by `share_`. Done in linear
    // space then re-logged — small overhead, avoids chasing the log-domain
    // formula for the convex combination.
    if (share_ > 0.0) {
        auto w_lin = normalized_weights_locked();
        const double uniform = 1.0 / static_cast<double>(K_);
        for (std::size_t i = 0; i < K_; ++i) {
            double mixed = (1.0 - share_) * w_lin[i] + share_ * uniform;
            log_weights_[i] = std::log(std::max(mixed, 1e-300));
        }
    }

    ++n_obs_;
}

std::vector<double> BrierAggregator::weights() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return normalized_weights_locked();
}

std::size_t BrierAggregator::num_observations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return n_obs_;
}

// ===== Registry =====

BrierAggregatorRegistry::BrierAggregatorRegistry(std::size_t K, double eta, double share)
    : K_(K), eta_(eta), share_(share) {}

BrierAggregator& BrierAggregatorRegistry::get(const std::string& context) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = aggregators_.find(context);
    if (it == aggregators_.end()) {
        auto [inserted, _] = aggregators_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(context),
            std::forward_as_tuple(K_, eta_, share_));
        return inserted->second;
    }
    return it->second;
}

std::vector<std::pair<std::string, std::vector<double>>>
BrierAggregatorRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::vector<double>>> out;
    out.reserve(aggregators_.size());
    for (const auto& [ctx, agg] : aggregators_) {
        out.emplace_back(ctx, agg.weights());
    }
    return out;
}

} // namespace trader::kalshi
