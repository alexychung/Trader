#include "strategy/nba/nba_strategy.hpp"

#include "strategy/nba/kalshi_nba_parser.hpp"
#include "strategy/nba/win_probability.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>

namespace trader::nba {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool parse_date_iso(const std::string& iso,
                     int& yy, int& mm, int& dd) {
    if (iso.size() < 10) return false;
    try {
        int yyyy = std::stoi(iso.substr(0, 4));
        mm = std::stoi(iso.substr(5, 2));
        dd = std::stoi(iso.substr(8, 2));
        yy = yyyy % 100;
    } catch (...) { return false; }
    return mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31;
}

// Binomial Kelly fraction for a "buy YES at price p" with true probability q.
// f* = (q*b - (1-q)) / b   where b = (1-p)/p is the payoff odds.
// Returns 0 (no bet) if the edge is non-positive.
double binomial_kelly(double q, double p) {
    if (p <= 0.0 || p >= 1.0) return 0.0;
    double b = (1.0 - p) / p;
    double f = (q * b - (1.0 - q)) / b;
    return std::max(0.0, f);
}

} // namespace

NbaStrategy::NbaStrategy(RiskManager& risk_manager,
                          CalibrationLogger& calibration,
                          NbaScoreFeed& score_feed,
                          Config cfg)
    : risk_(risk_manager), calibration_(calibration),
      score_feed_(score_feed), cfg_(cfg) {}

void NbaStrategy::set_markets(const std::vector<KalshiMarket>& markets) {
    markets_.clear();
    for (const auto& m : markets) {
        markets_[m.ticker] = m;
    }
}

void NbaStrategy::on_market_update(const MarketUpdate& update) {
    latest_updates_[update.ticker] = update;
}

void NbaStrategy::on_fill(const Fill& fill) {
    (void)fill;
    ++trades_executed_;
}

void NbaStrategy::on_settlement(const Settlement& settlement) {
    calibration_.resolve(settlement.ticker, settlement.outcome, settlement.pnl);
    spdlog::info("NBA settlement: {} → {} (PnL: ${:.2f})",
                 settlement.ticker, settlement.outcome ? "YES" : "NO",
                 settlement.pnl);
}

bool NbaStrategy::refresh_scoreboard_if_due() {
    auto now = std::chrono::system_clock::now();

    // Quote-timing jitter (#9): randomize the effective poll interval by
    // ±quote_jitter_pct of the base. Each call computes a fresh jittered
    // threshold so HFT pattern-detectors can't lock onto our cadence.
    auto base_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        cfg_.scoreboard_poll_interval);
    std::chrono::milliseconds effective_interval = base_ms;
    if (cfg_.quote_jitter_pct > 0.0) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<double> dist(-cfg_.quote_jitter_pct,
                                                     cfg_.quote_jitter_pct);
        double mult = 1.0 + dist(rng);
        effective_interval = std::chrono::milliseconds(
            static_cast<int64_t>(base_ms.count() * mult));
    }

    if (last_scoreboard_fetch_.time_since_epoch().count() != 0 &&
        now - last_scoreboard_fetch_ < effective_interval) {
        return false;
    }
    last_snapshots_ = score_feed_.fetch_today_scoreboard();
    last_scoreboard_fetch_ = now;
    std::size_t live = std::count_if(last_snapshots_.begin(), last_snapshots_.end(),
                                      [](const NbaGameSnapshot& g) { return g.is_live(); });
    spdlog::debug("NBA scoreboard refresh: {} games ({} live)",
                  last_snapshots_.size(), live);
    return true;
}

std::size_t NbaStrategy::live_games() const {
    return std::count_if(last_snapshots_.begin(), last_snapshots_.end(),
                          [](const NbaGameSnapshot& g) { return g.is_live(); });
}

std::vector<TradeSignal> NbaStrategy::generate_signals() {
    std::vector<TradeSignal> signals;
    refresh_scoreboard_if_due();

    // Injury kill switch (#3) — opportunistic refresh on signal cycle. Real
    // implementations rate-limit internally; cheap to call.
    if (injury_feed_ != nullptr) {
        injury_feed_->refresh();
    }

    for (const auto& game : last_snapshots_) {
        if (!game.is_live()) continue;

        // Time-window gating.
        const int t = game.regulation_seconds_remaining;
        if (t < cfg_.min_seconds_remaining || t > cfg_.max_seconds_remaining) {
            continue;
        }

        // Injury kill switch — skip games where either team has fresh
        // injury news. The freeze duration is the feed's responsibility.
        if (injury_feed_ != nullptr) {
            if (injury_feed_->should_freeze_team(game.home_tricode) ||
                injury_feed_->should_freeze_team(game.away_tricode)) {
                spdlog::info("NBA: skip {}@{} — injury freeze active",
                             game.away_tricode, game.home_tricode);
                continue;
            }
        }

        // Date → 2-digit year/month/day so we can format the Kalshi ticker.
        int yy = 0, mm = 0, dd = 0;
        if (!parse_date_iso(game.game_date_iso, yy, mm, dd)) {
            spdlog::warn("NBA: bad game_date_iso for {} vs {}: '{}'",
                         game.away_tricode, game.home_tricode, game.game_date_iso);
            continue;
        }
        // Build the event prefix (no contract-side suffix). Kalshi lists each
        // game as two binary markets: KXNBAGAME-{event}-{HOME} and
        // KXNBAGAME-{event}-{AWAY}, where the suffix is the team whose win
        // resolves YES. We match case-insensitively against the event part.
        const std::string event_prefix = format_nba_game_ticker(
            yy, mm, dd, to_lower(game.away_tricode), to_lower(game.home_tricode));
        const std::string event_prefix_upper = to_upper(event_prefix);
        const std::string home_upper = to_upper(game.home_tricode);
        const std::string away_upper = to_upper(game.away_tricode);

        // Pregame-spread-adjusted lead. The arcsine model assumes equal-
        // strength teams; in reality a 5-pt lead by a 7-pt favorite is no
        // real edge over expectation, while a 5-pt lead by a 7-pt dog is
        // a meaningful overperformance. We subtract `spread * fraction of
        // game elapsed` from the raw lead so the model evaluates excess
        // performance vs the pregame line.
        //   pregame_spread > 0  → home favored
        //   time_elapsed = 2880 - t  (regulation is 2880 sec total)
        //   adjusted_lead = home_lead - spread * (time_elapsed / 2880)
        // When neither the per-game spread nor the strategy default is set,
        // this reduces to the raw lead (original behavior).
        const double spread_pts = (game.pregame_spread != 0.0)
                                    ? game.pregame_spread
                                    : cfg_.default_pregame_spread;
        constexpr double kRegSeconds = 2880.0;
        const double time_elapsed = kRegSeconds - static_cast<double>(t);
        const double adjusted_lead =
            static_cast<double>(game.home_lead()) -
            spread_pts * (time_elapsed / kRegSeconds);

        // Compute home win-probability once per game.
        const double home_wp = WinProbability::survival_probability(
            adjusted_lead, static_cast<double>(t));

        // High-confidence gate: require BOTH a meaningful directional lead
        // AND a model probability clearly off 50/50. The lead check uses
        // ADJUSTED lead (after the pregame-spread offset) — a 5-pt lead by
        // a 10-pt dog is +14 adjusted, which is the actual edge signal.
        // Comparing raw lead here would systematically reject the dog-
        // overperformance scenarios where spread adjustment matters most.
        if (std::abs(adjusted_lead) < static_cast<double>(cfg_.min_abs_score_diff)) {
            spdlog::debug("NBA: skip {} — adjusted lead {:.1f} below min {} "
                          "(raw lead {}, spread {:.1f}; {}@{})",
                          event_prefix, adjusted_lead, cfg_.min_abs_score_diff,
                          game.home_lead(), spread_pts,
                          game.away_tricode, game.home_tricode);
            continue;
        }
        if (home_wp > cfg_.max_uncertain_wp && home_wp < cfg_.min_strong_wp) {
            spdlog::debug("NBA: skip {} — home_wp {:.3f} in uncertainty band [{:.2f},{:.2f}]",
                          event_prefix, home_wp,
                          cfg_.max_uncertain_wp, cfg_.min_strong_wp);
            continue;
        }

        // Sharp-book lookup (Pinnacle / OddsAPI / etc). Fetched once per game
        // so the CLV gate inside the per-ticker loop can apply it to each
        // candidate side using the actual market mid for that side.
        // NullProvider returns nullopt → gate is a no-op.
        //
        // The CLV semantics (fixed 2026-05-20):
        //   We trade only when Kalshi MARKET PRICE has drifted at least
        //   cfg_.min_clv_edge CHEAPER than the sharp fair, on the side we
        //   are buying. This is the documented CLV-arb thesis: the trade is
        //   "Kalshi is mispriced vs Pinnacle in our favor", not "our model
        //   is more confident than the sharp" (which is fighting the sharp,
        //   the dominant adverse-selection failure mode).
        //
        //   The old gate compared model_wp vs sharp_wp and only blocked when
        //   they disagreed on direction by min_clv_edge — that was a soft
        //   "don't fight sharp" filter, not the CLV gate the research called
        //   for, and min_clv_edge was effectively unused in the common case.
        std::optional<::trader::feeds::SharpFair> sharp_fair;
        if (sharp_book_ != nullptr) {
            sharp_fair = sharp_book_->get_fair(
                game.away_tricode, game.home_tricode, game.game_date_iso);
        }

        struct Candidate {
            std::string key;          // original casing — used as order/cal ticker
            const KalshiMarket* mkt;
            bool yes_is_home;
        };
        std::vector<Candidate> candidates;
        for (const auto& [key, market] : markets_) {
            const std::string key_upper = to_upper(key);
            if (key_upper.size() < event_prefix_upper.size()) continue;
            if (key_upper.compare(0, event_prefix_upper.size(),
                                   event_prefix_upper) != 0) continue;

            bool yes_is_home;
            if (key_upper.size() == event_prefix_upper.size()) {
                // Exact event-prefix match with no contract-side suffix.
                // This is the unit-test convention (synthetic single-market
                // setup) — treat YES = home by default.
                yes_is_home = true;
            } else if (key_upper[event_prefix_upper.size()] == '-') {
                const std::string suffix =
                    key_upper.substr(event_prefix_upper.size() + 1);
                if (suffix == home_upper) {
                    yes_is_home = true;
                } else if (suffix == away_upper) {
                    yes_is_home = false;
                } else {
                    spdlog::warn(
                        "NBA: unrecognized side suffix '{}' on ticker '{}' "
                        "(home={}, away={}) — skipping",
                        suffix, key, home_upper, away_upper);
                    continue;
                }
            } else {
                continue;  // Prefix match but not at a '-' boundary.
            }
            candidates.push_back({key, &market, yes_is_home});
        }

        if (candidates.empty()) {
            spdlog::debug(
                "NBA: no Kalshi market for prefix {} ({}@{}, gid={})",
                event_prefix, game.away_tricode, game.home_tricode,
                game.game_id);
            continue;
        }

        for (const auto& c : candidates) {
            const std::string& ticker = c.key;

            // Merge REST + WS view of the book. WS keys may arrive in any
            // casing — match case-insensitively.
            double yes_bid = c.mkt->yes_bid;
            double yes_ask = c.mkt->yes_ask;
            for (const auto& [upd_key, upd] : latest_updates_) {
                if (ieq(upd_key, ticker)) {
                    yes_bid = upd.yes_bid;
                    yes_ask = upd.yes_ask;
                    break;
                }
            }

            // Book sanity. Placeholder books (0.00/1.00, 0.01/1.00) and
            // absurd spreads have no actionable liquidity.
            if (yes_bid <= 0.0 || yes_ask <= 0.0 || yes_ask >= 1.0) continue;
            const double spread = yes_ask - yes_bid;
            if (spread <= 0.0 || spread > cfg_.max_spread) continue;

            // Volume sanity (#B4, 2026-05-20). Skip markets with low total
            // contract volume — the quote is likely stale relative to live
            // game state and either won't fill or will fill against
            // informed flow that just hasn't bothered to refresh.
            if (c.mkt->volume < cfg_.min_market_volume) {
                spdlog::debug("NBA: skip {} — volume {} below min {}",
                              ticker, c.mkt->volume, cfg_.min_market_volume);
                continue;
            }

            // Fair YES price for THIS ticker (inverted if YES = away).
            const double fair_yes = c.yes_is_home ? home_wp : (1.0 - home_wp);
            const double mid = (yes_bid + yes_ask) * 0.5;

            // Pinnacle/sharp CLV gate. We require the Kalshi market mid to
            // be at least cfg_.min_clv_edge CHEAPER than the sharp fair for
            // the side we're buying. NullProvider returns nullopt → no-op.
            //
            // Direction sanity also enforced: if our model and the sharp
            // disagree on which side is favored, we refuse the trade outright
            // (we're picking against the sharp; per Bartlett-O'Hara that's
            // where adverse selection eats us alive on KXNBAGAME).
            if (sharp_fair.has_value()) {
                const double sharp_fair_yes =
                    c.yes_is_home ? sharp_fair->home_win_prob
                                  : (1.0 - sharp_fair->home_win_prob);
                const bool we_bullish_yes = fair_yes > 0.5;
                const bool sharp_bullish_yes = sharp_fair_yes > 0.5;
                if (we_bullish_yes != sharp_bullish_yes) {
                    spdlog::info("NBA CLV: skip {} — direction disagrees "
                                 "(our {:.3f} vs sharp {:.3f}, src={})",
                                 ticker, fair_yes, sharp_fair_yes,
                                 sharp_fair->source);
                    continue;
                }
                const double clv_dev = sharp_fair_yes - mid;
                if (clv_dev < cfg_.min_clv_edge) {
                    spdlog::debug("NBA CLV: skip {} — dev {:.3f} (sharp {:.3f} - "
                                  "mid {:.3f}) below min_clv_edge {:.2f}",
                                  ticker, clv_dev, sharp_fair_yes, mid,
                                  cfg_.min_clv_edge);
                    continue;
                }
                spdlog::debug("NBA CLV: pass {} — sharp {:.3f}, mid {:.3f}, "
                              "dev {:.3f}",
                              ticker, sharp_fair_yes, mid, clv_dev);
            }

            // Edge math (fixed 2026-05-20). The OLD gate compared
            // `fair - mid >= min_edge_threshold`, but the strategy actually
            // pays `yes_ask` (post-only routing may improve on this, but
            // worst-case is the taker price). With max_spread=10¢, a 4¢
            // edge-above-mid was potentially NEGATIVE after the bid/ask
            // crossing and fees. The new gate is:
            //
            //   edge_after_costs = fair_yes - target_price - fee_per_contract
            //   require edge_after_costs >= min_edge_threshold
            //
            // Where target_price is the worst-case fill (yes_ask) and the
            // fee is the Kalshi maker fee amortized across min_lot_size
            // contracts. Default min_edge_threshold drops to 0.02 to reflect
            // the changed semantic (raw edge above mid is no longer the
            // metric).
            const std::string contract_side = "yes";
            const double target_price = yes_ask;
            const double our_p = fair_yes;
            if (target_price <= 0.0 || target_price >= 1.0) continue;

            const double fee_per_contract =
                kalshi_maker_fee(cfg_.min_lot_size, target_price) /
                static_cast<double>(std::max(1, cfg_.min_lot_size));
            const double edge_after_costs =
                fair_yes - target_price - fee_per_contract;
            // Keep `edge` (above mid) for telemetry only — it is what the
            // logs and calibration record continue to report.
            const double edge = fair_yes - mid;
            if (edge_after_costs < cfg_.min_edge_threshold) {
                spdlog::debug("NBA: skip {} — edge_after_costs {:.4f} "
                              "(fair {:.3f} - ask {:.3f} - fee {:.4f}) below "
                              "min {:.4f}",
                              ticker, edge_after_costs, fair_yes, target_price,
                              fee_per_contract, cfg_.min_edge_threshold);
                continue;
            }
            const double abs_edge = std::abs(fair_yes - mid);
            if (abs_edge > cfg_.max_edge_threshold) {
                spdlog::debug("NBA: skip {} — |edge| {:.3f} > max {:.3f} "
                              "(fair {:.3f} mid {:.3f}). Probable stale/broken "
                              "candle or news we don't see.",
                              ticker, abs_edge, cfg_.max_edge_threshold,
                              fair_yes, mid);
                continue;
            }

            // Position size: fractional binomial Kelly, capped per game.
            const double kelly_f =
                binomial_kelly(our_p, target_price) * cfg_.kelly_fraction;
            if (kelly_f <= 0.0) continue;
            const double bet_dollars =
                std::min(cfg_.max_position_per_game_dollars,
                          risk_.balance() * kelly_f);
            const int qty =
                static_cast<int>(std::floor(bet_dollars / target_price));
            if (qty <= 0) continue;

            // Lot-size optimizer (#7): refuse to quote below min_lot_size.
            // Kalshi fee `ceil()` makes 1-lots structurally unprofitable —
            // see config docstring for math.
            if (qty < cfg_.min_lot_size) {
                spdlog::debug("NBA: skip {} — qty {} below min_lot_size {}",
                              ticker, qty, cfg_.min_lot_size);
                continue;
            }

            // One position per market: if we already hold contracts (either
            // side) skip to avoid stacking.
            if (risk_.position_quantity(ticker) != 0) {
                spdlog::debug("NBA: skip {} — existing position", ticker);
                continue;
            }

            auto risk_check = risk_.check_trade(ticker, qty, target_price);
            if (!risk_check.allowed) {
                spdlog::debug("NBA trade rejected {} : {}",
                              ticker, risk_check.reason);
                continue;
            }

            TradeSignal signal;
            signal.ticker = ticker;
            signal.side = Side::Buy;
            signal.contract_side = contract_side;
            signal.model_probability = our_p;
            signal.market_price = target_price;
            signal.yes_bid_snapshot = yes_bid;
            signal.yes_ask_snapshot = yes_ask;
            signal.edge = edge;
            signal.confidence = 0.90;
            signal.quantity = qty;

            std::ostringstream rationale;
            rationale << "NBA arcsine: " << game.away_tricode << "@"
                      << game.home_tricode << " Q" << game.period << " "
                      << (t / 60) << ":" << std::setfill('0') << std::setw(2)
                      << (t % 60) << " home_lead=" << game.home_lead()
                      << " home_wp=" << std::fixed << std::setprecision(3)
                      << home_wp << " ticker=" << ticker
                      << " yes_side=" << (c.yes_is_home ? "home" : "away")
                      << " fair_yes=" << fair_yes << " mkt_mid=" << mid
                      << " edge_mid=" << edge
                      << " edge_after_costs=" << edge_after_costs
                      << " → buy " << contract_side << " "
                      << qty << "x @ " << target_price;
            signal.rationale = rationale.str();
            spdlog::info("NBA signal: {}", signal.rationale);

            signals.push_back(signal);
            ++signals_generated_;

            ::trader::kalshi::CalibrationRecord cal;
            cal.market_ticker = ticker;
            cal.category = "nba";
            cal.trade_time = std::chrono::system_clock::now();
            cal.model_probability = our_p;
            cal.market_price = target_price;
            cal.edge = edge;
            cal.side = contract_side;
            cal.quantity = qty;
            cal.entry_price = target_price;
            cal.maker_fee = kalshi_maker_fee(qty, target_price);
            cal.is_exploration = false;
            cal.model_source = "arcsine";
            calibration_.log_trade(cal);
        }
    }

    return signals;
}

} // namespace trader::nba
