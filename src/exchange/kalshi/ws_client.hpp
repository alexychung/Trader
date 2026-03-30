#pragma once

#include "core/types.hpp"
#include "exchange/kalshi/auth.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>

namespace trader::kalshi {

// Parsed WebSocket messages
struct WsOrderbookDelta {
    std::string ticker;
    double yes_bid = 0.0;
    double yes_ask = 0.0;
    int yes_bid_size = 0;
    int yes_ask_size = 0;
};

struct WsTrade {
    std::string ticker;
    double price = 0.0;
    int count = 0;
    std::string side;  // "yes" or "no"
    Timestamp timestamp;
};

struct WsFill {
    std::string order_id;
    std::string ticker;
    double price = 0.0;
    int count = 0;
    std::string side;
    std::string action;  // "buy" or "sell"
};

struct WsMarketUpdate {
    std::string ticker;
    double yes_bid = 0.0;
    double yes_ask = 0.0;
    double last_price = 0.0;
    int volume = 0;
};

// Callbacks
using OnMarketUpdateCb = std::function<void(const WsMarketUpdate&)>;
using OnFillCb = std::function<void(const WsFill&)>;
using OnErrorCb = std::function<void(const std::string&)>;

class KalshiWsClient {
public:
    KalshiWsClient(const std::string& ws_url, KalshiAuth& auth);
    ~KalshiWsClient();

    // Lifecycle
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_.load(); }

    // Subscriptions
    void subscribe_orderbook(const std::string& ticker);
    void subscribe_ticker(const std::string& ticker);
    void subscribe_fills();
    void unsubscribe(const std::string& channel, const std::string& ticker = "");

    // Callbacks
    void set_on_market_update(OnMarketUpdateCb cb) { on_market_update_ = std::move(cb); }
    void set_on_fill(OnFillCb cb) { on_fill_ = std::move(cb); }
    void set_on_error(OnErrorCb cb) { on_error_ = std::move(cb); }

    // Local book state
    struct BookState {
        double yes_bid = 0.0;
        double yes_ask = 0.0;
        double last_price = 0.0;
        int volume = 0;
        Timestamp last_update;
    };

    BookState get_book_state(const std::string& ticker) const;
    std::unordered_map<std::string, BookState> all_book_states() const;

    // Message parsing (public for testing)
    static std::optional<WsMarketUpdate> parse_ticker_message(const nlohmann::json& j);
    static std::optional<WsOrderbookDelta> parse_orderbook_delta(const nlohmann::json& j);
    static std::optional<WsFill> parse_fill_message(const nlohmann::json& j);

    // Process a raw JSON message (public for testing)
    void process_message(const std::string& raw);

private:
    void send_subscribe(const std::string& channel, const std::vector<std::string>& tickers);
    void update_book_state(const std::string& ticker, double yes_bid, double yes_ask,
                           double last_price, int volume);

    std::string ws_url_;
    KalshiAuth& auth_;
    std::atomic<bool> connected_{false};

    mutable std::mutex book_mutex_;
    std::unordered_map<std::string, BookState> book_states_;

    OnMarketUpdateCb on_market_update_;
    OnFillCb on_fill_;
    OnErrorCb on_error_;
};

} // namespace trader::kalshi
