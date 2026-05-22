#pragma once

// Resolution-lag arbitrage strategy — the "boring" trade.
//
// THESIS (per 2026-05-20 research, [Indie Hackers post]):
// On Polymarket the strategy of buying contracts at $0.96-$0.99 on games
// that are effectively decided (one team up 20+ with 1 min left, or game
// status = final pending settlement) and holding to settlement produced an
// audited 600+ trades, 99.7% win rate, $2k → $200k+ over time. Per-trade
// ROI ~3% (buy at 0.97, settle at 1.00 = +3.1%) compounded over many small
// trades.
//
// Why it works on Kalshi too:
//   - Kalshi binary contracts settle to $1.00 or $0.00.
//   - There's a 30-second settlement timer after the underlying event
//     resolves (rules_primary: "settlement_timer_seconds": 30).
//   - During the closing minutes of a blowout, books frequently leave a
//     bid at 0.95-0.98 from market makers reducing inventory. The arcsine
//     model gives effective-probability ≥ 0.99 — buying at 0.97 captures
//     2-3¢ of essentially free edge minus fees.
//
// Distinct from NbaStrategy:
//   - NbaStrategy targets the directional edge (model vs. mid in mid-game).
//   - This targets the END-OF-GAME resolution lag — different setup, same
//     market. Both can run simultaneously without overlap because:
//       * NbaStrategy requires regulation_remaining ≤ 1440s AND
//         |lead| ≥ 8 AND wp outside [0.30, 0.70].
//       * ResolutionLag fires only when game is "effectively over": either
//         status == final, or (Q4 AND clock < 120s AND |lead| ≥ 15).
//
// SAFETY:
//   - Refuses to buy YES at ≥ max_entry_price (default 0.99 — paying $0.99
//     to win $1.00 = 1% ROI minus fees → break-even at best).
//   - Refuses to buy YES at ≤ min_entry_price (default 0.94 — implies the
//     market thinks there's a real chance the favorite loses; not the
//     setup we want).
//   - Skips ticker if we already hold a position (no stacking).
//   - Honors RiskManager and KillSwitch like every other strategy.

#include "core/types.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include "risk/risk_manager.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/nba/nba_score_feed.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace trader::nba {

using ::trader::kalshi::CalibrationLogger;
using ::trader::kalshi::CalibrationRecord;
using ::trader::kalshi::KalshiMarket;

class ResolutionLagStrategy {
public:
    struct Config {
        // The "effectively decided" detector. Game qualifies if EITHER:
        //   (a) status == final / status == 3 (game ended, awaiting settle)
        //   (b) Q4 AND game_clock_seconds <= cutoff_clock_seconds
        //       AND |home_lead| >= cutoff_lead_points
        int cutoff_clock_seconds = 120;   // last 2 min of Q4
        int cutoff_lead_points = 15;      // 15-pt lead at that point = ~99% sure
        bool include_status_final = true;

        // Price window for the "boring arb." Buy YES only if:
        //   min_entry_price <= yes_ask <= max_entry_price
        double min_entry_price = 0.94;
        double max_entry_price = 0.99;

        // Position-sizing cap (dollars per game).
        double max_position_per_game_dollars = 25.0;
        // Per-trade minimum lot size (fee ceil — see NbaStrategy lot-size).
        int min_lot_size = 5;
        // Volume sanity gate — skip markets with low total volume to avoid
        // firing into stale 0.95-bid placeholders left over from earlier
        // in the day. ResolutionLag fires on tight settle-edges, so a
        // stale book is more dangerous here than for NbaStrategy.
        int min_market_volume = 50;
    };

    ResolutionLagStrategy(RiskManager& risk_manager,
                           CalibrationLogger& calibration,
                           Config cfg = {});

    // Same market-update plumbing as NbaStrategy — the main loop hands the
    // KXNBAGAME-filtered cached markets list each tick.
    void set_markets(const std::vector<KalshiMarket>& markets);
    void set_snapshots(const std::vector<NbaGameSnapshot>& snapshots) {
        last_snapshots_ = snapshots;
    }
    void on_market_update(const MarketUpdate& update);
    void on_settlement(const Settlement& settlement);

    std::vector<TradeSignal> generate_signals();

    int signals_generated() const { return signals_generated_; }
    const Config& config() const { return cfg_; }

private:
    RiskManager& risk_;
    CalibrationLogger& calibration_;
    Config cfg_;

    std::unordered_map<std::string, KalshiMarket> markets_;
    std::unordered_map<std::string, MarketUpdate> latest_updates_;
    std::vector<NbaGameSnapshot> last_snapshots_;

    int signals_generated_ = 0;
};

} // namespace trader::nba
