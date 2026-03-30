#pragma once

#include "strategy/kalshi/probability_engine.hpp"
#include "strategy/kalshi/econ_feed.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace trader::kalshi {

// CPI Model: Bottom-up estimation + Cleveland Fed Nowcast
class CpiModel : public IProbabilityModel {
public:
    // Set current CPI data from BLS
    void set_cpi_data(const std::vector<BlsDataPoint>& data) { cpi_data_ = data; }

    // Set Cleveland Fed Nowcast value (CPI YoY %)
    void set_nowcast(double nowcast_yoy) { nowcast_yoy_ = nowcast_yoy; }

    // Set latest gasoline price change (for energy component adjustment)
    void set_gasoline_change_pct(double pct) { gas_change_pct_ = pct; }

    Probability estimate(const std::string& ticker, double threshold) override;
    double confidence() const override { return confidence_; }
    std::string rationale() const override { return rationale_; }
    std::string category() const override { return "economics"; }

    // Gaussian CDF for probability calculation
    static double normal_cdf(double x);

    // Estimate CPI YoY from components
    double estimate_cpi_yoy() const;

private:
    std::vector<BlsDataPoint> cpi_data_;
    double nowcast_yoy_ = 0.0;         // Cleveland Fed Nowcast
    double gas_change_pct_ = 0.0;       // Recent gasoline price change
    double forecast_sigma_ = 0.15;      // Historical forecast error (calibrated)
    double confidence_ = 0.0;
    std::string rationale_;
};

// NFP Model: Leading indicators regression
class NfpModel : public IProbabilityModel {
public:
    void set_adp(double adp_thousands) { adp_ = adp_thousands; }
    void set_claims_trend(double trend) { claims_trend_ = trend; }
    void set_prior_nfp(double nfp) { prior_nfp_ = nfp; }

    Probability estimate(const std::string& ticker, double threshold) override;
    double confidence() const override { return confidence_; }
    std::string rationale() const override { return rationale_; }
    std::string category() const override { return "economics"; }

    // Simple regression estimate
    double estimate_nfp() const;

private:
    double adp_ = 0.0;
    double claims_trend_ = 0.0;
    double prior_nfp_ = 0.0;
    double forecast_sigma_ = 50.0;  // NFP forecast error in thousands
    double confidence_ = 0.0;
    std::string rationale_;
};

// Fed Rate Model: CME FedWatch proxy
class FedRateModel : public IProbabilityModel {
public:
    // Set the FedWatch-implied probability for the market's outcome
    void set_fedwatch_probability(double prob) { fedwatch_prob_ = prob; }

    Probability estimate(const std::string& ticker, double threshold) override;
    double confidence() const override { return confidence_; }
    std::string rationale() const override { return rationale_; }
    std::string category() const override { return "fed"; }

private:
    double fedwatch_prob_ = 0.5;
    double confidence_ = 0.0;
    std::string rationale_;
};

// Cross-market consistency checker
struct ConsistencyViolation {
    std::string market_higher;  // Should have higher probability
    std::string market_lower;   // Should have lower probability
    double price_higher = 0.0;
    double price_lower = 0.0;
    double violation = 0.0;     // How much the constraint is violated
};

class ConsistencyChecker {
public:
    // Check a group of related markets for monotonicity violations
    // e.g., P(temp > 80) should be <= P(temp > 75) should be <= P(temp > 70)
    static std::vector<ConsistencyViolation> check_monotonicity(
        const std::vector<std::pair<std::string, double>>& markets_ascending_threshold);
};

} // namespace trader::kalshi
