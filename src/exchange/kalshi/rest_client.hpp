#pragma once

#include "core/types.hpp"
#include "exchange/kalshi/auth.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>

namespace trader::kalshi {

struct KalshiMarket {
    std::string ticker;
    std::string title;
    std::string category;
    std::string status;          // "open", "closed", "settled"
    double yes_bid = 0.0;
    double yes_ask = 0.0;
    double last_price = 0.0;
    int volume = 0;
    int open_interest = 0;
    std::string close_time;      // ISO 8601 string
    std::string expiration_time;
    std::string result;          // "yes", "no", "" (unsettled)
};

struct KalshiOrderbook {
    struct Level {
        double price;
        int quantity;
    };
    std::string ticker;
    std::vector<Level> yes_bids;  // ascending order, best bid last
    double best_yes_bid() const { return yes_bids.empty() ? 0.0 : yes_bids.back().price; }
    double best_yes_ask() const;  // derived: 1.0 - best_no_bid (if available) or from spread
};

// HTTP response wrapper
struct HttpResponse {
    int status_code = 0;
    std::string body;
    bool ok() const { return status_code >= 200 && status_code < 300; }
};

class KalshiRestClient {
public:
    KalshiRestClient(const std::string& base_url, KalshiAuth& auth);

    // Market data
    std::vector<KalshiMarket> get_markets(const std::string& category = "",
                                           const std::string& status = "open",
                                           int limit = 100);
    std::optional<KalshiMarket> get_market(const std::string& ticker);
    KalshiOrderbook get_orderbook(const std::string& ticker);

    // Account
    double get_balance();

    // Orders
    OrderId place_order(const std::string& ticker, const std::string& side,
                        const std::string& action, double price, int quantity,
                        bool post_only = true);
    bool cancel_order(const OrderId& order_id);

    // Raw HTTP (for testing/extension)
    HttpResponse get(const std::string& path);
    HttpResponse post(const std::string& path, const nlohmann::json& body);
    HttpResponse del(const std::string& path);

    // Market cache
    void refresh_market_cache(const std::string& category = "");
    const std::unordered_map<std::string, KalshiMarket>& cached_markets() const { return market_cache_; }

    // Parse Kalshi JSON responses
    static KalshiMarket parse_market(const nlohmann::json& j);

private:
    std::string base_url_;
    KalshiAuth& auth_;
    std::unordered_map<std::string, KalshiMarket> market_cache_;
    Timestamp cache_time_;
};

} // namespace trader::kalshi
