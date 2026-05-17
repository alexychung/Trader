#pragma once

#include "core/types.hpp"
#include "strategy/kalshi/calibration.hpp"
#include <string>
#include <unordered_map>
#include <mutex>

namespace trader::kalshi {

// Bayesian shrinkage of model probability toward market price.
//
//     adjusted = w * model_prob + (1 - w) * market_price
//
// w starts low ("trust the market") and grows as a category accumulates
// resolved samples *and* demonstrates good calibration (low Brier).
//
// **Disabled by default.** Stacking shrinkage on top of AdaptiveSizer's
// per-category Kelly scale AND the global fractional Kelly (0.25) produced
// ~1.25% of full Kelly during cold start — below the break-even edge floor
// for almost every market. One layer of uncertainty downweighting is the
// right call; we keep AdaptiveSizer + fractional Kelly and treat probability
// shrinkage as opt-in.
//
// Research §8.9: the textbook approach for small N is temperature scaling
// or Venn-Abers intervals, not a linear blend with the market price. The
// `w*raw + (1-w)*mid` formula is a Bayesian-flavored hack that happens to
// double-count the Kelly fraction when `p_model` and `market_price` disagree.
// Leave `enabled = false` until Venn-Abers lower bounds land (research §9.5),
// at which point credal Kelly replaces this mechanism entirely.
class ProbabilityCalibrator {
public:
    struct Config {
        // Master switch. Off by default — see class comment.
        bool enabled = false;
        // w = w_min + (w_max - w_min) * trust
        double w_min = 0.20;       // cold-start weight on model
        double w_max = 0.80;       // upper cap (never fully ignore market)
        int samples_for_full_trust = 50;  // resolved samples for samples_factor=1
        double brier_full_trust = 0.10;   // brier <= this => calibration_factor=1
        double brier_zero_trust = 0.25;   // brier >= this => calibration_factor=0
    };

    explicit ProbabilityCalibrator(Config config = {}) : config_(config) {}

    // Compute adjusted probability for a single signal.
    Probability calibrate(const std::string& category,
                          Probability raw_prob,
                          double market_price,
                          const CategoryAggregate& agg) const;

    // Compute trust weight w in [w_min, w_max] for a category.
    double trust_weight(const CategoryAggregate& agg) const;

    const Config& config() const { return config_; }

private:
    Config config_;
};

} // namespace trader::kalshi
