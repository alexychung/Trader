#include "strategy/kalshi/probability_calibrator.hpp"
#include <algorithm>

namespace trader::kalshi {

double ProbabilityCalibrator::trust_weight(const CategoryAggregate& agg) const {
    // Sample sufficiency: ramps from 0 (no data) to 1 (>=samples_for_full_trust).
    double samples_factor = std::min(1.0,
        static_cast<double>(agg.resolved_total) / static_cast<double>(config_.samples_for_full_trust));

    // Calibration quality: 1 at brier_full_trust, 0 at brier_zero_trust.
    double cal_range = config_.brier_zero_trust - config_.brier_full_trust;
    double cal_factor = (cal_range > 0)
        ? std::clamp((config_.brier_zero_trust - agg.brier) / cal_range, 0.0, 1.0)
        : 1.0;

    double trust = samples_factor * cal_factor;
    return config_.w_min + (config_.w_max - config_.w_min) * trust;
}

Probability ProbabilityCalibrator::calibrate(const std::string& /*category*/,
                                              Probability raw_prob,
                                              double market_price,
                                              const CategoryAggregate& agg) const {
    // Disabled path: pass raw_prob through untouched. AdaptiveSizer +
    // config.kelly_fraction own uncertainty scaling.
    if (!config_.enabled) {
        return std::clamp(static_cast<double>(raw_prob), 0.0, 1.0);
    }
    double w = trust_weight(agg);
    double adjusted = w * raw_prob + (1.0 - w) * market_price;
    return std::clamp(adjusted, 0.0, 1.0);
}

} // namespace trader::kalshi
