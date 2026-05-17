#pragma once

#include "core/types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace trader::kalshi {

// Per-series fee rates (matches KalshiRestClient::FeeOverride, intentionally
// decoupled from the REST type so the strategy tier has no REST dep).
struct SeriesFeeRates {
    double maker_rate = kKalshiDefaultMakerRate;
    double taker_rate = kKalshiDefaultTakerRate;
};

// Extract the series prefix from a Kalshi ticker: everything before the first
// `-`. "KXHIGHNY-26APR26-T75" → "KXHIGHNY", which is what Kalshi keys its
// /series/fee_changes overrides under.
inline std::string kalshi_series_from_ticker(const std::string& ticker) {
    auto dash = ticker.find('-');
    return dash == std::string::npos ? ticker : ticker.substr(0, dash);
}

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
        // Minimum edge before a trade is considered. The effective threshold
        // is max(min_edge, fee_floor), so this floor only binds when fees are
        // small (cheap contracts). At mid-price (~$0.50) the fee floor
        // dominates. 8¢ is enough to clear round-trip maker fees (~1.7¢) plus
        // the 1¢ safety buffer plus a small margin for adverse fills and
        // ensemble noise.
        double min_edge = 0.08;
        // Minimum model confidence. WeatherEnsembleModel scales confidence by
        // ensemble-member agreement × data completeness, so settled_max
        // overrides sit at ~0.95 and fresh full-ensemble predictions at
        // 0.3-0.7. Sub-0.3 signals are noise from sparse or conflicting
        // forecasts and should not size real positions.
        double min_confidence = 0.30;
        double kelly_fraction = 0.25;    // Conservative fractional Kelly
        int max_position = 10;           // Max contracts per market
        double bankroll = 100.0;
        double min_price = 0.15;         // Skip cheap contracts (favorite-longshot bias)
        double max_price = 0.85;
        // Assume early-exit is possible (order_manager repositions on >2¢ market
        // moves and force-exits on 10pp model flips). If true, gate on round-trip
        // fees — requires edge > 2 × maker_fee_fraction. If false (buy-and-hold
        // only), single entry fee suffices.
        bool assume_round_trip_fees = true;
        // Extra safety buffer added on top of the fee floor. Keeps us off the
        // break-even line; trades priced exactly at fee parity are noise, not edge.
        double fee_safety_buffer = 0.01;
        // Per-series fee overrides from Kalshi's /series/fee_changes.
        // Empty → every series uses the global default rates.
        std::unordered_map<std::string, SeriesFeeRates> fee_schedule;
    };

    explicit EdgeDetector(Config config = {}) : config_(config) {}

    // Rates for a specific ticker's series. Falls back to global defaults
    // when the series has no custom schedule.
    SeriesFeeRates rates_for(const std::string& ticker) const;

    // Detect edge for a single market.
    // kelly_scale lets the caller (AdaptiveSizer) scale Kelly per category.
    // 1.0 = unmodified base Kelly; 0.0 = don't trade.
    EdgeResult detect(const std::string& ticker, Probability model_prob,
                      double yes_bid, double yes_ask, double confidence,
                      double kelly_scale = 1.0);

    // Compute Kelly criterion for binary outcome
    // f* = (b * p - q) / b where b = (1-price)/price
    static double kelly_fraction(double model_prob, double market_price);

    // Position size: kelly * fraction * bankroll / price, clamped
    int position_size(double model_prob, double market_price, double kelly_scale = 1.0) const;

    // Fee-aware net expected value per contract.
    // round_trip=true subtracts entry + exit maker fees (matches order_manager
    // behavior — it can reprice / force-exit before settlement). Default false
    // preserves historic single-fee semantics for tests and pure hold-to-settle.
    // `maker_rate` overrides the global 0.0175 default when the series has a
    // custom schedule — use rates_for(ticker).maker_rate.
    static double net_ev_per_contract(double model_prob, double market_price,
                                      bool round_trip = false,
                                      double maker_rate = kKalshiDefaultMakerRate);

    // Fee-floor minimum edge: edge must exceed round-trip fees + safety buffer.
    // Expressed in probability points (e.g. 0.02 = 2¢ on a $1 contract).
    static double fee_floor_edge(double market_price, bool round_trip,
                                 double safety_buffer,
                                 double maker_rate = kKalshiDefaultMakerRate);

    void set_bankroll(double b) { config_.bankroll = b; }
    void set_fee_schedule(std::unordered_map<std::string, SeriesFeeRates> s) {
        config_.fee_schedule = std::move(s);
    }
    const Config& config() const { return config_; }

private:
    Config config_;
};

} // namespace trader::kalshi
