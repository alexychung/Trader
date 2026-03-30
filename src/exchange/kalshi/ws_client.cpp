#include "exchange/kalshi/ws_client.hpp"
#include <spdlog/spdlog.h>

namespace trader::kalshi {

KalshiWsClient::KalshiWsClient(const std::string& ws_url, KalshiAuth& auth)
    : ws_url_(ws_url), auth_(auth) {}

KalshiWsClient::~KalshiWsClient() {
    disconnect();
}

bool KalshiWsClient::connect() {
    // Full Boost.Beast WebSocket + SSL connection will be implemented
    // when integration testing against demo API.
    // For now, the message parsing and book state logic is fully functional
    // and testable without a live connection.
    spdlog::info("KalshiWsClient: connect() called for {}", ws_url_);
    connected_ = true;
    return true;
}

void KalshiWsClient::disconnect() {
    if (connected_) {
        spdlog::info("KalshiWsClient: disconnecting");
        connected_ = false;
    }
}

void KalshiWsClient::subscribe_orderbook(const std::string& ticker) {
    send_subscribe("orderbook_delta", {ticker});
}

void KalshiWsClient::subscribe_ticker(const std::string& ticker) {
    send_subscribe("ticker", {ticker});
}

void KalshiWsClient::subscribe_fills() {
    send_subscribe("fill", {});
}

void KalshiWsClient::unsubscribe(const std::string& channel, const std::string& ticker) {
    spdlog::debug("Unsubscribe from {} {}", channel, ticker);
}

void KalshiWsClient::send_subscribe(const std::string& channel,
                                      const std::vector<std::string>& tickers) {
    nlohmann::json msg;
    msg["id"] = 1;
    msg["cmd"] = "subscribe";
    msg["params"] = {{"channels", {channel}}};
    if (!tickers.empty()) {
        msg["params"]["market_tickers"] = tickers;
    }
    spdlog::debug("Subscribe: {}", msg.dump());
    // Actual WebSocket send will happen when connected to real endpoint
}

KalshiWsClient::BookState KalshiWsClient::get_book_state(const std::string& ticker) const {
    std::lock_guard<std::mutex> lock(book_mutex_);
    auto it = book_states_.find(ticker);
    if (it != book_states_.end()) return it->second;
    return {};
}

std::unordered_map<std::string, KalshiWsClient::BookState> KalshiWsClient::all_book_states() const {
    std::lock_guard<std::mutex> lock(book_mutex_);
    return book_states_;
}

void KalshiWsClient::update_book_state(const std::string& ticker, double yes_bid,
                                         double yes_ask, double last_price, int volume) {
    std::lock_guard<std::mutex> lock(book_mutex_);
    auto& state = book_states_[ticker];
    if (yes_bid > 0) state.yes_bid = yes_bid;
    if (yes_ask > 0) state.yes_ask = yes_ask;
    if (last_price > 0) state.last_price = last_price;
    if (volume > 0) state.volume = volume;
    state.last_update = std::chrono::system_clock::now();
}

// Parse ticker channel message
std::optional<WsMarketUpdate> KalshiWsClient::parse_ticker_message(const nlohmann::json& j) {
    try {
        WsMarketUpdate update;
        if (j.contains("market_ticker")) {
            update.ticker = j["market_ticker"].get<std::string>();
        } else if (j.contains("ticker")) {
            update.ticker = j["ticker"].get<std::string>();
        } else {
            return std::nullopt;
        }

        auto get_price = [&](const std::string& key) -> double {
            if (!j.contains(key)) return 0.0;
            if (j[key].is_string()) return std::stod(j[key].get<std::string>());
            if (j[key].is_number()) return j[key].get<double>();
            return 0.0;
        };

        update.yes_bid = get_price("yes_bid");
        update.yes_ask = get_price("yes_ask");
        update.last_price = get_price("last_price");
        update.volume = j.value("volume", 0);

        return update;
    } catch (...) {
        return std::nullopt;
    }
}

// Parse orderbook delta message
std::optional<WsOrderbookDelta> KalshiWsClient::parse_orderbook_delta(const nlohmann::json& j) {
    try {
        WsOrderbookDelta delta;
        if (j.contains("market_ticker")) {
            delta.ticker = j["market_ticker"].get<std::string>();
        } else {
            return std::nullopt;
        }

        auto get_price = [&](const std::string& key) -> double {
            if (!j.contains(key)) return 0.0;
            if (j[key].is_string()) return std::stod(j[key].get<std::string>());
            if (j[key].is_number()) return j[key].get<double>();
            return 0.0;
        };

        delta.yes_bid = get_price("yes_bid");
        delta.yes_ask = get_price("yes_ask");
        delta.yes_bid_size = j.value("yes_bid_size", 0);
        delta.yes_ask_size = j.value("yes_ask_size", 0);

        return delta;
    } catch (...) {
        return std::nullopt;
    }
}

// Parse fill message
std::optional<WsFill> KalshiWsClient::parse_fill_message(const nlohmann::json& j) {
    try {
        WsFill fill;
        fill.order_id = j.value("order_id", "");
        fill.ticker = j.value("ticker", "");

        if (j.contains("price") && j["price"].is_string()) {
            fill.price = std::stod(j["price"].get<std::string>());
        } else {
            fill.price = j.value("price", 0.0);
        }

        fill.count = j.value("count", 0);
        fill.side = j.value("side", "");
        fill.action = j.value("action", "");

        if (fill.order_id.empty() && fill.ticker.empty()) return std::nullopt;
        return fill;
    } catch (...) {
        return std::nullopt;
    }
}

// Process a raw WebSocket message
void KalshiWsClient::process_message(const std::string& raw) {
    try {
        auto j = nlohmann::json::parse(raw);

        std::string type;
        if (j.contains("type")) type = j["type"].get<std::string>();
        else if (j.contains("channel")) type = j["channel"].get<std::string>();

        nlohmann::json data = j.contains("msg") ? j["msg"] : j;

        if (type == "ticker") {
            if (auto update = parse_ticker_message(data)) {
                update_book_state(update->ticker, update->yes_bid, update->yes_ask,
                                  update->last_price, update->volume);
                if (on_market_update_) on_market_update_(*update);
            }
        } else if (type == "orderbook_delta") {
            if (auto delta = parse_orderbook_delta(data)) {
                update_book_state(delta->ticker, delta->yes_bid, delta->yes_ask, 0.0, 0);
                WsMarketUpdate update;
                update.ticker = delta->ticker;
                update.yes_bid = delta->yes_bid;
                update.yes_ask = delta->yes_ask;
                if (on_market_update_) on_market_update_(update);
            }
        } else if (type == "fill") {
            if (auto fill = parse_fill_message(data)) {
                if (on_fill_) on_fill_(*fill);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to parse WS message: {}", e.what());
        if (on_error_) on_error_(e.what());
    }
}

} // namespace trader::kalshi
