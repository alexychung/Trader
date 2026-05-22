#pragma once

// IKalshiPriceProvider that serves real historical Kalshi prices for
// backtest replay, sourced from KalshiRestClient::get_candlesticks.
//
// One provider instance per game. The constructor pre-fetches 1-minute
// candlesticks for both YES side-tickers (KXNBAGAME-...-HOME and
// KXNBAGAME-...-AWAY) across a window large enough to cover any
// reasonable NBA tipoff time on the game date. Results are cached to
// disk so re-runs don't hammer the API.
//
// WHY THIS MATTERS: SyntheticKalshiPriceProvider invents Kalshi prices
// as our_fair ± noise. It exercises strategy logic but tells us nothing
// about whether the strategy has real edge — by construction, the
// synthetic prices are correlated with our model output. The candle
// provider is the only way to honestly answer "would the strategy have
// triggered against the actual Kalshi book that day?"
//
// LIMITATIONS:
//   - 1-minute resolution. Sub-minute moves are invisible. Fine for the
//     NBA strategy which polls every 60s anyway.
//   - yes_bid_close / yes_ask_close are end-of-bar snapshots. If a quote
//     blipped mid-bar the strategy would see the close, not the blip.
//   - Old games may have empty data (Kalshi only added NBA market series
//     in 2024+). Returns nullopt → strategy treats market as absent.
//   - Wall-clock anchoring is approximate (see pbp_wall_clock_ts_sec in
//     replay_engine.cpp — 3x stretch on game-clock, anchored at midnight
//     UTC of game date). May be off by up to a few hours vs. actual
//     tipoff; the 1-minute bar resolution is forgiving but worth keeping
//     in mind.

#include "backtest/replay_engine.hpp"
#include "exchange/kalshi/rest_client.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace trader::backtest {

class KalshiCandlePriceProvider : public IKalshiPriceProvider {
public:
    // rest: live REST client (caller manages lifetime).
    // cache_dir: per-ticker JSON cache directory. Created if absent.
    KalshiCandlePriceProvider(::trader::kalshi::KalshiRestClient& rest,
                               std::string cache_dir);

    // Pre-fetch both home/away YES side tickers for a single game across
    // a wide-enough wall-clock window to cover any tipoff time on
    // game_date_iso. Returns true if at least one ticker returned a
    // non-empty candle series; false if both were empty (Kalshi has no
    // data for this game — replay_game will see nullopt on every quote).
    bool prefetch_game(const std::string& game_date_iso,
                        const std::string& home_tricode,
                        const std::string& away_tricode);

    // IKalshiPriceProvider — returns the candle whose [start, end) bar
    // contains wall_clock_ts_sec. nullopt if the ticker has no data or
    // the timestamp falls outside the cached window.
    std::optional<Quote> get_quote(const std::string& ticker,
                                    int64_t wall_clock_ts_sec) override;

    // Diagnostics.
    int cache_hits() const { return cache_hits_; }
    int cache_misses() const { return cache_misses_; }
    int empty_fetches() const { return empty_fetches_; }

    using KalshiCandle = ::trader::kalshi::KalshiRestClient::KalshiCandle;

    // Pure quote-selection logic — picks the candle covering `ts`, applies
    // the stale-quote filter, returns the resulting Quote (or nullopt).
    // Exposed for unit testing without a live REST client. Assumes
    // `candles` is sorted ascending by end_period_ts.
    static std::optional<Quote> select_quote(
        const std::vector<KalshiCandle>& candles, int64_t wall_clock_ts_sec);

private:

    // Resolve KXNBAGAME series ticker — the Kalshi candlestick endpoint
    // is GET /series/{series}/markets/{ticker}/candlesticks, and NBA
    // game series live under "KXNBAGAME".
    static constexpr const char* kSeries = "KXNBAGAME";

    std::string cache_path_for(const std::string& ticker) const;
    std::vector<KalshiCandle> load_from_cache(const std::string& ticker);
    void save_to_cache(const std::string& ticker,
                        const std::vector<KalshiCandle>& candles);

    // Fetch from REST (no cache), returning empty on failure.
    std::vector<KalshiCandle> fetch_uncached(const std::string& ticker,
                                              int64_t start_ts_sec,
                                              int64_t end_ts_sec);

    ::trader::kalshi::KalshiRestClient& rest_;
    std::string cache_dir_;
    // ticker → sorted-by-end_period_ts list of candles.
    std::unordered_map<std::string, std::vector<KalshiCandle>> by_ticker_;

    int cache_hits_ = 0;
    int cache_misses_ = 0;
    int empty_fetches_ = 0;
};

} // namespace trader::backtest
