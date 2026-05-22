#pragma once

#include "core/types.hpp"
#include "exchange/kalshi/auth.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <memory>

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
    // When true, this market accepts sub-contract fractional fills via
    // `count_fp`. Our strategy/position layer still tracks `int quantity`
    // which silently truncates those fractions on the wire boundary;
    // MarketFilter rejects fractional-enabled markets until the quantity
    // type is migrated end-to-end.
    bool fractional_trading_enabled = false;
};

// Historical / account fill, returned by GET /portfolio/fills. Shape matches
// the fields the WS `fill` channel streams, except `trade_id` is included
// so callers can dedup against already-seen fills during reconnection
// reconciliation. `fee_cost` is the dollar fee reported by the server as a
// 4-decimal string post Jan 29 2026.
struct KalshiFill {
    std::string order_id;
    std::string trade_id;
    std::string ticker;
    double price = 0.0;
    int count = 0;
    std::string side;     // "yes" or "no"
    std::string action;   // "buy" or "sell"
    int64_t created_ts_ms = 0;  // server epoch ms
    double fee_cost = 0.0;
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
    // Paginated version — walks the Kalshi `cursor` field until exhausted or
    // until `max_pages` have been fetched (safety cap). `page_limit` is the
    // per-request page size (Kalshi caps at 1000). Returns every market across
    // every page. Use this for exhaustive catalog scans; the non-paginated
    // form is fine when you're fetching a known-bounded batch.
    std::vector<KalshiMarket> get_markets_paginated(const std::string& status = "open",
                                                     int page_limit = 1000,
                                                     int max_pages = 20,
                                                     const std::string& series_ticker = "");
    std::optional<KalshiMarket> get_market(const std::string& ticker);
    KalshiOrderbook get_orderbook(const std::string& ticker);

    // Historical candlestick (OHLC) data for a market. Used by the backtest
    // replay engine — without full book history (Kalshi doesn't expose tick
    // data publicly), candlesticks are the highest-resolution free proxy.
    //
    // Endpoint: GET /series/{series}/markets/{ticker}/candlesticks
    // period_interval: 1 = 1-minute bars, 60 = 1-hour, 1440 = 1-day.
    // start_ts / end_ts are unix epoch SECONDS (NOT ms).
    struct KalshiCandle {
        int64_t end_period_ts = 0;   // unix epoch seconds (close of bar)
        int open_interest = 0;
        int volume = 0;
        // Last-trade-price OHLC across the bar. Zero if no trades in the
        // bar (Kalshi reports null/missing — we map to 0).
        double price_open = 0.0;
        double price_high = 0.0;
        double price_low = 0.0;
        double price_close = 0.0;
        // YES-side bid/ask OHLC across the bar. Useful for replay since
        // strategy signals depend on bid/ask, not just last trade.
        double yes_bid_close = 0.0;
        double yes_ask_close = 0.0;
    };
    std::vector<KalshiCandle> get_candlesticks(const std::string& series_ticker,
                                                const std::string& ticker,
                                                int64_t start_ts_sec,
                                                int64_t end_ts_sec,
                                                int period_interval_min = 1);

    // Pure JSON parser for the candlestick response. Exposed for unit tests.
    static std::vector<KalshiCandle> parse_candlesticks(
        const nlohmann::json& body);

    // Batch orderbook fetch (GET /markets/orderbooks, landed Mar 30 2026).
    // Returns a map keyed by ticker — entries absent from the map were not
    // returned by the server (ticker unknown or still loading). Kalshi caps
    // a single request at 100 tickers; we transparently split larger lists.
    // Cuts cold-start seed from N serial round-trips (~300ms + RTT each) to
    // ceil(N/100) parallel-safe round-trips.
    std::unordered_map<std::string, KalshiOrderbook> get_orderbooks(
        const std::vector<std::string>& tickers);

    // Account
    double get_balance();

    // Per-account rate-limit snapshot. Kalshi tiers accounts by volume; new
    // accounts start at Basic (low RPS caps), Prime tiers get higher caps.
    // Call at startup to log what we're working with — our current order
    // submission cadence assumes ≥5 RPS, which Basic barely hits. `tier`
    // is a free-form string ("Basic", "Prime", etc.). All fields default
    // to 0 / empty if the endpoint is unavailable or the response is a
    // shape we don't recognize.
    struct AccountLimits {
        std::string tier;
        int read_rps = 0;
        int write_rps = 0;
        int max_open_orders = 0;
    };
    AccountLimits get_account_limits();

    // Per-series fee overrides. Kalshi defaults to 0.07 taker / 0.0175 maker
    // but some series now have custom schedules (Sep 21, 2025). The
    // `series_ticker` is the underlying (e.g. "KXCPI"); rates are decimal
    // fractions, same units as the global formula. Missing series keys
    // imply the global default. Call once at startup; Kalshi changes
    // schedules infrequently enough that a per-session cache is fine.
    struct FeeOverride {
        double maker_rate = 0.0175;
        double taker_rate = 0.07;
    };
    std::unordered_map<std::string, FeeOverride> get_fee_schedule();

    // Reconciliation — fetch fills with server timestamp >= min_ts_ms. Kalshi
    // uses this endpoint to let callers catch up on fills missed during a WS
    // disconnect: subscribe/fill WS messages are not re-sent on reconnect.
    // Returns fills ordered oldest-first so a sequential replay preserves
    // the original fill order. `max_pages` caps pagination to avoid runaway
    // pulls on a brand-new account with no prior watermark.
    std::vector<KalshiFill> get_fills_since(int64_t min_ts_ms,
                                             int page_limit = 1000,
                                             int max_pages = 20);

    // Server-side market positions. Kalshi returns both event-level and
    // market-level aggregates — we only need market-level because our risk
    // manager tracks per-ticker. Each entry is a *net* position with
    // `position` = total shares held (signed: positive for YES-leaning,
    // negative for NO-leaning) and `cost` = total_traded_dollars less any
    // already-realized PnL.  Use at startup to seed positions so the risk
    // gate doesn't let the bot stack orders on top of an already-funded
    // position.  Skips entries with position=0 (previously closed).
    struct MarketPosition {
        std::string ticker;
        int position = 0;          // signed shares
        double cost = 0.0;         // dollars
        double fees_paid = 0.0;
        double realized_pnl = 0.0;
    };
    std::vector<MarketPosition> get_market_positions();

    // Pure JSON parsing of a /portfolio/positions response. Extracted from
    // get_market_positions() for unit testability — pass a parsed nlohmann
    // body and get a vector back with no HTTP in the loop.
    static std::vector<MarketPosition> parse_market_positions(
        const nlohmann::json& body);

    // Orders
    OrderId place_order(const std::string& ticker, const std::string& side,
                        const std::string& action, double price, int quantity,
                        bool post_only = true);
    bool cancel_order(const OrderId& order_id);

    // Amend a resting order's price and/or quantity in place.
    // POST /portfolio/orders/{order_id}/amend
    //
    // Wire format mirrors place_order: `yes_price_dollars` as 4-decimal
    // string, `count_fp` as 2-decimal string. (An earlier version sent an
    // int-cents `yes_price`; that was wrong and was fixed 2026-05-20.)
    //
    // Returns true on success. Queue-position semantics per Kalshi docs are
    // ambiguous: increasing count at the same price forfeits queue, but the
    // price-change case is undocumented. Empirical assumption (consistent
    // with most CDA exchanges): amend-down-price keeps queue, amend-up-price
    // moves you to the back. Verify in shadow before relying on it.
    //
    // Pass a non-positive value to leave that field unchanged.
    bool amend_order(const OrderId& order_id, double new_price = -1.0,
                      int new_quantity = -1);

    // Batch cancel: DELETE /portfolio/orders/batched. Per Kalshi's rate
    // limiting, each cancel counts as 0.2 write transactions vs 1.0 for
    // serial cancels — ~5x cheaper. Transparently splits into <=20-id
    // batches (Kalshi cap).
    //
    // Return contract:
    //   - std::nullopt      = endpoint unavailable (403/404). Caller must
    //                         fall back to serial for ALL ids.
    //   - vector<OrderId>   = ids the server explicitly confirmed cancelled.
    //                         Empty vector means nothing was confirmed (a
    //                         partial HTTP error or empty response); caller
    //                         should retry uncovered ids serially.
    //
    // We deliberately do NOT return an optimistic "assume all cancelled"
    // list when the response shape lacks an accounting — doing so caused
    // survivors to be marked Cancelled locally and then skipped by the
    // serial-retry fallback, silently leaving orders resting on the venue.
    std::optional<std::vector<OrderId>> batch_cancel_orders(
        const std::vector<OrderId>& order_ids);

    // Raw HTTP (for testing/extension)
    HttpResponse get(const std::string& path);
    HttpResponse post(const std::string& path, const nlohmann::json& body);
    HttpResponse del(const std::string& path);

    // Market cache. `category` is passed to Kalshi as a query param but is
    // being deprecated (Mar 20, 2026) and already returns inconsistent results
    // on demo — the API ignores unknown values and returns everything.
    void refresh_market_cache(const std::string& category = "");
    // Filter the cache down to markets whose ticker starts with any of the
    // given prefixes AND does not contain any substring in `excludes`. Use
    // this in preference to `refresh_market_cache("weather")` — e.g.
    // `refresh_markets_by_ticker_prefix({"KXHIGH","KXHIGHT"}, {"INFLATION"})`
    // for every high-temp market while skipping `KXHIGHINFLATION` (an
    // economic contract that shares the KXHIGH prefix). Walks the catalog
    // via cursor pagination, so page_limit is per-page, not total.
    void refresh_markets_by_ticker_prefix(
        const std::vector<std::string>& prefixes,
        const std::vector<std::string>& excludes = {},
        int page_limit = 1000);

    // Refresh the cache by Kalshi series ticker, which is a server-side filter.
    // ~1000x cheaper than refresh_markets_by_ticker_prefix on prod, where the
    // full open-markets catalog runs ~300k entries and series like KXNBAGAME
    // sort beyond any reasonable page cap. Use this when you know the exact
    // series (e.g. "KXNBAGAME"); use the prefix variant only for fuzzy lookups
    // that need to walk multiple series at once.
    void refresh_markets_by_series(const std::string& series_ticker,
                                    int page_limit = 1000);
    const std::unordered_map<std::string, KalshiMarket>& cached_markets() const { return market_cache_; }

    // Parse Kalshi JSON responses
    static KalshiMarket parse_market(const nlohmann::json& j);

private:
    // Forward-declared pimpl for ssl::context — keeps Boost headers out of callers.
    // ssl::context loads the OS cert store on construction (~10ms + a few MB RSS);
    // it's the expensive part of each request, so we construct it once and reuse.
    struct SslState;

    std::string base_url_;
    KalshiAuth& auth_;
    std::unordered_map<std::string, KalshiMarket> market_cache_;
    Timestamp cache_time_;
    std::shared_ptr<SslState> ssl_state_;  // shared_ptr so the type stays complete in .cpp
};

} // namespace trader::kalshi
