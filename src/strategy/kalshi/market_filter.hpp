#pragma once

#include "exchange/kalshi/rest_client.hpp"
#include "strategy/kalshi/edge_detector.hpp"
#include <vector>
#include <string>
#include <unordered_set>

namespace trader::kalshi {

struct FilterConfig {
    std::unordered_set<std::string> allowed_categories = {"weather", "economics", "fed"};
    std::unordered_set<std::string> blocked_categories = {"sports", "crypto", "company"};
    int min_volume = 50;
    double min_spread = 0.03;
    double min_price = 0.15;    // Skip cheap contracts (favorite-longshot bias)
    double max_price = 0.85;
    int max_days_to_resolution_weather = 7;
    int max_days_to_resolution_econ = 30;
};

struct FilteredMarket {
    KalshiMarket market;
    double spread = 0.0;
    bool passes = false;
    std::string reject_reason;
};

class MarketFilter {
public:
    explicit MarketFilter(FilterConfig config = {}) : config_(config) {}

    // Filter a list of markets, returning those suitable for trading
    std::vector<FilteredMarket> filter(const std::vector<KalshiMarket>& markets) const;

    // Check a single market
    FilteredMarket check(const KalshiMarket& market) const;

    const FilterConfig& config() const { return config_; }

private:
    FilterConfig config_;
};

// Market making mode: quote both sides when spread is wide
struct MmQuote {
    std::string ticker;
    double bid_price = 0.0;    // Our buy price (YES side)
    double ask_price = 0.0;    // Our sell price (YES side)
    int quantity = 0;
    bool valid = false;
    std::string reason;
};

class MarketMaker {
public:
    struct Config {
        double min_spread_to_mm = 0.08;     // 8 cent minimum spread
        double min_confidence = 0.7;
        double half_spread_target = 0.03;   // 3 cents each side of model price
        int mm_quantity = 3;                // Contracts per side
    };

    explicit MarketMaker(Config config = {}) : config_(config) {}

    // Generate a two-sided quote if conditions are met
    MmQuote generate_quote(const std::string& ticker, Probability model_prob,
                           double yes_bid, double yes_ask, double confidence) const;

    const Config& config() const { return config_; }

private:
    Config config_;
};

} // namespace trader::kalshi
