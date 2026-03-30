#include "exchange/kalshi/rest_client.hpp"
#include <spdlog/spdlog.h>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <regex>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace trader::kalshi {

KalshiRestClient::KalshiRestClient(const std::string& base_url, KalshiAuth& auth)
    : base_url_(base_url), auth_(auth) {}

// Parse URL into host, port, path prefix
static void parse_url(const std::string& url, std::string& host, std::string& port, std::string& path_prefix) {
    // Simple URL parsing: https://host:port/path or https://host/path
    std::regex re(R"(https?://([^/:]+)(?::(\d+))?(/.*)?)");
    std::smatch match;
    if (std::regex_match(url, match, re)) {
        host = match[1].str();
        port = match[2].matched ? match[2].str() : "443";
        path_prefix = match[3].matched ? match[3].str() : "";
    }
}

HttpResponse KalshiRestClient::get(const std::string& path) {
    std::string host, port, path_prefix;
    parse_url(base_url_, host, port, path_prefix);
    std::string full_path = path_prefix + path;

    try {
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        // SNI
        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            return {0, "SNI setup failed"};
        }

        auto const results = resolver.resolve(host, port);
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        // Build request
        http::request<http::string_body> req(http::verb::get, full_path, 11);
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "Trader/0.1.0");
        req.set(http::field::accept, "application/json");

        // Auth headers
        if (auth_.is_loaded()) {
            auto headers = auth_.make_headers("GET", full_path);
            req.set("KALSHI-ACCESS-KEY", headers.key);
            req.set("KALSHI-ACCESS-TIMESTAMP", headers.timestamp);
            req.set("KALSHI-ACCESS-SIGNATURE", headers.signature);
        }

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        // Graceful shutdown
        beast::error_code ec;
        stream.shutdown(ec);

        return {static_cast<int>(res.result_int()), res.body()};
    } catch (const std::exception& e) {
        spdlog::error("HTTP GET {} failed: {}", full_path, e.what());
        return {0, e.what()};
    }
}

HttpResponse KalshiRestClient::post(const std::string& path, const nlohmann::json& body) {
    std::string host, port, path_prefix;
    parse_url(base_url_, host, port, path_prefix);
    std::string full_path = path_prefix + path;

    try {
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            return {0, "SNI setup failed"};
        }

        auto const results = resolver.resolve(host, port);
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req(http::verb::post, full_path, 11);
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "Trader/0.1.0");
        req.set(http::field::content_type, "application/json");
        req.set(http::field::accept, "application/json");
        req.body() = body.dump();
        req.prepare_payload();

        if (auth_.is_loaded()) {
            auto headers = auth_.make_headers("POST", full_path);
            req.set("KALSHI-ACCESS-KEY", headers.key);
            req.set("KALSHI-ACCESS-TIMESTAMP", headers.timestamp);
            req.set("KALSHI-ACCESS-SIGNATURE", headers.signature);
        }

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.shutdown(ec);

        return {static_cast<int>(res.result_int()), res.body()};
    } catch (const std::exception& e) {
        spdlog::error("HTTP POST {} failed: {}", full_path, e.what());
        return {0, e.what()};
    }
}

HttpResponse KalshiRestClient::del(const std::string& path) {
    std::string host, port, path_prefix;
    parse_url(base_url_, host, port, path_prefix);
    std::string full_path = path_prefix + path;

    try {
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            return {0, "SNI setup failed"};
        }

        auto const results = resolver.resolve(host, port);
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req(http::verb::delete_, full_path, 11);
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "Trader/0.1.0");
        req.set(http::field::accept, "application/json");

        if (auth_.is_loaded()) {
            auto headers = auth_.make_headers("DELETE", full_path);
            req.set("KALSHI-ACCESS-KEY", headers.key);
            req.set("KALSHI-ACCESS-TIMESTAMP", headers.timestamp);
            req.set("KALSHI-ACCESS-SIGNATURE", headers.signature);
        }

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.shutdown(ec);

        return {static_cast<int>(res.result_int()), res.body()};
    } catch (const std::exception& e) {
        spdlog::error("HTTP DELETE {} failed: {}", full_path, e.what());
        return {0, e.what()};
    }
}

KalshiMarket KalshiRestClient::parse_market(const nlohmann::json& j) {
    KalshiMarket m;
    m.ticker = j.value("ticker", "");
    m.title = j.value("title", "");
    m.category = j.value("category", "");
    m.status = j.value("status", "");

    // Prices can be strings ("0.6500") or numbers
    if (j.contains("yes_bid") && j["yes_bid"].is_string()) {
        m.yes_bid = std::stod(j["yes_bid"].get<std::string>());
    } else if (j.contains("yes_bid")) {
        m.yes_bid = j.value("yes_bid", 0.0);
    }

    if (j.contains("yes_ask") && j["yes_ask"].is_string()) {
        m.yes_ask = std::stod(j["yes_ask"].get<std::string>());
    } else if (j.contains("yes_ask")) {
        m.yes_ask = j.value("yes_ask", 0.0);
    }

    if (j.contains("last_price") && j["last_price"].is_string()) {
        m.last_price = std::stod(j["last_price"].get<std::string>());
    } else if (j.contains("last_price")) {
        m.last_price = j.value("last_price", 0.0);
    }

    m.volume = j.value("volume", 0);
    m.open_interest = j.value("open_interest", 0);
    m.close_time = j.value("close_time", "");
    m.expiration_time = j.value("expiration_time", "");
    m.result = j.value("result", "");

    return m;
}

std::vector<KalshiMarket> KalshiRestClient::get_markets(const std::string& category,
                                                          const std::string& status,
                                                          int limit) {
    std::string path = "/markets?limit=" + std::to_string(limit);
    if (!status.empty()) path += "&status=" + status;
    if (!category.empty()) path += "&category=" + category;

    auto resp = get(path);
    if (!resp.ok()) {
        spdlog::warn("get_markets failed: HTTP {}", resp.status_code);
        return {};
    }

    std::vector<KalshiMarket> markets;
    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("markets") && j["markets"].is_array()) {
            for (const auto& item : j["markets"]) {
                markets.push_back(parse_market(item));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse markets response: {}", e.what());
    }
    return markets;
}

std::optional<KalshiMarket> KalshiRestClient::get_market(const std::string& ticker) {
    auto resp = get("/markets/" + ticker);
    if (!resp.ok()) return std::nullopt;

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("market")) {
            return parse_market(j["market"]);
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse market response: {}", e.what());
    }
    return std::nullopt;
}

KalshiOrderbook KalshiRestClient::get_orderbook(const std::string& ticker) {
    auto resp = get("/markets/" + ticker + "/orderbook");
    KalshiOrderbook book;
    book.ticker = ticker;

    if (!resp.ok()) return book;

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("orderbook") && j["orderbook"].contains("yes")) {
            for (const auto& level : j["orderbook"]["yes"]) {
                double price = 0.0;
                int qty = 0;
                if (level.is_array() && level.size() >= 2) {
                    // [price, quantity] format
                    if (level[0].is_string()) price = std::stod(level[0].get<std::string>());
                    else price = level[0].get<double>();
                    qty = level[1].get<int>();
                }
                book.yes_bids.push_back({price, qty});
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse orderbook response: {}", e.what());
    }
    return book;
}

double KalshiRestClient::get_balance() {
    auto resp = get("/portfolio/balance");
    if (!resp.ok()) return 0.0;

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("balance") && j["balance"].is_string()) {
            return std::stod(j["balance"].get<std::string>());
        } else if (j.contains("balance")) {
            return j["balance"].get<double>();
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse balance response: {}", e.what());
    }
    return 0.0;
}

OrderId KalshiRestClient::place_order(const std::string& ticker, const std::string& side,
                                       const std::string& action, double price, int quantity,
                                       bool post_only) {
    nlohmann::json body;
    body["ticker"] = ticker;
    body["side"] = side;
    body["action"] = action;

    // Kalshi expects string prices with 4 decimal places
    std::ostringstream price_ss;
    price_ss << std::fixed << std::setprecision(4) << price;
    body["yes_price_dollars"] = price_ss.str();

    // Quantity as string
    std::ostringstream qty_ss;
    qty_ss << std::fixed << std::setprecision(2) << static_cast<double>(quantity);
    body["count_fp"] = qty_ss.str();

    if (post_only) body["post_only"] = true;
    body["time_in_force"] = "gtc";

    auto resp = post("/portfolio/orders", body);
    if (!resp.ok()) {
        spdlog::warn("place_order failed: HTTP {} - {}", resp.status_code, resp.body);
        return "";
    }

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("order") && j["order"].contains("order_id")) {
            return j["order"]["order_id"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse order response: {}", e.what());
    }
    return "";
}

bool KalshiRestClient::cancel_order(const OrderId& order_id) {
    auto resp = del("/portfolio/orders/" + order_id);
    return resp.ok();
}

void KalshiRestClient::refresh_market_cache(const std::string& category) {
    auto markets = get_markets(category);
    market_cache_.clear();
    for (auto& m : markets) {
        market_cache_[m.ticker] = std::move(m);
    }
    cache_time_ = std::chrono::system_clock::now();
}

double KalshiOrderbook::best_yes_ask() const {
    // In Kalshi, the best NO bid = 1.0 - best YES ask
    // Since orderbook only returns bids, we estimate ask from the spread
    // This is a simplification; real implementation would use the NO book
    return 0.0;  // Placeholder — need full orderbook data
}

} // namespace trader::kalshi
