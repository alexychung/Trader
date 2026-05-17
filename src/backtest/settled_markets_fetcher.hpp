#pragma once

#include "core/types.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include <string>
#include <vector>

namespace trader::backtest {

struct SettledTrade {
    std::string ticker;
    double yes_price = 0.0;
    int count = 0;
    std::string taker_side;  // "yes" or "no"
    std::string created_at;  // ISO 8601
};

struct SettledMarket {
    trader::kalshi::KalshiMarket market;
    std::vector<SettledTrade> trades;  // ordered oldest-first
    bool outcome_yes = false;          // true = "yes", false = "no"
    std::string resolution_time;       // ISO 8601
};

// Fetches Kalshi markets that have settled and their per-ticker trade history.
// Caches responses to `cache_dir` so re-runs skip the API. Uses KalshiRestClient
// for auth-signed HTTPS.
class SettledMarketsFetcher {
public:
    SettledMarketsFetcher(trader::kalshi::KalshiRestClient& rest,
                          std::string cache_dir);

    // Fetch up to `max_markets` settled markets in [start_date, end_date]
    // (ISO 8601 YYYY-MM-DD). Category is optional (empty = all). Honors
    // on-disk cache keyed by query params.
    std::vector<trader::kalshi::KalshiMarket> fetch_settled_markets(
        const std::string& start_date,
        const std::string& end_date,
        const std::string& category = "",
        int max_markets = 200);

    // Fetch trade history for one ticker. Results sorted oldest-first. Cached.
    std::vector<SettledTrade> fetch_trades(const std::string& ticker,
                                             int max_trades = 1000);

    // Compose: settled markets + their trades + outcome. Applies cache.
    std::vector<SettledMarket> fetch_all(const std::string& start_date,
                                           const std::string& end_date,
                                           const std::string& category = "",
                                           int max_markets = 200);

    // Diagnostics.
    int cache_hits() const { return cache_hits_; }
    int cache_misses() const { return cache_misses_; }

private:
    trader::kalshi::KalshiRestClient& rest_;
    std::string cache_dir_;
    int cache_hits_ = 0;
    int cache_misses_ = 0;

    std::string markets_cache_path(const std::string& start_date,
                                    const std::string& end_date,
                                    const std::string& category) const;
    std::string trades_cache_path(const std::string& ticker) const;
};

} // namespace trader::backtest
