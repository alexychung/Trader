#pragma once

#include "core/types.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/ws_reconnect.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <optional>
#include <memory>
#include <deque>
#include <condition_variable>

namespace trader::kalshi {

// Parsed WebSocket messages.
// `timestamp` is parsed from ts_ms (preferred, ms) if present, else from ts (deprecated, seconds).
// `seq` is the per-channel monotonic sequence number; 0 if not present.
struct WsOrderbookDelta {
    std::string ticker;
    double yes_bid = 0.0;
    double yes_ask = 0.0;
    int yes_bid_size = 0;
    int yes_ask_size = 0;
    Timestamp timestamp;
    int64_t seq = 0;
};

struct WsTrade {
    std::string ticker;
    double price = 0.0;
    int count = 0;
    std::string side;  // "yes" or "no"
    Timestamp timestamp;
    int64_t seq = 0;
};

struct WsFill {
    std::string order_id;
    std::string trade_id;  // server-unique per fill; used for dedup
    std::string ticker;
    double price = 0.0;
    int count = 0;
    std::string side;
    std::string action;  // "buy" or "sell"
    Timestamp timestamp;
    int64_t seq = 0;
};

struct WsMarketUpdate {
    std::string ticker;
    double yes_bid = 0.0;
    double yes_ask = 0.0;
    double last_price = 0.0;
    int volume = 0;
    Timestamp timestamp;
    int64_t seq = 0;
};

// Callbacks
using OnMarketUpdateCb = std::function<void(const WsMarketUpdate&)>;
using OnFillCb = std::function<void(const WsFill&)>;
using OnErrorCb = std::function<void(const std::string&)>;
// Fired after a successful reconnect + subscription replay. The caller is
// expected to trigger a REST fill reconcile to close any gap window — WS
// alone doesn't replay missed fills on reconnect (fix #6, 2026-05-20).
using OnReconnectCb = std::function<void()>;
// Signals a detected sequence gap on a given channel/ticker. The caller
// (or the client itself) must resubscribe and apply a fresh snapshot —
// Kalshi does not provide an incremental resync endpoint.
using OnSeqGapCb = std::function<void(const std::string& channel,
                                      const std::string& ticker,
                                      int64_t expected_seq,
                                      int64_t received_seq)>;

// Live-mode configuration. Default (enable_live=false) keeps the client in
// stub mode where connect() is a no-op that reports success — used by every
// unit test. Production wires `enable_live = true` so connect() opens a real
// WebSocket against Kalshi, spins up a reader thread, and runs a reconnect
// loop that replays the tracked subscriptions.
struct WsConfig {
    bool enable_live = false;
    BackoffConfig backoff{};
    // Beast's auto-ping interval. Kalshi closes idle connections ~60s, so
    // 25s gives two chances before the idle kill.
    std::chrono::seconds ping_interval{25};
    // Handshake timeout for the TLS + WebSocket upgrade.
    std::chrono::seconds handshake_timeout{15};
    // Consecutive reconnect failures before firing on_error_. The reconnect
    // loop itself never stops — transient Kalshi outages can easily last
    // minutes — but sustained failure usually means revoked credentials or
    // a misconfigured host, not network weather. At the default cap of 10,
    // with exponential backoff to 30s, the alert fires ~5 minutes in.
    // on_error is called once per threshold crossing, not every failure,
    // so the alert channel doesn't flood.
    int max_consecutive_failures_before_alert = 10;
};

class KalshiWsClient {
public:
    KalshiWsClient(const std::string& ws_url, KalshiAuth& auth);
    KalshiWsClient(const std::string& ws_url, KalshiAuth& auth, WsConfig cfg);
    ~KalshiWsClient();

    // Lifecycle
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_.load(); }

    // Subscriptions. Each call both (a) emits the subscribe message on the
    // current connection (via send_subscribe) and (b) records the subscription
    // so it can be replayed after a reconnect. Kalshi has no incremental
    // resync: on any disconnect we must resend every active subscribe.
    void subscribe_orderbook(const std::string& ticker);
    void subscribe_ticker(const std::string& ticker);
    void subscribe_fills();
    // `user_orders` is the replacement Kalshi rolled out Feb 3, 2026: a
    // strictly-ordered per-order stream that carries fill events with
    // `fee_cost` as a dollar string. New subscribers should prefer it over
    // the legacy `fill` channel. Messages still flow through the same
    // on_fill callback — on_fill dedups via trade_id so it is safe to keep
    // both channels subscribed during the migration window.
    void subscribe_user_orders();
    void unsubscribe(const std::string& channel, const std::string& ticker = "");

    // Snapshot of active subscriptions. Used by the reconnect path to replay
    // on a fresh session, and exposed publicly so tests can pin the policy.
    struct Subscription {
        std::string channel;   // "orderbook_delta", "ticker", "fill"
        std::string ticker;    // empty for channels with no per-ticker filter (e.g. fill)
        bool operator==(const Subscription& o) const {
            return channel == o.channel && ticker == o.ticker;
        }
    };
    std::vector<Subscription> active_subscriptions() const;

    // Re-issue every active subscription on the current connection. Called
    // automatically after a gap-induced resync or a reconnect; also callable
    // by tests.
    void replay_subscriptions();

    // Callbacks
    void set_on_market_update(OnMarketUpdateCb cb) { on_market_update_ = std::move(cb); }
    void set_on_fill(OnFillCb cb) { on_fill_ = std::move(cb); }
    void set_on_error(OnErrorCb cb) { on_error_ = std::move(cb); }
    void set_on_seq_gap(OnSeqGapCb cb) { on_seq_gap_ = std::move(cb); }
    // Fires after each successful reconnect + replay_subscriptions completes.
    // Use to trigger REST fill reconcile so fills delivered during the
    // outage are recovered. Without this, WS reconnect leaves a gap window.
    void set_on_reconnect(OnReconnectCb cb) { on_reconnect_ = std::move(cb); }

    // Returns the last-observed sequence on a channel for a ticker. 0 if never seen.
    int64_t last_seq(const std::string& channel, const std::string& ticker) const;
    // Clears sequence tracking for a channel+ticker after a fresh snapshot.
    void reset_seq(const std::string& channel, const std::string& ticker);

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
    // Returns true if seq is the expected next value or the first one seen.
    // Returns false and fires on_seq_gap_ otherwise. Zero seq (not present) is ignored.
    bool check_and_update_seq(const std::string& channel,
                              const std::string& ticker,
                              int64_t seq);

    // Forward-declared pimpl for the live Beast session (keeps Boost headers
    // out of the public header and out of every TU that includes this).
    struct LiveSession;
    // Reader-thread entrypoint. Runs connect/read/reconnect until stop_live_
    // is set. No-op + returns immediately if enable_live is false.
    void run_live_loop();
    // Thread-safe write path. Queues the JSON onto the outbound deque and
    // notifies the writer. In stub mode this is a logging no-op.
    void enqueue_write(const std::string& json);

    std::string ws_url_;
    KalshiAuth& auth_;
    WsConfig cfg_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_live_{false};
    // Set once when consecutive reconnect failures cross the alert threshold;
    // cleared on the next successful handshake. Ensures on_error_ is called
    // at most once per streak, not every backoff cycle.
    bool alerted_consecutive_failures_ = false;

    mutable std::mutex book_mutex_;
    std::unordered_map<std::string, BookState> book_states_;

    mutable std::mutex seq_mutex_;
    // key = channel + "|" + ticker
    std::unordered_map<std::string, int64_t> last_seq_;

    mutable std::mutex subs_mutex_;
    std::vector<Subscription> subscriptions_;

    // Outbound message queue — populated by subscribe/replay, drained by the
    // reader thread between reads. Guarded by write_mutex_.
    mutable std::mutex write_mutex_;
    std::condition_variable write_cv_;
    std::deque<std::string> outbound_;

    // Live Beast session (nullptr in stub mode).
    std::unique_ptr<LiveSession> live_;
    std::thread reader_thread_;

    OnMarketUpdateCb on_market_update_;
    OnFillCb on_fill_;
    OnErrorCb on_error_;
    OnSeqGapCb on_seq_gap_;
    OnReconnectCb on_reconnect_;
};

} // namespace trader::kalshi
