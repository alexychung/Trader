#pragma once

#include "core/types.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include "feeds/injury_news_feed.hpp"
#include "feeds/sharp_book_provider.hpp"
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
//   - Kalshi lists each game as TWO binary markets:
//       KXNBAGAME-{YYmonDDawayhome}-{HOME}  (YES = home team)
//       KXNBAGAME-{YYmonDDawayhome}-{AWAY}  (YES = away team)
//     The strategy matches the event prefix case-insensitively, parses the
//     contract-side suffix to determine which team is YES, and inverts the
//     model probability when YES = away. Each signal logs the ticker, the
//     yes-side mapping, and the fair/mid prices for audit.
//   - Only BUY YES signals are emitted. Each game has two opposing tickers,
//     so the side with positive edge naturally fires; buying NO on the
//     companion ticker would just hedge ourselves and pay 2× maker fees.
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
        // Minimum edge AFTER costs to fire a signal. Formula:
        //   edge_after_costs = fair_yes − target_price − fee_per_contract
        // where target_price = yes_ask (worst case) and the fee is the
        // Kalshi maker fee amortized across min_lot_size contracts.
        //
        // This is the post-2026-05-20 semantic — the previous default of
        // 0.04 measured `fair − mid`, which silently allowed negative
        // expected-value trades whenever the bid/ask spread was wide. The
        // new default 0.02 means "at least 2¢ of post-fee EV per contract"
        // and is comparable to the old gate AFTER accounting for half-
        // spread + fee on a typical KXNBAGAME book.
        double min_edge_threshold = 0.02;

        // Maximum tolerable model-vs-market disagreement before we refuse
        // to fire. Two reasons this is a SANITY GATE, not a "size up on
        // huge edge" gate:
        //   1) Stale or broken data — a candle-bar `bid=0.04/ask=0.05`
        //      on a one-possession game implies 4% win probability,
        //      which is nonsense. Real Kalshi books don't carry that
        //      kind of mispricing for >a few seconds; if our backtest
        //      sees one, it's almost certainly a data artifact.
        //   2) The "too good to be true" case in live trading. If our
        //      model says 70% and Kalshi says 3%, the sharps (DRW, SIG)
        //      have not left a 67¢ bill on the floor — far more likely
        //      we have stale data (model running on lagged score) or
        //      Kalshi has a news event we don't.
        // Both cases: skip the trade. Legitimate edges in close games
        // live in the 2-15% range, but late-game blowouts can legitimately
        // push edge up to 40-50% if the market is slow to follow the
        // arcsine. Default 0.50 catches the phantom 60-70%-edge signals
        // we saw in the candlestick backtest (broken bid=0.04 bars on
        // close one-possession games) without rejecting real blowout
        // edges.
        double max_edge_threshold = 0.50;

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

        // High-confidence gate (improved-directional v2, added 2026-05-19).
        // Skip the signal unless BOTH:
        //   |home_lead| >= min_abs_score_diff   AND
        //   home_wp <= max_uncertain_wp OR home_wp >= min_strong_wp
        // The arcsine model is most accurate when one side clearly leads; in
        // tight games it produces noisy probabilities near 0.5 that get picked
        // off by faster MMs with arena feeds.
        int min_abs_score_diff = 8;
        double max_uncertain_wp = 0.30;
        double min_strong_wp = 0.70;

        // Lot-size optimizer (#7, 2026-05-20). Kalshi maker fee is
        //   ceil(0.0175 × contracts × P × (1-P))  (CEIL to whole cent)
        // so a 1-contract fill ALWAYS pays at least 1¢ regardless of price.
        // At P=0.5 the raw fee is 0.44¢/contract; ceiling means 1-lot pays
        // 2.3× the per-contract rate of a 10-lot. We refuse to size below
        // min_lot_size to amortize the ceiling.
        int min_lot_size = 5;

        // Quote-timing jitter (#9, 2026-05-20). Per arXiv:2510.27334, RL MMs
        // leak behavioral patterns to faster counterparties via deterministic
        // refresh timing. We add ± this fraction of randomness to the
        // scoreboard poll interval so our quote-update pattern can't be
        // pattern-matched by HFT. 0.0 disables; 0.20 = ±20% jitter.
        double quote_jitter_pct = 0.20;

        // Pinnacle CLV gate (#1, 2026-05-20). When a sharp-book provider is
        // wired in (see ISharpBookProvider), this is the minimum deviation
        // between the Kalshi market mid and the sharp fair (with Kalshi
        // CHEAPER than sharp on the side we are buying) required to fire a
        // signal. 0.02 = 2¢. Strategy skips the gate when no sharp_fair is
        // available (Null provider returns nullopt).
        double min_clv_edge = 0.02;

        // Volume sanity gate. KXNBAGAME books with low total volume are
        // typically stale at signal time — the model probability has moved
        // but no counterparty has refreshed the quote. Default 100 contracts
        // (~$50 in two-way notional) rejects morning-of-game placeholders
        // and obvious dead markets.
        int min_market_volume = 100;

        // Pregame point spread default applied to the arcsine model (see
        // win_probability.hpp limitation: "Apply an offset for known spread
        // before calling"). Sign convention: positive means HOME is favored
        // by that many points pregame. Formula:
        //   effective_lead = current_lead - spread * (time_elapsed / 2880)
        // Per-game spreads override this default via
        // NbaGameSnapshot.pregame_spread (set by a feed when available).
        // Zero matches the original equal-strength-teams assumption.
        double default_pregame_spread = 0.0;
    };

    NbaStrategy(RiskManager& risk_manager,
                CalibrationLogger& calibration,
                NbaScoreFeed& score_feed,
                Config cfg = {});

    // Optional sharp-book provider for the CLV gate (#1). If not set, the
    // strategy falls back to arcsine-only fair value (no external check).
    // Setter rather than ctor arg to keep existing wiring backwards-compat.
    void set_sharp_book_provider(::trader::feeds::ISharpBookProvider* p) {
        sharp_book_ = p;
    }

    // Optional injury kill switch (#3). When set and the feed flags either
    // team in a game as frozen, the strategy skips that game entirely. The
    // caller is responsible for cancelling existing orders separately
    // (typically via KillSwitch or OrderManager).
    void set_injury_feed(::trader::feeds::IInjuryNewsFeed* f) {
        injury_feed_ = f;
    }

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

    // Snapshot accessor — exposed so sibling strategies (e.g. ResolutionLag)
    // can reuse the same cdn.nba.com scoreboard fetch instead of duplicating
    // the HTTP call. Mutates after every generate_signals() / refresh.
    const std::vector<NbaGameSnapshot>& last_snapshots() const {
        return last_snapshots_;
    }

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

    // Optional CLV gate. nullptr = no external sanity check (default).
    ::trader::feeds::ISharpBookProvider* sharp_book_ = nullptr;

    // Optional injury kill switch. nullptr = never freezes (default).
    ::trader::feeds::IInjuryNewsFeed* injury_feed_ = nullptr;
};

} // namespace trader::nba
