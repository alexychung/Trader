#pragma once

// Backtest replay engine — runs NbaStrategy and ResolutionLagStrategy
// against historical NBA play-by-play data, with a configurable Kalshi
// price provider.
//
// V1 SCOPE:
//   The engine takes one game's PBP events and replays them in order.
//   At each event we construct an NbaGameSnapshot matching what the live
//   bot would see, hand it to the strategy, and capture the signals it
//   generates. A SyntheticKalshiPriceProvider gives Kalshi prices as
//   `our_fair ± half_spread + noise`. This is OPTIMISTIC about strategy
//   profitability (it pretends Kalshi is always priced very close to our
//   own model) — its purpose is to verify the strategy logic, the rate
//   at which it fires, and how its gates behave on real game state.
//
//   For a strategy-quality backtest, swap in a KalshiCandleProvider built
//   from real historical candlestick data (KalshiRestClient::get_candlesticks).
//   That comparison — strategy signals vs. real Kalshi book at the same
//   moment — is the truth-test of whether there is edge.
//
// FILL MODEL:
//   Every signal is assumed to fill at signal.market_price (the worst-case
//   ask the strategy logged). Maker post-only routing in live would often
//   fill at a better price OR not fill at all; modeling either accurately
//   requires resting-quote queue simulation we deliberately omit in V1.
//   The fill rate is therefore an OPTIMISTIC upper bound for taker mode
//   and uncalibrated for maker mode.
//
// SETTLEMENT:
//   At end-of-game the engine settles all open simulated positions to
//   $1.00 (winning side) or $0.00 (losing side) — Kalshi's actual binary
//   payoff. Maker fees are subtracted from PnL at entry; no settle fee.

#include "core/types.hpp"
#include "backtest/nba_pbp_fetcher.hpp"
#include "strategy/nba/nba_strategy.hpp"
#include "strategy/nba/resolution_lag_strategy.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trader::backtest {

// Provider abstraction so callers can swap synthetic prices for real
// Kalshi candlestick data without touching the replay loop.
class IKalshiPriceProvider {
public:
    virtual ~IKalshiPriceProvider() = default;
    // Returns (yes_bid, yes_ask, volume) for the given side ticker at the
    // given approximate wall-clock time (epoch seconds). nullopt if no
    // data is available for the requested time.
    struct Quote {
        double yes_bid = 0.0;
        double yes_ask = 0.0;
        int volume = 0;
    };
    virtual std::optional<Quote> get_quote(const std::string& ticker,
                                            int64_t wall_clock_ts_sec) = 0;
};

// Synthetic provider: prices each side at our_fair ± half_spread, with
// optional white noise. Stateful — caller updates `current_home_wp` as
// the game progresses.
class SyntheticKalshiPriceProvider : public IKalshiPriceProvider {
public:
    SyntheticKalshiPriceProvider(const std::string& home_tricode,
                                  const std::string& away_tricode,
                                  double half_spread = 0.02,
                                  double noise_stdev = 0.01);
    void set_home_wp(double wp) { current_home_wp_ = wp; }

    std::optional<Quote> get_quote(const std::string& ticker,
                                    int64_t /*wall_clock_ts_sec*/) override;

private:
    std::string home_upper_;
    std::string away_upper_;
    double half_spread_;
    double noise_stdev_;
    double current_home_wp_ = 0.5;
};

struct SimFill {
    int regulation_seconds_remaining = 0;
    int period = 0;
    int home_score = 0;
    int away_score = 0;
    std::string ticker;
    std::string contract_side;   // "yes" or "no"
    bool yes_is_home = true;     // for telemetry; derived from ticker suffix
    int quantity = 0;
    double entry_price = 0.0;
    double model_probability = 0.0;
    double maker_fee = 0.0;
    std::string strategy;        // "arcsine" or "resolution-lag"
};

struct GameReplayResult {
    std::string game_id;
    std::string date_iso;
    std::string home_tricode;
    std::string away_tricode;
    int home_final_score = 0;
    int away_final_score = 0;
    bool home_won = false;

    std::vector<SimFill> fills;
    double settled_pnl = 0.0;     // sum of (settle - entry) * qty - fees, all fills
    double total_fees = 0.0;
    int signals_arcsine = 0;
    int signals_resolution_lag = 0;
    // (model_probability, outcome) pairs for Brier-score aggregation.
    // outcome is 1 if the contract paid out (we held YES on the winner OR
    // NO on the loser), 0 otherwise.
    std::vector<std::pair<double, int>> brier_samples;
};

struct ReplayConfig {
    ::trader::nba::NbaStrategy::Config nba;
    ::trader::nba::ResolutionLagStrategy::Config reslag;
    bool run_resolution_lag = true;
    // Starting bankroll for the simulated risk manager. Backtest is run
    // game-by-game so this is the per-game starting capital — set high
    // enough that the Kelly sizing isn't clipped.
    double simulated_bankroll = 10000.0;
    // Per-game spread to associate with each replayed game. 0 = use
    // arcsine's equal-strength default. Per-game override hook for when
    // real spreads are wired.
    double pregame_spread = 0.0;
};

class BacktestReplay {
public:
    explicit BacktestReplay(ReplayConfig cfg) : cfg_(std::move(cfg)) {}

    // Replay one game. The provider must be alive for the duration of the
    // call. Returns per-game metrics including all fills + settled PnL.
    GameReplayResult replay_game(const PbpGameSummary& summary,
                                  const std::vector<PbpEvent>& events,
                                  IKalshiPriceProvider& price_provider);

private:
    ReplayConfig cfg_;
};

// Aggregate metrics across many games.
struct BacktestSummary {
    int games_replayed = 0;
    int total_signals = 0;     // arcsine + reslag
    int total_fills = 0;
    double total_pnl = 0.0;
    double total_fees = 0.0;
    int wins = 0;              // signals that settled in our favor
    int losses = 0;
    double brier_score = 0.25; // 0.25 = random baseline
    int brier_samples = 0;
    // Maximum cumulative drawdown across the chronological order of games.
    double max_drawdown = 0.0;
    // Per-game breakdown — useful for the CSV output.
    std::vector<GameReplayResult> per_game;
};

BacktestSummary aggregate(const std::vector<GameReplayResult>& per_game);

} // namespace trader::backtest
