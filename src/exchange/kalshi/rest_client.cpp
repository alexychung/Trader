#include "exchange/kalshi/rest_client.hpp"
#include "exchange/kalshi/encoding.hpp"
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
#include <thread>
#include <chrono>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace trader::kalshi {

// ssl::context loads OS cert stores and does one-time config — ~10ms + MBs RSS.
// We create it once per KalshiRestClient and reuse across all requests.
// ssl::context is documented thread-safe for concurrent SSL_new calls (via
// stream construction), so this is safe to share across threads.
struct KalshiRestClient::SslState {
    ssl::context ctx{ssl::context::tlsv12_client};
    SslState() { ctx.set_default_verify_paths(); }
};

KalshiRestClient::KalshiRestClient(const std::string& base_url, KalshiAuth& auth)
    : base_url_(base_url), auth_(auth), ssl_state_(std::make_shared<SslState>()) {}

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

namespace {
// Unified request path used by get/post/del. Returns HttpResponse with
// status_code=0 and body=error string on failure. Preserves the previous
// per-verb behavior exactly — identical header set, identical shutdown pattern
// — while sharing the ssl::context from the client instance.
HttpResponse do_request(const std::string& base_url,
                        KalshiAuth& auth,
                        ssl::context& ssl_ctx,
                        http::verb verb,
                        const std::string& method_str,
                        const std::string& path,
                        const std::string* body /* nullable */) {
    std::string host, port, path_prefix;
    parse_url(base_url, host, port, path_prefix);
    const std::string full_path = path_prefix + path;

    try {
        // io_context remains per-call — Asio io_contexts are stateful and not
        // meant to be reused across synchronous calls on this scale.
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            return {0, "SNI setup failed"};
        }

        auto const results = resolver.resolve(host, port);
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req(verb, full_path, 11);
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "Trader/0.1.0");
        req.set(http::field::accept, "application/json");
        if (body != nullptr) {
            req.set(http::field::content_type, "application/json");
            req.body() = *body;
            req.prepare_payload();
        }

        if (auth.is_loaded()) {
            auto headers = auth.make_headers(method_str, full_path);
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
        spdlog::error("HTTP {} {} failed: {}", method_str, full_path, e.what());
        return {0, e.what()};
    }
}
} // namespace

HttpResponse KalshiRestClient::get(const std::string& path) {
    return do_request(base_url_, auth_, ssl_state_->ctx, http::verb::get, "GET", path, nullptr);
}

HttpResponse KalshiRestClient::post(const std::string& path, const nlohmann::json& body) {
    std::string serialized = body.dump();
    return do_request(base_url_, auth_, ssl_state_->ctx, http::verb::post, "POST", path, &serialized);
}

HttpResponse KalshiRestClient::del(const std::string& path) {
    return do_request(base_url_, auth_, ssl_state_->ctx, http::verb::delete_, "DELETE", path, nullptr);
}

KalshiMarket KalshiRestClient::parse_market(const nlohmann::json& j) {
    KalshiMarket m;
    m.ticker = j.value("ticker", "");
    m.title = j.value("title", "");
    m.category = j.value("category", "");
    m.status = j.value("status", "");

    // 2026 wire format: prices are 4-decimal dollar strings under `*_dollars`
    // keys; quantities under `*_fp` keys. Legacy numeric keys kept as fallback
    // for older cached responses and fixtures.
    m.yes_bid    = parse_kalshi_price(j, j.contains("yes_bid_dollars") ? "yes_bid_dollars" : "yes_bid");
    m.yes_ask    = parse_kalshi_price(j, j.contains("yes_ask_dollars") ? "yes_ask_dollars" : "yes_ask");
    m.last_price = parse_kalshi_price(j, j.contains("last_price_dollars") ? "last_price_dollars" : "last_price");

    m.volume = j.contains("volume_fp") ? parse_kalshi_count_fp(j, "volume_fp") : j.value("volume", 0);
    m.open_interest = j.contains("open_interest_fp") ? parse_kalshi_count_fp(j, "open_interest_fp")
                                                      : j.value("open_interest", 0);
    m.close_time = j.value("close_time", "");
    m.expiration_time = j.value("expiration_time", "");
    m.result = j.value("result", "");
    m.fractional_trading_enabled = j.value("fractional_trading_enabled", false);

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

std::vector<KalshiMarket> KalshiRestClient::get_markets_paginated(
    const std::string& status, int page_limit, int max_pages,
    const std::string& series_ticker) {
    std::vector<KalshiMarket> all;
    std::string cursor;
    int pages = 0;

    while (pages < max_pages) {
        std::string path = "/markets?limit=" + std::to_string(page_limit);
        if (!status.empty()) path += "&status=" + status;
        if (!series_ticker.empty()) path += "&series_ticker=" + series_ticker;
        if (!cursor.empty()) path += "&cursor=" + cursor;

        // Transient 4xx/5xx have been observed on /markets in production
        // (Kalshi demo returned HTTP 400 for ~5 minutes on 2026-04-21 11:15
        // before recovering). One retry with a short backoff avoids aborting
        // the whole paginated scan over a blip. Only retries when the error
        // is plausibly transient (5xx or 429); a hard 400/401/403 still
        // aborts since those are client-shape problems.
        HttpResponse resp = get(path);
        if (!resp.ok()) {
            bool transient = (resp.status_code == 0 || resp.status_code == 429 ||
                              resp.status_code == 408 || resp.status_code >= 500);
            // Treat HTTP 400 on pagination as transient too — observed in
            // prod as a server-side issue, not a request-shape issue (the
            // same request succeeded minutes later).
            if (resp.status_code == 400 && pages > 0) transient = true;
            if (transient) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                spdlog::debug("get_markets_paginated retrying page {} after HTTP {}",
                              pages, resp.status_code);
                resp = get(path);
            }
        }
        if (!resp.ok()) {
            spdlog::warn("get_markets_paginated failed at page {}: HTTP {} "
                         "path={} body[0:500]={}",
                         pages, resp.status_code, path,
                         resp.body.substr(0, std::min<std::size_t>(500, resp.body.size())));
            break;
        }

        std::string next_cursor;
        int page_rows = 0;
        try {
            auto j = nlohmann::json::parse(resp.body);
            if (j.contains("markets") && j["markets"].is_array()) {
                for (const auto& item : j["markets"]) {
                    all.push_back(parse_market(item));
                    ++page_rows;
                }
            }
            // Kalshi pagination: response.cursor is empty / missing when no
            // more pages remain. Some responses include "" to signal end;
            // treat both cases the same.
            if (j.contains("cursor") && j["cursor"].is_string()) {
                next_cursor = j["cursor"].get<std::string>();
            }
        } catch (const nlohmann::json::exception& e) {
            spdlog::error("get_markets_paginated parse page {}: {}", pages, e.what());
            break;
        }

        ++pages;
        if (next_cursor.empty()) break;          // end of catalog
        if (page_rows == 0) break;               // defensive: no rows + cursor is a loop
        cursor = std::move(next_cursor);
    }

    if (pages == max_pages) {
        spdlog::warn("get_markets_paginated hit max_pages={} cap — catalog may be truncated",
                     max_pages);
    }
    return all;
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
                if (!level.is_array() || level.size() < 2) continue;
                // [price, quantity] — price is string "0.6500"; quantity is
                // integer `count` (legacy) or string `count_fp` per market.
                double price = 0.0;
                if (level[0].is_string()) price = std::stod(level[0].get<std::string>());
                else if (level[0].is_number()) price = level[0].get<double>();
                int qty = 0;
                if (level[1].is_string()) qty = static_cast<int>(std::stod(level[1].get<std::string>()));
                else if (level[1].is_number()) qty = level[1].get<int>();
                book.yes_bids.push_back({price, qty});
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse orderbook response: {}", e.what());
    }
    return book;
}

std::vector<KalshiRestClient::KalshiCandle>
KalshiRestClient::get_candlesticks(const std::string& series_ticker,
                                    const std::string& ticker,
                                    int64_t start_ts_sec,
                                    int64_t end_ts_sec,
                                    int period_interval_min) {
    // GET /series/{series}/markets/{ticker}/candlesticks
    //   ?start_ts=...&end_ts=...&period_interval=...
    std::ostringstream path;
    path << "/series/" << series_ticker << "/markets/" << ticker
         << "/candlesticks"
         << "?start_ts=" << start_ts_sec
         << "&end_ts=" << end_ts_sec
         << "&period_interval=" << period_interval_min;
    auto resp = get(path.str());
    if (!resp.ok()) {
        spdlog::warn("get_candlesticks {} failed: HTTP {} body[0:200]={}",
                     ticker, resp.status_code, resp.body.substr(0, 200));
        return {};
    }
    spdlog::debug("get_candlesticks {} resp body[0:1200]={}", ticker,
                  resp.body.substr(0, 1200));
    try {
        auto j = nlohmann::json::parse(resp.body);
        return parse_candlesticks(j);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("Failed to parse candlesticks for {}: {}", ticker, e.what());
        return {};
    }
}

std::vector<KalshiRestClient::KalshiCandle>
KalshiRestClient::parse_candlesticks(const nlohmann::json& body) {
    std::vector<KalshiCandle> out;
    if (!body.contains("candlesticks") || !body["candlesticks"].is_array()) {
        return out;
    }
    auto read_price = [](const nlohmann::json& obj, const char* key) -> double {
        if (!obj.contains(key) || obj[key].is_null()) return 0.0;
        const auto& v = obj[key];
        if (v.is_string()) {
            try { return std::stod(v.get<std::string>()); }
            catch (...) { return 0.0; }
        }
        if (v.is_number()) return v.get<double>();
        return 0.0;
    };
    // Try a primary key first, fall back to legacy. Kalshi switched the
    // OHLC field names from `close`/`open`/etc. to `close_dollars`/etc.
    // and renamed `volume` → `volume_fp` (and `open_interest` →
    // `open_interest_fp`) at some point in 2025/2026; the legacy shape is
    // kept as a fallback so old fixtures keep parsing.
    auto read_price_pref = [&](const nlohmann::json& obj, const char* primary,
                                const char* legacy) -> double {
        double v = read_price(obj, primary);
        if (v != 0.0) return v;
        return read_price(obj, legacy);
    };
    auto read_int_loose = [](const nlohmann::json& obj,
                              std::initializer_list<const char*> keys) -> int {
        for (const char* key : keys) {
            if (!obj.contains(key) || obj[key].is_null()) continue;
            const auto& v = obj[key];
            if (v.is_number_integer()) return v.get<int>();
            if (v.is_number()) return static_cast<int>(v.get<double>());
            if (v.is_string()) {
                try { return static_cast<int>(std::stod(v.get<std::string>())); }
                catch (...) { continue; }
            }
        }
        return 0;
    };
    for (const auto& c : body["candlesticks"]) {
        KalshiCandle k;
        if (c.contains("end_period_ts")) {
            if (c["end_period_ts"].is_number_integer()) {
                k.end_period_ts = c["end_period_ts"].get<int64_t>();
            } else if (c["end_period_ts"].is_number()) {
                k.end_period_ts = static_cast<int64_t>(c["end_period_ts"].get<double>());
            }
        }
        k.open_interest = read_int_loose(c, {"open_interest_fp", "open_interest"});
        k.volume = read_int_loose(c, {"volume_fp", "volume"});
        if (c.contains("price") && c["price"].is_object()) {
            const auto& p = c["price"];
            k.price_open  = read_price_pref(p, "open_dollars",  "open");
            k.price_high  = read_price_pref(p, "high_dollars",  "high");
            k.price_low   = read_price_pref(p, "low_dollars",   "low");
            k.price_close = read_price_pref(p, "close_dollars", "close");
        }
        if (c.contains("yes_bid") && c["yes_bid"].is_object()) {
            k.yes_bid_close = read_price_pref(c["yes_bid"], "close_dollars", "close");
        }
        if (c.contains("yes_ask") && c["yes_ask"].is_object()) {
            k.yes_ask_close = read_price_pref(c["yes_ask"], "close_dollars", "close");
        }
        out.push_back(k);
    }
    return out;
}

namespace {
// Parse a Kalshi timestamp, which may appear as ISO-8601 string
// ("2026-03-12T15:30:45.123Z") or epoch-ms number. Returns 0 on parse
// failure so callers can use the fallback policy.
int64_t parse_created_ts_ms(const nlohmann::json& j) {
    // Prefer milliseconds-precision fields in this order.
    for (const char* k : {"created_ts_ms", "ts_ms", "created_time", "created_at"}) {
        auto it = j.find(k);
        if (it == j.end() || it->is_null()) continue;
        if (it->is_number_integer() || it->is_number_unsigned()) {
            int64_t v = it->get<int64_t>();
            // Some fields (ts) are seconds-precision. Detect by magnitude:
            // a reasonable epoch-ms is > 1e12 (year 2001+).
            return v < 10'000'000'000LL ? v * 1000 : v;
        }
        if (it->is_number()) return static_cast<int64_t>(it->get<double>());
        if (it->is_string()) {
            // ISO-8601 string parsing — best-effort. Kalshi uses
            // "YYYY-MM-DDTHH:MM:SS(.fff)Z".
            std::tm tm{};
            const auto& s = it->get_ref<const std::string&>();
            if (s.size() < 19) continue;
            try {
                tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
                tm.tm_mon  = std::stoi(s.substr(5, 2)) - 1;
                tm.tm_mday = std::stoi(s.substr(8, 2));
                tm.tm_hour = std::stoi(s.substr(11, 2));
                tm.tm_min  = std::stoi(s.substr(14, 2));
                tm.tm_sec  = std::stoi(s.substr(17, 2));
            } catch (...) { continue; }
#ifdef _WIN32
            auto t = _mkgmtime(&tm);
#else
            auto t = timegm(&tm);
#endif
            if (t == -1) continue;
            int64_t ms = static_cast<int64_t>(t) * 1000;
            // Parse fractional seconds after '.' if present.
            auto dot = s.find('.');
            if (dot != std::string::npos && dot + 1 < s.size()) {
                std::string frac;
                for (std::size_t i = dot + 1; i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])); ++i) {
                    frac += s[i];
                    if (frac.size() == 3) break;
                }
                while (frac.size() < 3) frac += '0';
                try { ms += std::stoi(frac); } catch (...) {}
            }
            return ms;
        }
    }
    return 0;
}
} // namespace

std::vector<KalshiFill> KalshiRestClient::get_fills_since(int64_t min_ts_ms,
                                                            int page_limit,
                                                            int max_pages) {
    std::vector<KalshiFill> out;
    std::string cursor;
    int pages = 0;

    while (pages < max_pages) {
        std::ostringstream path;
        path << "/portfolio/fills?limit=" << page_limit;
        if (min_ts_ms > 0) {
            // Kalshi accepts seconds-precision min_ts on the query string.
            // Use seconds here and we filter more tightly in-memory below
            // (min_ts_ms) to avoid re-replaying the exact boundary record.
            path << "&min_ts=" << (min_ts_ms / 1000);
        }
        if (!cursor.empty()) path << "&cursor=" << cursor;

        auto resp = get(path.str());
        if (!resp.ok()) {
            spdlog::warn("get_fills_since failed: HTTP {} body={}",
                         resp.status_code, resp.body.substr(0, 200));
            break;
        }

        std::string next_cursor;
        try {
            auto j = nlohmann::json::parse(resp.body);
            if (j.contains("fills") && j["fills"].is_array()) {
                for (const auto& item : j["fills"]) {
                    KalshiFill f;
                    f.order_id = item.value("order_id", "");
                    f.trade_id = item.value("trade_id", "");
                    f.ticker = item.value("ticker", "");
                    // 2026 wire format: prices/fees are dollar strings.
                    f.price = parse_kalshi_price(item,
                        item.contains("yes_price_dollars") ? "yes_price_dollars" : "price");
                    f.count = item.contains("count_fp")
                        ? parse_kalshi_count_fp(item, "count_fp")
                        : item.value("count", 0);
                    f.side = item.value("side", "");
                    f.action = item.value("action", "");
                    f.created_ts_ms = parse_created_ts_ms(item);
                    f.fee_cost = parse_kalshi_price(item, "fee_cost", 0.0);

                    // Drop anything older than the watermark (query param is
                    // seconds-precision; in-memory filter is ms-precision).
                    if (f.created_ts_ms > min_ts_ms) out.push_back(std::move(f));
                }
            }
            if (j.contains("cursor") && j["cursor"].is_string()) {
                next_cursor = j["cursor"].get<std::string>();
            }
        } catch (const nlohmann::json::exception& e) {
            spdlog::error("get_fills_since parse page {}: {}", pages, e.what());
            break;
        }

        ++pages;
        if (next_cursor.empty()) break;
        cursor = std::move(next_cursor);
    }
    if (pages == max_pages) {
        spdlog::warn("get_fills_since hit max_pages={} cap — some history may be truncated",
                     max_pages);
    }

    // Oldest-first so a sequential replay preserves original ordering.
    std::sort(out.begin(), out.end(), [](const KalshiFill& a, const KalshiFill& b) {
        return a.created_ts_ms < b.created_ts_ms;
    });
    return out;
}

std::vector<KalshiRestClient::MarketPosition>
KalshiRestClient::parse_market_positions(const nlohmann::json& body) {
    std::vector<MarketPosition> out;
    if (!body.contains("market_positions") || !body["market_positions"].is_array()) {
        return out;
    }
    for (const auto& item : body["market_positions"]) {
        MarketPosition mp;
        mp.ticker = item.value("ticker", "");
        if (mp.ticker.empty()) continue;

        // position_fp is a decimal-string on fractional markets, integer
        // elsewhere. Parse as double then truncate — the strategy/risk
        // layer still tracks int shares, matching MarketFilter's
        // skip-fractional gate.
        const auto pos_it = item.find("position_fp");
        double pos_fp = 0.0;
        if (pos_it != item.end() && pos_it->is_string()) {
            try { pos_fp = std::stod(pos_it->get<std::string>()); } catch (...) {}
        } else if (item.contains("position")) {
            pos_fp = item["position"].get<double>();
        }
        mp.position = static_cast<int>(pos_fp);
        if (mp.position == 0) continue;  // skip closed positions

        auto parse_money = [&](const char* key) -> double {
            auto it = item.find(key);
            if (it == item.end()) return 0.0;
            if (it->is_string()) {
                try { return std::stod(it->get<std::string>()); } catch (...) { return 0.0; }
            }
            if (it->is_number()) return it->get<double>();
            return 0.0;
        };
        mp.cost = parse_money("total_traded_dollars");
        mp.fees_paid = parse_money("fees_paid_dollars");
        mp.realized_pnl = parse_money("realized_pnl_dollars");
        out.push_back(std::move(mp));
    }
    return out;
}

std::vector<KalshiRestClient::MarketPosition>
KalshiRestClient::get_market_positions() {
    auto resp = get("/portfolio/positions");
    if (!resp.ok()) {
        spdlog::warn("get_market_positions failed: HTTP {} body={}",
                     resp.status_code, resp.body.substr(0, 200));
        return {};
    }
    try {
        return parse_market_positions(nlohmann::json::parse(resp.body));
    } catch (const nlohmann::json::exception& e) {
        spdlog::warn("get_market_positions parse failed: {}", e.what());
        return {};
    }
}

KalshiRestClient::AccountLimits KalshiRestClient::get_account_limits() {
    AccountLimits out;
    auto resp = get("/account/limits");
    if (!resp.ok()) {
        spdlog::warn("get_account_limits failed: HTTP {}", resp.status_code);
        return out;
    }
    try {
        auto j = nlohmann::json::parse(resp.body);
        // Kalshi wraps the payload under "limits" on some responses, directly
        // at root on others. Accept both shapes.
        const auto& root = j.contains("limits") ? j["limits"] : j;
        out.tier = root.value("tier", root.value("account_type", ""));
        out.read_rps = root.value("read_rate_limit_per_second",
                        root.value("read_rps", 0));
        out.write_rps = root.value("write_rate_limit_per_second",
                         root.value("write_rps", 0));
        out.max_open_orders = root.value("max_open_orders", 0);
    } catch (const nlohmann::json::exception& e) {
        spdlog::warn("get_account_limits parse failed: {}", e.what());
    }
    return out;
}

std::unordered_map<std::string, KalshiRestClient::FeeOverride>
KalshiRestClient::get_fee_schedule() {
    std::unordered_map<std::string, FeeOverride> out;
    auto resp = get("/series/fee_changes");
    if (!resp.ok()) {
        spdlog::debug("get_fee_schedule: HTTP {} (likely no custom schedules)",
                      resp.status_code);
        return out;
    }
    try {
        auto j = nlohmann::json::parse(resp.body);
        // Shape: { "fee_changes": [{ "series_ticker": "KXCPI", "maker_fee_rate": 0.02, ... }] }
        if (j.contains("fee_changes") && j["fee_changes"].is_array()) {
            for (const auto& entry : j["fee_changes"]) {
                std::string series = entry.value("series_ticker", "");
                if (series.empty()) continue;
                FeeOverride fo;
                // Accept both "maker_fee_rate" and "maker_rate" spellings —
                // Kalshi's docs have had both in different revisions.
                if (entry.contains("maker_fee_rate")) {
                    fo.maker_rate = entry["maker_fee_rate"].get<double>();
                } else if (entry.contains("maker_rate")) {
                    fo.maker_rate = entry["maker_rate"].get<double>();
                }
                if (entry.contains("taker_fee_rate")) {
                    fo.taker_rate = entry["taker_fee_rate"].get<double>();
                } else if (entry.contains("taker_rate")) {
                    fo.taker_rate = entry["taker_rate"].get<double>();
                }
                out[series] = fo;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::warn("get_fee_schedule parse failed: {}", e.what());
    }
    return out;
}

std::unordered_map<std::string, KalshiOrderbook> KalshiRestClient::get_orderbooks(
    const std::vector<std::string>& tickers) {
    std::unordered_map<std::string, KalshiOrderbook> out;
    if (tickers.empty()) return out;

    // Kalshi caps the batch at 100 per request. Split transparently.
    constexpr std::size_t kBatchCap = 100;

    auto parse_yes_levels = [](const nlohmann::json& yes_arr,
                                KalshiOrderbook& book) {
        for (const auto& level : yes_arr) {
            if (!level.is_array() || level.size() < 2) continue;
            double price = 0.0;
            if (level[0].is_string()) price = std::stod(level[0].get<std::string>());
            else if (level[0].is_number()) price = level[0].get<double>();
            int qty = 0;
            if (level[1].is_string()) qty = static_cast<int>(std::stod(level[1].get<std::string>()));
            else if (level[1].is_number()) qty = level[1].get<int>();
            book.yes_bids.push_back({price, qty});
        }
    };

    for (std::size_t i = 0; i < tickers.size(); i += kBatchCap) {
        const std::size_t end = std::min(i + kBatchCap, tickers.size());
        std::ostringstream path;
        path << "/markets/orderbooks?tickers=";
        bool first = true;
        for (std::size_t j = i; j < end; ++j) {
            if (!first) path << ",";
            path << tickers[j];
            first = false;
        }

        auto resp = get(path.str());
        if (!resp.ok()) {
            spdlog::warn("get_orderbooks batch failed HTTP {} body={}",
                         resp.status_code, resp.body.substr(0, 200));
            continue;  // try next batch — caller may still get partial results
        }

        try {
            auto j = nlohmann::json::parse(resp.body);
            // Shape: { "orderbooks": [{ "ticker": "KX...", "yes": [[price, qty], ...] }] }
            // Also seen: a top-level map { ticker: { yes: [...] } } — accept both.
            if (j.contains("orderbooks") && j["orderbooks"].is_array()) {
                for (const auto& entry : j["orderbooks"]) {
                    std::string t = entry.value("ticker", "");
                    if (t.empty()) continue;
                    KalshiOrderbook book;
                    book.ticker = t;
                    if (entry.contains("yes") && entry["yes"].is_array()) {
                        parse_yes_levels(entry["yes"], book);
                    }
                    out[t] = std::move(book);
                }
            } else if (j.is_object()) {
                for (auto it = j.begin(); it != j.end(); ++it) {
                    if (!it->is_object()) continue;
                    KalshiOrderbook book;
                    book.ticker = it.key();
                    if (it->contains("yes") && (*it)["yes"].is_array()) {
                        parse_yes_levels((*it)["yes"], book);
                    }
                    out[it.key()] = std::move(book);
                }
            }
        } catch (const nlohmann::json::exception& e) {
            spdlog::warn("get_orderbooks batch parse failed: {}", e.what());
        }
    }
    return out;
}

double KalshiRestClient::get_balance() {
    auto resp = get("/portfolio/balance");
    if (!resp.ok()) return 0.0;

    try {
        auto j = nlohmann::json::parse(resp.body);
        // Kalshi 2026 prod returns TWO balance fields with different units:
        //   - top-level "balance": <int>  in CENTS (e.g. 10000 = $100.00)
        //   - "balance_breakdown":[{"balance":"<dollar string>", ...}]
        //     in DOLLARS as a string (e.g. "100.0000" = $100.00)
        // Prior implementation read the top-level numeric as dollars and was
        // 100× too high. We now prefer the breakdown (cleaner units) and fall
        // back to top-level-as-cents.
        if (j.contains("balance_breakdown") && j["balance_breakdown"].is_array()
            && !j["balance_breakdown"].empty()) {
            const auto& first = j["balance_breakdown"][0];
            if (first.contains("balance")) {
                return parse_kalshi_price(first, "balance");
            }
        }
        if (j.contains("balance") && j["balance"].is_number()) {
            return j["balance"].get<double>() / 100.0;
        }
        return parse_kalshi_price(j, "balance");
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

    // 2026 wire format (see encoding.hpp). Prices are 4-decimal strings,
    // counts are 2-decimal strings in count_fp.
    body["yes_price_dollars"] = price_to_kalshi_string(price);
    body["count_fp"] = count_to_kalshi_fp_string(quantity);

    if (post_only) body["post_only"] = true;
    // Kalshi defaults to GTC when `time_in_force` is omitted. Sending "gtc"
    // (or any value outside its oneof validator) fails request validation.

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

bool KalshiRestClient::amend_order(const OrderId& order_id, double new_price,
                                    int new_quantity) {
    if (order_id.empty()) return false;
    if (new_price <= 0.0 && new_quantity <= 0) return false;

    nlohmann::json body = nlohmann::json::object();
    if (new_price > 0.0) {
        // Wire format mirrors place_order (2026 API):
        //   yes_price_dollars: "<4-decimal-string>"  in YES-native space
        // The original implementation sent an int-cents `yes_price` field —
        // wrong API shape; would silently fail or be reinterpreted. Fixed
        // 2026-05-20.
        if (new_price <= 0.0 || new_price >= 1.0) {
            spdlog::warn("amend_order: price {} out of (0, 1) range", new_price);
            return false;
        }
        body["yes_price_dollars"] = price_to_kalshi_string(new_price);
    }
    if (new_quantity > 0) {
        // count_fp matches place_order; some Kalshi endpoints accept either
        // `count` (int) or `count_fp` (string). Standardize on the place_order
        // convention.
        body["count_fp"] = count_to_kalshi_fp_string(new_quantity);
    }

    auto resp = post("/portfolio/orders/" + order_id + "/amend", body);
    if (!resp.ok()) {
        spdlog::warn("amend_order failed: HTTP {} body[0:200]={}",
                     resp.status_code,
                     resp.body.substr(0, std::min<std::size_t>(200, resp.body.size())));
        return false;
    }
    return true;
}

std::optional<std::vector<OrderId>> KalshiRestClient::batch_cancel_orders(
    const std::vector<OrderId>& order_ids) {
    if (order_ids.empty()) return std::vector<OrderId>{};

    // Kalshi caps batches at 20. Split transparently.
    constexpr std::size_t kBatchCap = 20;
    std::vector<OrderId> cancelled;
    cancelled.reserve(order_ids.size());

    for (std::size_t i = 0; i < order_ids.size(); i += kBatchCap) {
        const std::size_t end = std::min(i + kBatchCap, order_ids.size());
        nlohmann::json body;
        body["ids"] = nlohmann::json::array();
        for (std::size_t j = i; j < end; ++j) body["ids"].push_back(order_ids[j]);

        auto resp = post("/portfolio/orders/batched/cancel", body);
        if (resp.status_code == 404 || resp.status_code == 403) {
            spdlog::warn("batch cancel endpoint unavailable ({}), fallback to serial",
                         resp.status_code);
            return std::nullopt;
        }
        if (!resp.ok()) {
            spdlog::warn("batch cancel failed ({}): {}", resp.status_code, resp.body);
            continue;  // uncovered ids will be retried serially by caller
        }

        // Response shape: { "cancelled": [...], "errors": [...] }. Only add
        // server-confirmed ids. If the shape is missing the accounting,
        // add NONE — the caller will retry all ids in the batch serially
        // rather than assume success and leave orders resting on the venue.
        try {
            auto j = nlohmann::json::parse(resp.body);
            if (j.contains("cancelled") && j["cancelled"].is_array()) {
                for (const auto& item : j["cancelled"]) {
                    if (item.is_string()) {
                        cancelled.push_back(item.get<std::string>());
                    } else if (item.is_object() && item.contains("id") && item["id"].is_string()) {
                        cancelled.push_back(item["id"].get<std::string>());
                    }
                }
            } else {
                spdlog::warn("batch cancel response missing 'cancelled' array; "
                             "caller will retry batch serially");
            }
        } catch (const std::exception& e) {
            spdlog::warn("batch cancel response parse failed: {}", e.what());
        }
    }

    return cancelled;
}

void KalshiRestClient::refresh_market_cache(const std::string& category) {
    auto markets = get_markets(category);
    market_cache_.clear();
    for (auto& m : markets) {
        market_cache_[m.ticker] = std::move(m);
    }
    cache_time_ = std::chrono::system_clock::now();
}

void KalshiRestClient::refresh_markets_by_ticker_prefix(
    const std::vector<std::string>& prefixes,
    const std::vector<std::string>& excludes,
    int page_limit) {
    // Walk the entire open-markets catalog via cursor pagination and keep
    // only those with a matching prefix and no disallowed substring.
    // Without pagination the first page is dominated by high-volume series
    // (MVE, sports) and hides the entire weather/macro slate.
    market_cache_.clear();

    auto matches_prefix = [&](const std::string& t) {
        for (const auto& p : prefixes) {
            if (t.compare(0, p.size(), p) == 0) return true;
        }
        return false;
    };
    auto contains_exclude = [&](const std::string& t) {
        for (const auto& e : excludes) {
            if (t.find(e) != std::string::npos) return true;
        }
        return false;
    };

    // Kalshi's prod catalog has grown massively — verified 2026-05-05 that
    // the first 50k+ markets are dominated by KXMVECROSSCATEGORY (~21k) and
    // KXMVESPORTSMULTIGAMEEXTENDED (~29k). Weather (KXTEMP*), macro (KXUSNFP,
    // KXCPI, KXFED*), and other prefixes used by this strategy sort
    // alphabetically AFTER the sports glut, so a 100-page cap (100k markets)
    // hit truncation and returned 0/100k matches. 300 pages (~300k markets)
    // walks the full catalog with headroom; cursor pagination ends naturally
    // when the catalog is exhausted, so the cap only matters as a runaway
    // guard, not as a tuning knob.
    //
    // Query param note: Kalshi's `status` filter accepts "open" (tradeable)
    // but **rejects "active" with HTTP 400 "invalid status filter"**, even
    // though the response body reports `status: "active"` for the very same
    // markets. We observed this live on 2026-04-21: a prior edit swapped
    // "open" → "active" and the paginated catalog fetch 400'd every tick
    // for an hour. Keep the query param as "open"; use is_tradeable_status
    // below to recognize both "active" and "open" in the response shape.
    auto markets = get_markets_paginated("open", page_limit, /*max_pages=*/300);
    // Kalshi's `status` query filter is best-effort; the response still
    // occasionally contains closed/determined markets (observed overnight:
    // 84 closed April-20 contracts reappearing every tick). Drop them
    // here so they don't occupy cache slots or spam the rejection log.
    auto is_tradeable_status = [](const std::string& s) {
        return s == "active" || s == "open" || s.empty();
    };
    int kept = 0, dropped_closed = 0;
    for (auto& m : markets) {
        if (!matches_prefix(m.ticker)) continue;
        if (contains_exclude(m.ticker)) continue;
        if (!is_tradeable_status(m.status)) { ++dropped_closed; continue; }
        market_cache_[m.ticker] = std::move(m);
        ++kept;
    }
    cache_time_ = std::chrono::system_clock::now();

    std::string joined_p;
    for (const auto& p : prefixes) { joined_p += (joined_p.empty() ? "" : ","); joined_p += p; }
    std::string joined_e;
    for (const auto& e : excludes) { joined_e += (joined_e.empty() ? "" : ","); joined_e += e; }
    spdlog::info("refresh_markets_by_ticker_prefix(prefix=[{}], exclude=[{}]): "
                 "{}/{} markets kept ({} dropped as closed/settled)",
                 joined_p, joined_e, kept, static_cast<int>(markets.size()),
                 dropped_closed);
    int shown = 0;
    for (const auto& [t, m] : market_cache_) {
        if (shown >= 3) break;
        spdlog::info("  sample: {} status='{}' yes_bid={:.4f} yes_ask={:.4f}",
                     t, m.status, m.yes_bid, m.yes_ask);
        ++shown;
    }
}

void KalshiRestClient::refresh_markets_by_series(const std::string& series_ticker,
                                                  int page_limit) {
    // Server-side series filter — Kalshi returns only markets for the named
    // series. Prod's open catalog is ~300k entries and our series-of-interest
    // (KXNBAGAME) sorts well past any sane page cap, so the prefix-walk in
    // refresh_markets_by_ticker_prefix returns 0/300000 on prod even though
    // the markets exist. This path queries ?series_ticker= directly — one
    // round-trip per page covers the whole series in ~20 markets.
    market_cache_.clear();

    auto markets = get_markets_paginated("open", page_limit,
                                          /*max_pages=*/10, series_ticker);
    auto is_tradeable_status = [](const std::string& s) {
        return s == "active" || s == "open" || s.empty();
    };
    int kept = 0, dropped_closed = 0;
    for (auto& m : markets) {
        if (!is_tradeable_status(m.status)) { ++dropped_closed; continue; }
        market_cache_[m.ticker] = std::move(m);
        ++kept;
    }
    cache_time_ = std::chrono::system_clock::now();

    spdlog::info("refresh_markets_by_series(series={}): {}/{} markets kept "
                 "({} dropped as closed/settled)",
                 series_ticker, kept, static_cast<int>(markets.size()),
                 dropped_closed);
    int shown = 0;
    for (const auto& [t, m] : market_cache_) {
        if (shown >= 3) break;
        spdlog::info("  sample: {} status='{}' yes_bid={:.4f} yes_ask={:.4f}",
                     t, m.status, m.yes_bid, m.yes_ask);
        ++shown;
    }
}

double KalshiOrderbook::best_yes_ask() const {
    // In Kalshi, the best NO bid = 1.0 - best YES ask
    // Since orderbook only returns bids, we estimate ask from the spread
    // This is a simplification; real implementation would use the NO book
    return 0.0;  // Placeholder — need full orderbook data
}

} // namespace trader::kalshi
