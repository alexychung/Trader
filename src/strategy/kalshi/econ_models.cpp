#include "strategy/kalshi/econ_models.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace trader::kalshi {

// ===== Normal CDF (Abramowitz & Stegun approximation) =====
double CpiModel::normal_cdf(double x) {
    if (x < -8.0) return 0.0;
    if (x > 8.0) return 1.0;

    double t = 1.0 / (1.0 + 0.2316419 * std::abs(x));
    double d = 0.3989422804014327;  // 1/sqrt(2*pi)
    double p = d * std::exp(-0.5 * x * x) *
        (t * (0.319381530 + t * (-0.356563782 + t * (1.781477937 +
         t * (-1.821255978 + t * 1.330274429)))));

    return (x >= 0) ? (1.0 - p) : p;
}

// ===== CPI Model =====

double CpiModel::estimate_cpi_yoy() const {
    // Start with Cleveland Fed Nowcast as strong prior
    double estimate = nowcast_yoy_;

    // Adjust for latest energy prices (energy is ~7% of CPI)
    // If gas is up 5% since nowcast, CPI impact ≈ 0.07 * 5% = 0.35%
    estimate += 0.07 * gas_change_pct_;

    return estimate;
}

Probability CpiModel::estimate(const std::string& ticker, double threshold) {
    double cpi_estimate = estimate_cpi_yoy();

    if (cpi_estimate == 0.0 && cpi_data_.empty()) {
        confidence_ = 0.0;
        rationale_ = "No CPI data available";
        return 0.5;
    }

    // P(CPI YoY > threshold) = 1 - Φ((threshold - estimate) / sigma)
    double z = (threshold - cpi_estimate) / forecast_sigma_;
    double prob = 1.0 - normal_cdf(z);

    // Clamp to reasonable range
    prob = std::max(0.01, std::min(0.99, prob));

    confidence_ = (forecast_sigma_ > 0) ? std::min(1.0, 0.15 / forecast_sigma_) : 0.0;

    std::ostringstream oss;
    oss << "CPI YoY estimate: " << cpi_estimate << "%, threshold: " << threshold
        << "%, P(above): " << (prob * 100) << "% (sigma=" << forecast_sigma_ << ")";
    rationale_ = oss.str();

    return prob;
}

// ===== NFP Model =====

double NfpModel::estimate_nfp() const {
    // Simple weighted estimate from leading indicators
    // β₀ + β₁*ADP + β₂*claims_trend + β₃*prior_NFP
    // Coefficients calibrated from historical data
    double estimate = 50.0;  // baseline intercept (thousands)
    estimate += 0.6 * adp_;  // ADP is strongest single predictor
    estimate += -2.0 * claims_trend_;  // Rising claims = fewer jobs
    estimate += 0.2 * prior_nfp_;  // Momentum

    return estimate;
}

Probability NfpModel::estimate(const std::string& ticker, double threshold) {
    double nfp_estimate = estimate_nfp();

    if (adp_ == 0.0 && prior_nfp_ == 0.0) {
        confidence_ = 0.0;
        rationale_ = "No NFP input data available";
        return 0.5;
    }

    // P(NFP > threshold) = 1 - Φ((threshold - estimate) / sigma)
    double z = (threshold - nfp_estimate) / forecast_sigma_;
    double prob = 1.0 - CpiModel::normal_cdf(z);  // Reuse normal CDF
    prob = std::max(0.01, std::min(0.99, prob));

    confidence_ = (adp_ > 0) ? 0.6 : 0.3;  // Higher if ADP is available

    std::ostringstream oss;
    oss << "NFP estimate: " << nfp_estimate << "K, threshold: " << threshold
        << "K, P(above): " << (prob * 100) << "%";
    rationale_ = oss.str();

    return prob;
}

// ===== Fed Rate Model =====

Probability FedRateModel::estimate(const std::string& ticker, double threshold) {
    // FedWatch probability is our estimate
    // threshold is unused for binary Fed decisions (hold/cut/hike)
    double prob = fedwatch_prob_;
    prob = std::max(0.01, std::min(0.99, prob));

    confidence_ = 0.7;  // FedWatch is well-calibrated but Kalshi divergences can be persistent

    std::ostringstream oss;
    oss << "FedWatch implied: " << (fedwatch_prob_ * 100) << "% for " << ticker;
    rationale_ = oss.str();

    return prob;
}

// ===== Consistency Checker =====

std::vector<ConsistencyViolation> ConsistencyChecker::check_monotonicity(
    const std::vector<std::pair<std::string, double>>& markets_ascending_threshold) {

    std::vector<ConsistencyViolation> violations;

    // Markets with ascending thresholds should have descending prices
    // e.g., P(temp > 70) >= P(temp > 75) >= P(temp > 80)
    for (size_t i = 1; i < markets_ascending_threshold.size(); ++i) {
        const auto& [ticker_low, price_low] = markets_ascending_threshold[i - 1];
        const auto& [ticker_high, price_high] = markets_ascending_threshold[i];

        // price_low (lower threshold) should be >= price_high (higher threshold)
        if (price_high > price_low) {
            ConsistencyViolation v;
            v.market_higher = ticker_low;   // Should have higher probability
            v.market_lower = ticker_high;   // Should have lower probability
            v.price_higher = price_low;
            v.price_lower = price_high;
            v.violation = price_high - price_low;
            violations.push_back(v);
        }
    }

    return violations;
}

} // namespace trader::kalshi
