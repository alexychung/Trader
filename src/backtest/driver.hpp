#pragma once

#include "backtest/settled_markets_fetcher.hpp"
#include "core/staleness_gate.hpp"
#include "core/types.hpp"
#include "exchange/mock_exchange.hpp"
#include "strategy/kalshi/event_strategy.hpp"
#include "strategy/kalshi/probability_engine.hpp"
#include "strategy/kalshi/weather_feed.hpp"
#include <string>
#include <vector>

namespace trader::backtest {

// Replays settled Kalshi markets through a KalshiEventStrategy instance.
// Only weather markets are wired in V1 — economic model plumbing is not yet
// hooked up in the live stack (see CpiModel::set_cpi_data) so backtest coverage
// of those categories would only test the filter/sizer paths, not the model.
class BacktestDriver {
public:
    struct Config {
        std::string weather_cache_dir = "data/cache/backtest/weather";
        // A decision point is emitted whenever the yes-price has moved by at
        // least this much (in dollars) since the last emitted decision.
        double min_price_move = 0.02;
        // Hard cap per market so liquid markets don't dominate.
        int max_decisions_per_market = 12;
        // Skip decision points this many hours before resolution.
        int skip_last_hours = 1;
        // Fallback decision-points when a market has no trade history.
        int fallback_decisions_per_market = 3;

        // Fallback cumulative volume to assume when the cached Kalshi market
        // response lacks a volume field entirely (different field names
        // across the 2026 migration). Use only when the server didn't
        // return a number — not a free pass for zero-volume markets.
        // Default 0 means: reject zero-volume markets honestly. Raise to
        // `MarketFilter::min_volume` if you want the filter to always
        // pass in replay (risky — masks a real live-trading constraint).
        int fallback_volume_when_missing = 0;

        // Slippage model — the backtest's execution is a fiction unless we
        // model the cost of actually crossing the book. Three knobs:
        //
        // half_spread: the bracket width around the observed trade price
        //   used to synthesize yes_bid/yes_ask. Kalshi demo books are 3-20¢
        //   wide on weather markets; production settled-max markets
        //   typically 5-10¢. The strategy's edge calculation uses yes_ask,
        //   so a wider bracket tightens realized edge. 3.5¢ (→ 7¢ spread)
        //   reflects what we observed on demo in April 2026.
        //
        // adverse_fill_slippage: extra adverse move applied to the logged
        //   entry_price when computing PnL in the report. Models the
        //   reality that post-only orders can sit in queue while price
        //   moves against you — or, more often on Kalshi, that the strategy
        //   has to cross as taker and eats the near-book depth. 2¢ is a
        //   realistic penalty on thin weather books.  This does NOT feed
        //   back into the strategy's edge decision (the strategy can't know
        //   the future) — it only discounts the reported PnL to reflect a
        //   plausible fill penalty.
        //
        // assume_taker_fills: propagated to BacktestReport::Config. See the
        //   rationale there — defaults to true because our demo trials show
        //   post-only-at-ask gets rejected, so the live bot crosses as
        //   taker and pays ~4× the maker fee.
        double half_spread = 0.035;
        double adverse_fill_slippage = 0.02;
        bool assume_taker_fills = true;
    };

    struct CoverageEntry {
        std::string ticker;
        std::string category;
        bool attempted = false;
        std::string skip_reason;
        int decisions = 0;
    };

    struct RunResult {
        std::vector<CoverageEntry> coverage;
        int decisions_made = 0;
        int settlements_applied = 0;
        int orders_placed = 0;
    };

    BacktestDriver(trader::kalshi::KalshiEventStrategy& strategy,
                   trader::kalshi::ProbabilityEngine& prob_engine,
                   std::shared_ptr<trader::kalshi::WeatherEnsembleModel> weather_model,
                   trader::StalenessGate& staleness,
                   trader::MockExchange& exchange,
                   Config config = {});

    RunResult run(std::vector<SettledMarket> markets);

private:
    trader::kalshi::KalshiEventStrategy& strategy_;
    trader::kalshi::ProbabilityEngine& prob_engine_;
    std::shared_ptr<trader::kalshi::WeatherEnsembleModel> weather_model_;
    trader::StalenessGate& staleness_;
    trader::MockExchange& exchange_;
    Config config_;
};

// Parse Kalshi ISO 8601 timestamp (e.g., "2026-04-20T15:00:00Z") to Timestamp.
// Returns epoch on parse failure.
Timestamp parse_iso8601(const std::string& s);

// Format a Timestamp as "YYYY-MM-DD".
std::string format_date_utc(Timestamp ts);

// Format a Timestamp as ISO 8601 UTC "YYYY-MM-DDTHH:MM:SSZ".
std::string format_iso8601(Timestamp ts);

} // namespace trader::backtest
