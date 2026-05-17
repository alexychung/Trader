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
    // Upper bound on spread. A bid=0.00/ask=1.00 market has spread=1.00 and
    // technically passes min_spread — but it's a placeholder book with no
    // liquidity, not a tradeable quote. Observed on demo: these "wide empty"
    // books dominated the post-filter set and produced no useful shadows.
    // Default 0.30 lets legitimate wide markets through (early-morning CPI
    // brackets run 15-25¢) but cuts the trivial 100¢ placeholders.
    double max_spread = 0.30;
    double min_price = 0.15;    // Skip cheap contracts (favorite-longshot bias)
    double max_price = 0.85;
    int max_days_to_resolution_weather = 7;
    int max_days_to_resolution_econ = 30;
    // Whether to skip markets with server-side fractional trading enabled.
    // Rationale for skipping: our strategy/position layer is int-quantity, so
    // a fractional fill (e.g. 10.50 contracts from a partial-match) silently
    // truncates on parse — balance drifts. Rationale for allowing: on demo
    // (and increasingly on prod), ALL weather markets are fractional-enabled;
    // skipping them loses the whole category. Safe middle ground: allow, but
    // we always submit whole-integer counts via count_fp="N.00", so the only
    // exposure is inbound fills — and our parse_kalshi_count_fp truncates
    // those toward zero, which costs at most 0.99 contracts of P&L per fill.
    // Default: allow; flip to true only if you observe P&L drift you can't
    // explain from fractional fills.
    bool skip_fractional_markets = false;
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
