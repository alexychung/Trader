#include "exchange/kalshi/ws_client.hpp"
#include "exchange/kalshi/encoding.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <random>
#include <regex>

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace trader::kalshi {

namespace {
// Extract a timestamp from a message. Prefer ts_ms (ms, added Mar 2026);
// fall back to ts (seconds, deprecated Apr 15 2026). Defaults to now if neither present.
Timestamp parse_timestamp(const nlohmann::json& j) {
    if (j.contains("ts_ms") && j["ts_ms"].is_number()) {
        return Timestamp(std::chrono::milliseconds(j["ts_ms"].get<int64_t>()));
    }
    if (j.contains("ts") && j["ts"].is_number()) {
        return Timestamp(std::chrono::seconds(j["ts"].get<int64_t>()));
    }
    return std::chrono::system_clock::now();
}

// Extract seq if present; 0 means "no sequence tracking for this message."
int64_t parse_seq(const nlohmann::json& j) {
    if (j.contains("seq") && j["seq"].is_number()) return j["seq"].get<int64_t>();
    return 0;
}

std::string seq_key(const std::string& channel, const std::string& ticker) {
    return channel + "|" + ticker;
}
} // namespace

// Pimpl — holds all Boost.Beast state so callers never need the Beast headers.
struct KalshiWsClient::LiveSession {
    net::io_context ioc;
    ssl::context ssl_ctx{ssl::context::tlsv12_client};
    // Constructed per-session (recreated on reconnect).
    std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> ws;
    beast::flat_buffer read_buffer;
    std::deque<std::string> write_queue;
    bool writing = false;
    bool session_failed = false;

    LiveSession() { ssl_ctx.set_default_verify_paths(); }
};

namespace {
// Parse wss://host[:port]/path into components. Mirrors rest_client's parse_url
// but for the WebSocket scheme.
bool parse_ws_url(const std::string& url,
                  std::string& host, std::string& port, std::string& path) {
    std::regex re(R"(wss?://([^/:]+)(?::(\d+))?(/.*)?)");
    std::smatch m;
    if (!std::regex_match(url, m, re)) return false;
    host = m[1].str();
    port = m[2].matched ? m[2].str() : "443";
    path = m[3].matched ? m[3].str() : "/";
    return true;
}

// Seeded per-process random in [0, 1) for jitter. Not cryptographic; just
// prevents every reconnect fleet from thundering-herding on identical delays.
double random_01() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}
} // namespace

KalshiWsClient::KalshiWsClient(const std::string& ws_url, KalshiAuth& auth)
    : ws_url_(ws_url), auth_(auth), cfg_{} {}

KalshiWsClient::KalshiWsClient(const std::string& ws_url, KalshiAuth& auth, WsConfig cfg)
    : ws_url_(ws_url), auth_(auth), cfg_(std::move(cfg)) {}

KalshiWsClient::~KalshiWsClient() {
    disconnect();
}

bool KalshiWsClient::connect() {
    if (!cfg_.enable_live) {
        // Stub mode — unchanged behavior for unit tests and offline runs.
        // Message parsing, book state, and subscription tracking are fully
        // exercised in tests via process_message() without a live socket.
        spdlog::info("KalshiWsClient: connect() stub mode for {}", ws_url_);
        connected_ = true;
        return true;
    }

    stop_live_ = false;
    reader_thread_ = std::thread([this] { run_live_loop(); });
    // We return true eagerly — the reader thread owns the actual connection
    // lifecycle and will retry via the backoff policy. Poll is_connected()
    // (the atomic is flipped on successful handshake) if synchronous readiness
    // matters.
    spdlog::info("KalshiWsClient: live mode — reader thread started for {}", ws_url_);
    return true;
}

void KalshiWsClient::disconnect() {
    if (reader_thread_.joinable()) {
        stop_live_ = true;
        // Wake the writer if it's parked on the condition variable.
        { std::lock_guard<std::mutex> lock(write_mutex_); }
        write_cv_.notify_all();
        // Force any in-flight async I/O to complete by stopping the io_context.
        if (live_) live_->ioc.stop();
        reader_thread_.join();
        live_.reset();
    }
    if (connected_) {
        spdlog::info("KalshiWsClient: disconnecting");
        connected_ = false;
    }
}

void KalshiWsClient::run_live_loop() {
    // Owned for the lifetime of the thread. Recreated fresh on every
    // reconnect — a failed Beast stream is not reusable once its underlying
    // socket has errored.
    live_ = std::make_unique<LiveSession>();

    std::string host, port, path;
    if (!parse_ws_url(ws_url_, host, port, path)) {
        spdlog::error("WS url unparseable: {}", ws_url_);
        return;
    }

    int attempt = 0;
    while (!stop_live_) {
        // Fresh stream per session. Reusing after error is UB in Beast.
        live_->ws = std::make_unique<
            websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(
                live_->ioc, live_->ssl_ctx);

        try {
            // SNI is required by most production TLS termination — including
            // Kalshi's. Without it the handshake fails with a generic SSL
            // error that's painful to diagnose.
            if (!SSL_set_tlsext_host_name(
                    live_->ws->next_layer().native_handle(), host.c_str())) {
                throw beast::system_error{beast::error_code{
                    static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()}};
            }

            tcp::resolver resolver(live_->ioc);
            auto results = resolver.resolve(host, port);

            // TCP connect — bounded by the handshake timeout so a hung SYN
            // doesn't wedge the thread forever.
            beast::get_lowest_layer(*live_->ws).expires_after(cfg_.handshake_timeout);
            beast::get_lowest_layer(*live_->ws).connect(results);

            // TLS.
            live_->ws->next_layer().handshake(ssl::stream_base::client);

            // Switch to permessage idle timeout; disable the expires_after
            // that was set for the connect phase. Also turn on Beast's built-
            // in keepalive pings: idle > ping_interval triggers a PING frame
            // which Kalshi must PONG or we treat the session as dead.
            beast::get_lowest_layer(*live_->ws).expires_never();
            websocket::stream_base::timeout opt{
                cfg_.handshake_timeout,          // handshake
                cfg_.ping_interval,              // idle
                true                             // keep_alive_pings
            };
            live_->ws->set_option(opt);

            // Kalshi auth lives in the WebSocket upgrade request, not in the
            // post-upgrade frames. Decorator runs every handshake/reconnect.
            live_->ws->set_option(websocket::stream_base::decorator(
                [this, path](websocket::request_type& req) {
                    if (auth_.is_loaded()) {
                        auto h = auth_.make_headers("GET", path);
                        req.set("KALSHI-ACCESS-KEY", h.key);
                        req.set("KALSHI-ACCESS-TIMESTAMP", h.timestamp);
                        req.set("KALSHI-ACCESS-SIGNATURE", h.signature);
                    }
                    req.set(beast::http::field::user_agent, "Trader/0.1.0");
                }));

            live_->ws->handshake(host, path);
            spdlog::info("KalshiWsClient: live WS handshake OK ({}:{}{})",
                         host, port, path);
            connected_ = true;
            attempt = 0;
            alerted_consecutive_failures_ = false;
            live_->session_failed = false;

            // Every active subscription must be re-issued after the handshake.
            replay_subscriptions();

            // Async read loop. Each completion schedules the next read and
            // dispatches to process_message(). Errors surface via session_failed.
            std::function<void()> do_read;
            do_read = [this, &do_read] {
                live_->ws->async_read(live_->read_buffer,
                    [this, &do_read](beast::error_code ec, std::size_t) {
                        if (ec) {
                            if (!stop_live_) {
                                spdlog::warn("WS read error: {}", ec.message());
                            }
                            live_->session_failed = true;
                            return;
                        }
                        std::string msg = beast::buffers_to_string(live_->read_buffer.data());
                        live_->read_buffer.consume(live_->read_buffer.size());
                        try { process_message(msg); }
                        catch (const std::exception& e) {
                            spdlog::warn("process_message threw: {}", e.what());
                        }
                        if (!stop_live_ && !live_->session_failed) do_read();
                    });
            };
            do_read();

            // Run until stop, failure, or io exhaustion. reset() before each
            // run_one() because stop() latches the context.
            while (!stop_live_ && !live_->session_failed) {
                live_->ioc.run_one();
            }

            connected_ = false;

            // Clean close attempt — best-effort; failure to close cleanly is
            // fine because we're tearing down anyway.
            beast::error_code close_ec;
            live_->ws->close(websocket::close_code::normal, close_ec);
        } catch (const std::exception& e) {
            spdlog::warn("WS session failed: {}", e.what());
            connected_ = false;
        }

        if (stop_live_) break;

        // Reset the context before reconnecting — a stopped io_context is
        // sticky; run_one() returns immediately until reset.
        live_->ioc.restart();

        auto delay = backoff_delay(attempt, random_01(), cfg_.backoff);
        spdlog::warn("WS reconnect in {}ms (attempt {})", delay.count(), attempt);

        // Fire on_error once when we cross the consecutive-failure threshold.
        // Don't refire on every subsequent failure — the alert pipeline is
        // typically a human-facing webhook and repeated messages are noise.
        // The flag resets once a handshake succeeds.
        if (!alerted_consecutive_failures_ &&
            attempt >= cfg_.max_consecutive_failures_before_alert &&
            on_error_) {
            alerted_consecutive_failures_ = true;
            on_error_(
                "WebSocket has failed to reconnect " +
                std::to_string(attempt) + " times in a row. This usually means "
                "revoked API credentials, a DNS/TLS misconfiguration, or an "
                "extended Kalshi outage. The client will keep retrying.");
        }

        auto deadline = std::chrono::steady_clock::now() + delay;
        while (!stop_live_ && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ++attempt;
    }

    spdlog::info("KalshiWsClient: reader thread exiting");
}

namespace {
// Returns true if sub was added (not a duplicate).
bool record_subscription(std::vector<KalshiWsClient::Subscription>& v,
                         const KalshiWsClient::Subscription& s) {
    for (const auto& e : v) if (e == s) return false;
    v.push_back(s);
    return true;
}
} // namespace

void KalshiWsClient::subscribe_orderbook(const std::string& ticker) {
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        record_subscription(subscriptions_, {"orderbook_delta", ticker});
    }
    send_subscribe("orderbook_delta", {ticker});
}

void KalshiWsClient::subscribe_ticker(const std::string& ticker) {
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        record_subscription(subscriptions_, {"ticker", ticker});
    }
    send_subscribe("ticker", {ticker});
}

void KalshiWsClient::subscribe_fills() {
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        record_subscription(subscriptions_, {"fill", ""});
    }
    send_subscribe("fill", {});
}

void KalshiWsClient::subscribe_user_orders() {
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        record_subscription(subscriptions_, {"user_orders", ""});
    }
    send_subscribe("user_orders", {});
}

void KalshiWsClient::unsubscribe(const std::string& channel, const std::string& ticker) {
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        subscriptions_.erase(
            std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                [&](const Subscription& s) {
                    return s.channel == channel && (ticker.empty() || s.ticker == ticker);
                }),
            subscriptions_.end());
    }
    spdlog::debug("Unsubscribe from {} {}", channel, ticker);
}

std::vector<KalshiWsClient::Subscription> KalshiWsClient::active_subscriptions() const {
    std::lock_guard<std::mutex> lock(subs_mutex_);
    return subscriptions_;
}

void KalshiWsClient::replay_subscriptions() {
    // Copy under lock so the replay network I/O (send_subscribe) doesn't
    // block callers who add new subscriptions mid-replay.
    std::vector<Subscription> snapshot;
    {
        std::lock_guard<std::mutex> lock(subs_mutex_);
        snapshot = subscriptions_;
    }
    for (const auto& s : snapshot) {
        std::vector<std::string> tickers = s.ticker.empty()
            ? std::vector<std::string>{}
            : std::vector<std::string>{s.ticker};
        send_subscribe(s.channel, tickers);
    }
    spdlog::info("KalshiWsClient: replayed {} subscription(s)", snapshot.size());
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
    enqueue_write(msg.dump());
}

void KalshiWsClient::enqueue_write(const std::string& json) {
    // Stub mode: no-op (callers can safely pre-connect subscribe).
    if (!cfg_.enable_live) return;

    if (!json.empty()) {
        std::lock_guard<std::mutex> lock(write_mutex_);
        outbound_.push_back(json);
    }

    // Wake the reader thread's drain logic, or chain the next write if we
    // are already on the io_context thread.
    if (live_) {
        auto* session = live_.get();
        net::post(session->ioc, [this, session] {
            if (!session->ws || session->writing) return;
            std::string next;
            {
                std::lock_guard<std::mutex> lock(write_mutex_);
                if (outbound_.empty()) return;
                next = std::move(outbound_.front());
                outbound_.pop_front();
            }
            session->writing = true;
            // async_write's lifetime: the buffer must outlive the op. We
            // move `next` into a shared_ptr so the completion handler owns it.
            auto payload = std::make_shared<std::string>(std::move(next));
            session->ws->async_write(net::buffer(*payload),
                [this, session, payload](beast::error_code ec, std::size_t) {
                    session->writing = false;
                    if (ec) {
                        spdlog::warn("WS write failed: {}", ec.message());
                        session->session_failed = true;
                        return;
                    }
                    // Chain the next queued write.
                    enqueue_write(std::string{});
                });
        });
    }
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
    // Use >= 0 to allow zero values (a bid of $0.00 is valid — YES is worthless)
    // Use < 0 as sentinel for "not provided in this update" (-1.0)
    if (yes_bid >= 0.0) state.yes_bid = yes_bid;
    if (yes_ask >= 0.0) state.yes_ask = yes_ask;
    if (last_price >= 0.0) state.last_price = last_price;
    if (volume >= 0) state.volume = volume;
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

        update.yes_bid = parse_kalshi_price(j, "yes_bid");
        update.yes_ask = parse_kalshi_price(j, "yes_ask");
        update.last_price = parse_kalshi_price(j, "last_price");
        update.volume = j.value("volume", 0);
        update.timestamp = parse_timestamp(j);
        update.seq = parse_seq(j);

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

        delta.yes_bid = parse_kalshi_price(j, "yes_bid");
        delta.yes_ask = parse_kalshi_price(j, "yes_ask");
        // yes_bid_size / yes_ask_size migrate to _fp strings on
        // fractional-enabled markets; accept both shapes.
        delta.yes_bid_size = parse_kalshi_count_fp(j,
            j.contains("yes_bid_size_fp") ? "yes_bid_size_fp" : "yes_bid_size");
        delta.yes_ask_size = parse_kalshi_count_fp(j,
            j.contains("yes_ask_size_fp") ? "yes_ask_size_fp" : "yes_ask_size");
        delta.timestamp = parse_timestamp(j);
        delta.seq = parse_seq(j);

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
        fill.trade_id = j.value("trade_id", "");
        fill.ticker = j.value("ticker", "");
        fill.price = parse_kalshi_price(j, "price");
        // Fills carry count_fp (strings) on fractional markets, count elsewhere.
        fill.count = parse_kalshi_count_fp(j,
            j.contains("count_fp") ? "count_fp" : "count");
        fill.side = j.value("side", "");
        fill.action = j.value("action", "");
        fill.timestamp = parse_timestamp(j);
        fill.seq = parse_seq(j);

        // Account-fill messages must carry an order_id — it is what ties
        // the fill to a TrackedOrder in KalshiExchange. Public trade-tape
        // messages ("trade" channel) have a ticker and price but no
        // order_id; accepting those here would apply their cost/fee to
        // order_id="" and silently drain balance. Also require an action
        // string ("buy"/"sell") since that drives balance direction.
        if (fill.order_id.empty()) return std::nullopt;
        if (fill.ticker.empty()) return std::nullopt;
        if (fill.action != "buy" && fill.action != "sell") return std::nullopt;
        return fill;
    } catch (...) {
        return std::nullopt;
    }
}

bool KalshiWsClient::check_and_update_seq(const std::string& channel,
                                           const std::string& ticker,
                                           int64_t seq) {
    if (seq == 0) return true;  // no sequence tracking for this message

    std::lock_guard<std::mutex> lock(seq_mutex_);
    const std::string key = seq_key(channel, ticker);
    auto it = last_seq_.find(key);

    if (it == last_seq_.end()) {
        // First seq seen — accept it as the baseline.
        last_seq_[key] = seq;
        return true;
    }

    const int64_t expected = it->second + 1;
    if (seq == expected) {
        it->second = seq;
        return true;
    }

    // Gap (or out-of-order). Kalshi does not provide incremental resync;
    // the caller must resubscribe. We reset our tracking so the next snapshot
    // becomes the new baseline.
    spdlog::warn("WS seq gap on {} ticker={} expected={} got={}",
                 channel, ticker, expected, seq);
    last_seq_.erase(it);
    if (on_seq_gap_) on_seq_gap_(channel, ticker, expected, seq);
    return false;
}

int64_t KalshiWsClient::last_seq(const std::string& channel, const std::string& ticker) const {
    std::lock_guard<std::mutex> lock(seq_mutex_);
    auto it = last_seq_.find(seq_key(channel, ticker));
    return it == last_seq_.end() ? 0 : it->second;
}

void KalshiWsClient::reset_seq(const std::string& channel, const std::string& ticker) {
    std::lock_guard<std::mutex> lock(seq_mutex_);
    last_seq_.erase(seq_key(channel, ticker));
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
                check_and_update_seq("ticker", update->ticker, update->seq);
                update_book_state(update->ticker, update->yes_bid, update->yes_ask,
                                  update->last_price, update->volume);
                if (on_market_update_) on_market_update_(*update);
            }
        } else if (type == "orderbook_snapshot") {
            // Snapshot resets the seq baseline — next delta should be snapshot.seq + 1.
            if (auto delta = parse_orderbook_delta(data)) {
                reset_seq("orderbook_delta", delta->ticker);
                if (delta->seq > 0) check_and_update_seq("orderbook_delta", delta->ticker, delta->seq);
                update_book_state(delta->ticker, delta->yes_bid, delta->yes_ask, -1.0, -1);
                WsMarketUpdate update;
                update.ticker = delta->ticker;
                update.yes_bid = delta->yes_bid;
                update.yes_ask = delta->yes_ask;
                update.timestamp = delta->timestamp;
                update.seq = delta->seq;
                if (on_market_update_) on_market_update_(update);
            }
        } else if (type == "orderbook_delta") {
            if (auto delta = parse_orderbook_delta(data)) {
                // On gap: drop the local book AND re-issue the subscribe so the
                // server replays a fresh snapshot. Kalshi provides no partial
                // resync — resubscribe is the only recovery.
                if (!check_and_update_seq("orderbook_delta", delta->ticker, delta->seq)) {
                    {
                        std::lock_guard<std::mutex> lock(book_mutex_);
                        book_states_.erase(delta->ticker);
                    }
                    send_subscribe("orderbook_delta", {delta->ticker});
                    return;
                }
                // Pass -1 for fields not in orderbook_delta to preserve existing values
                update_book_state(delta->ticker, delta->yes_bid, delta->yes_ask, -1.0, -1);
                WsMarketUpdate update;
                update.ticker = delta->ticker;
                update.yes_bid = delta->yes_bid;
                update.yes_ask = delta->yes_ask;
                update.timestamp = delta->timestamp;
                update.seq = delta->seq;
                if (on_market_update_) on_market_update_(update);
            }
        } else if (type == "fill" || type == "user_order_fill" || type == "user_orders") {
            // The legacy `fill` channel and the new (Feb 3, 2026) `user_orders`
            // channel carry the same fill payload shape; user_orders also
            // delivers order-state change events, which we currently ignore
            // (parse_fill_message returns nullopt when order_id/action fields
            // are missing, filtering those out).
            spdlog::info("WS fill-type message received: type={} raw={}",
                         type, raw.substr(0, 300));
            if (auto fill = parse_fill_message(data)) {
                if (on_fill_) on_fill_(*fill);
            } else {
                spdlog::warn("WS fill message failed to parse. raw={}", raw.substr(0, 300));
            }
        } else {
            // Diagnostic: log any unhandled message types so we can see what
            // Kalshi is actually delivering — including subscribe ACKs, errors,
            // and any new/renamed channels.
            spdlog::info("WS unhandled msg: type='{}' raw={}", type, raw.substr(0, 300));
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to parse WS message: {}", e.what());
        if (on_error_) on_error_(e.what());
    }
}

} // namespace trader::kalshi
