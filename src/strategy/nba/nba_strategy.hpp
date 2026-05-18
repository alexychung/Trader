#pragma once

#include "core/types.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include "risk/risk_manager.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/nba/nba_score_feed.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace trader::nba {

// Re-export from trader::kalshi so NBA callers can pass these without verbose
// qualification. The calibration logger and market struct are venue-agnostic
// and happen to live under the kalshi namespace for historical reasons.
using ::trader::kalshi::CalibrationLogger;
using ::trader::kalshi::CalibrationRecord;
using ::trader::kalshi::KalshiMarket;

// Live NBA in-game trading strategy.
//
// V1 edge thesis (per research report 2026-05-18):
//   Arcsine "safe lead" garbage-time arbitrage. When our random-walk-derived
//   home win-probability differs from the market mid by ≥ min_edge_threshold
//   during the 3rd or 4th quarter, fire a maker order on the cheaper side.
//
// Assumptions / explicit limitations:
//   - Kalshi convention: YES side = HOME team (per Kalshi `yes_sub_title`
//     observed convention; we do not yet read that field to verify per-market).
//     If Kalshi ever lists a KXNBAGAME with YES = away, this strategy will
//     systematically take the wrong side. The strategy logs the ticker +
//     team mapping at every signal so a human review can catch it.
//   - WP is computed from regulation-clock only; OT markets stop trading.
//   - Strategy assumes equal-strength teams. No pre-game-spread adjustment in
//     v1; that's a v2 calibration item.
//   - Skips the final 60 seconds (formula breaks: fouling, 3-pt heaves).
//   - Skips when more than 24 minutes remain (1440s). The arcsine random walk
//     under-represents talent gaps over long horizons; first-half is
//     dominated by pre-game line, not score-diff dynamics.
//   - One position per market — we never stack YES on top of NO or scale up
//     after an entry. Simplification: re-entry requires settlement.
class NbaStrategy {
public:
    struct Config {
        // Minimum |our_WP − market_mid| to fire a signal. 4¢ default is a
        // best-guess starting point — sports books are tighter than weather
        // but you also pay fees on every contract (~0.4¢/side at P=0.5).
        // Tune via calibration over the first ~2 weeks of paper trading.
        double min_edge_threshold = 0.04;

        // Maximum book spread to accept. Wider than this, market is too
        // illiquid to trust the mid.
        double max_spread = 0.10;

        // Time-window gating, in seconds remaining in regulation.
        int min_seconds_remaining = 60;       // skip final minute
        int max_seconds_remaining = 1440;     // skip first half

        // Dollar cap per game (binary contracts cost $0-$1 each).
        double max_position_per_game_dollars = 50.0;

        // Fractional Kelly multiplier on top of binomial-Kelly bet size.
        // 0.25 mirrors the weather strategy default.
        double kelly_fraction = 0.25;

        // How often to refresh scoreboard from cdn.nba.com. The CDN updates
        // its JSON ~every 3-5s, so 5s polling is more than enough.
        std::chrono::seconds scoreboard_poll_interval{5};
    };

    NbaStrategy(RiskManager& risk_manager,
                CalibrationLogger& calibration,
                NbaScoreFeed& score_feed,
                Config cfg = {});

    // Update Kalshi-side market state. Called by main loop on each tick
    // with the full filtered KXNBAGAME-* market snapshot.
    void set_markets(const std::vector<KalshiMarket>& markets);

    // WS price update — overrides the REST snapshot for the rest of the tick.
    void on_market_update(const MarketUpdate& update);

    // Fill / settlement callbacks — for stats and calibration resolution.
    void on_fill(const Fill& fill);
    void on_settlement(const Settlement& settlement);

    // Polls scoreboard if poll_interval has elapsed since last fetch.
    // Returns true if a fetch actually happened, false if rate-limited.
    bool refresh_scoreboard_if_due();

    // Compute trade signals from current game state + market book.
    // Internally calls refresh_scoreboard_if_due().
    std::vector<TradeSignal> generate_signals();

    // Stats / introspection
    int signals_generated() const { return signals_generated_; }
    int trades_executed() const { return trades_executed_; }
    std::size_t games_tracked() const { return last_snapshots_.size(); }
    std::size_t live_games() const;

    const Config& config() const { return cfg_; }

private:
    RiskManager& risk_;
    CalibrationLogger& calibration_;
    NbaScoreFeed& score_feed_;
    Config cfg_;

    std::unordered_map<std::string, KalshiMarket> markets_;
    std::unordered_map<std::string, MarketUpdate> latest_updates_;
    std::vector<NbaGameSnapshot> last_snapshots_;
    Timestamp last_scoreboard_fetch_{};  // zero-init → first call always fetches

    int signals_generated_ = 0;
    int trades_executed_ = 0;
};

} // namespace trader::nba
