#pragma once

#include "core/types.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace trader::kalshi {

struct EdgeResult {
    std::string ticker;
    Side side = Side::Buy;
    std::string contract_side;  // "yes" or "no"
    Probability model_prob = 0.0;
    double market_price = 0.0;
    double edge = 0.0;          // model_prob - market_price (for yes side)
    double net_ev = 0.0;        // EV after fees
    double confidence = 0.0;
    int kelly_quantity = 0;
    std::string rationale;
};

class EdgeDetector {
public:
    struct Config {
        double min_edge = 0.05;          // 5 cent minimum edge
        double min_confidence = 0.5;
        double kelly_fraction = 0.25;    // Conservative fractional Kelly
        int max_position = 10;           // Max contracts per market
        double bankroll = 100.0;
        double min_price = 0.15;         // Skip cheap contracts (favorite-longshot bias)
        double max_price = 0.85;
    };

    explicit EdgeDetector(Config config = {}) : config_(config) {}

    // Detect edge for a single market
    EdgeResult detect(const std::string& ticker, Probability model_prob,
                      double yes_bid, double yes_ask, double confidence);

    // Compute Kelly criterion for binary outcome
    // f* = (b * p - q) / b where b = (1-price)/price
    static double kelly_fraction(double model_prob, double market_price);

    // Position size: kelly * fraction * bankroll / price, clamped
    int position_size(double model_prob, double market_price) const;

    // Fee-aware net expected value per contract
    static double net_ev_per_contract(double model_prob, double market_price);

    void set_bankroll(double b) { config_.bankroll = b; }
    const Config& config() const { return config_; }

private:
    Config config_;
};

} // namespace trader::kalshi
