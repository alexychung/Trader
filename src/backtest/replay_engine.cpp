#include "backtest/replay_engine.hpp"

#include "risk/risk_manager.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/nba/kalshi_nba_parser.hpp"
#include "strategy/nba/win_probability.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>

namespace trader::backtest {

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

bool ends_with_iupper(const std::string& s, const std::string& suffix_upper) {
    if (s.size() < suffix_upper.size()) return false;
    std::string tail = to_upper(s.substr(s.size() - suffix_upper.size()));
    return tail == suffix_upper;
}

double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

// Stub feed — returns a single pre-set snapshot. The replay engine sets
// the snapshot before each call to strategy.generate_signals().
class StubScoreFeed : public ::trader::nba::NbaScoreFeed {
public:
    ::trader::nba::NbaGameSnapshot snapshot;
    bool valid = false;
    std::vector<::trader::nba::NbaGameSnapshot> fetch_today_scoreboard() override {
        if (!valid) return {};
        return {snapshot};
    }
};

} // namespace

SyntheticKalshiPriceProvider::SyntheticKalshiPriceProvider(
    const std::string& home_tricode, const std::string& away_tricode,
    double half_spread, double noise_stdev)
    : home_upper_(to_upper(home_tricode)),
      away_upper_(to_upper(away_tricode)),
      half_spread_(half_spread),
      noise_stdev_(noise_stdev) {}

std::optional<IKalshiPriceProvider::Quote>
SyntheticKalshiPriceProvider::get_quote(const std::string& ticker,
                                         int64_t /*wall_clock_ts_sec*/) {
    // Determine which side this ticker is for (YES=home or YES=away).
    bool yes_is_home = false;
    if (ends_with_iupper(ticker, "-" + home_upper_)) {
        yes_is_home = true;
    } else if (ends_with_iupper(ticker, "-" + away_upper_)) {
        yes_is_home = false;
    } else {
        // No side suffix — assume YES=home (single-market test convention).
        yes_is_home = true;
    }
    const double fair_yes =
        yes_is_home ? current_home_wp_ : (1.0 - current_home_wp_);

    // Deterministic noise seeded by (ticker, fair) so replays are
    // reproducible. The std::random_device version of "stateless noise"
    // would make backtests irreproducible.
    static thread_local std::mt19937 rng{0xC0FFEEu};
    std::normal_distribution<double> noise(0.0, noise_stdev_);
    double mid = clamp01(fair_yes + noise(rng));
    Quote q;
    q.yes_bid = clamp01(mid - half_spread_);
    q.yes_ask = clamp01(mid + half_spread_);
    // Synthetic volume — large enough to pass any reasonable
    // min_market_volume gate.
    q.volume = 1000;
    return q;
}

namespace {

// Mirror the strategy's spread-adjustment formula so the price provider
// can be told the right wp.
double compute_home_wp(int home_lead, int regulation_seconds_remaining,
                       double pregame_spread) {
    constexpr double kRegSeconds = 2880.0;
    const double t = static_cast<double>(regulation_seconds_remaining);
    const double time_elapsed = kRegSeconds - t;
    const double adjusted_lead =
        static_cast<double>(home_lead) -
        pregame_spread * (time_elapsed / kRegSeconds);
    return ::trader::nba::WinProbability::survival_probability(adjusted_lead, t);
}

::trader::nba::NbaGameSnapshot snapshot_from_pbp(const PbpGameSummary& g,
                                                   const PbpEvent& e,
                                                   double pregame_spread) {
    ::trader::nba::NbaGameSnapshot s;
    s.valid = true;
    s.game_id = g.game_id;
    s.game_status = 2;           // live during PBP
    s.game_date_iso = g.game_date_iso;
    s.period = e.period;
    s.game_clock_seconds = e.clock_seconds;
    s.regulation_seconds_remaining = e.regulation_seconds_remaining;
    s.home_tricode = g.home_tricode;
    s.away_tricode = g.away_tricode;
    s.home_score = e.score_home;
    s.away_score = e.score_away;
    s.pregame_spread = pregame_spread;
    return s;
}

std::vector<::trader::kalshi::KalshiMarket> build_markets(
    const PbpGameSummary& g, IKalshiPriceProvider& provider,
    int64_t wall_clock_ts_sec) {
    std::vector<::trader::kalshi::KalshiMarket> out;
    // Build both per-side tickers, matching Kalshi's KXNBAGAME-*-HOME /
    // KXNBAGAME-*-AWAY format.
    int yy = 0, mm = 0, dd = 0;
    if (g.game_date_iso.size() >= 10) {
        try {
            yy = std::stoi(g.game_date_iso.substr(0, 4)) % 100;
            mm = std::stoi(g.game_date_iso.substr(5, 2));
            dd = std::stoi(g.game_date_iso.substr(8, 2));
        } catch (...) { return out; }
    }
    if (mm < 1 || mm > 12) return out;
    const std::string event_prefix = ::trader::nba::format_nba_game_ticker(
        yy, mm, dd, to_lower(g.away_tricode), to_lower(g.home_tricode));

    auto build_side = [&](const std::string& side_tricode) {
        ::trader::kalshi::KalshiMarket m;
        m.ticker = event_prefix + "-" + to_upper(side_tricode);
        m.title = "NBA " + g.away_tricode + " @ " + g.home_tricode;
        m.category = "Sports";
        m.status = "open";
        auto q = provider.get_quote(m.ticker, wall_clock_ts_sec);
        if (q.has_value()) {
            m.yes_bid = q->yes_bid;
            m.yes_ask = q->yes_ask;
            m.last_price = (q->yes_bid + q->yes_ask) * 0.5;
            m.volume = q->volume;
            m.open_interest = q->volume;
        }
        return m;
    };
    out.push_back(build_side(g.home_tricode));
    out.push_back(build_side(g.away_tricode));
    return out;
}

// Approximate wall-clock for a PBP event. NBA games are ~2.5h for ~48
// minutes of game time; we use a flat 3x multiplier on game-clock-elapsed
// for the regulation portion. Period breaks and timeouts are folded in
// implicitly. Tipoff anchored at 19:00 local (~midnight UTC). Used only
// for IKalshiPriceProvider lookup; the synthetic provider ignores it.
int64_t pbp_wall_clock_ts_sec(const PbpGameSummary& g, const PbpEvent& e) {
    int yy = 0, mm = 0, dd = 0;
    if (g.game_date_iso.size() >= 10) {
        try {
            yy = std::stoi(g.game_date_iso.substr(0, 4));
            mm = std::stoi(g.game_date_iso.substr(5, 2));
            dd = std::stoi(g.game_date_iso.substr(8, 2));
        } catch (...) { return 0; }
    }
    std::tm tm{};
    tm.tm_year = yy - 1900;
    tm.tm_mon = mm - 1;
    tm.tm_mday = dd;
    tm.tm_hour = 0;    // assume midnight UTC tipoff (close enough for cand lookup)
    int64_t tipoff_utc = static_cast<int64_t>(
#ifdef _WIN32
        _mkgmtime(&tm)
#else
        timegm(&tm)
#endif
        );
    if (e.regulation_seconds_remaining < 0) return tipoff_utc;
    // Game elapsed in seconds.
    int game_elapsed = std::max(0, 2880 - e.regulation_seconds_remaining);
    return tipoff_utc + 3 * game_elapsed;  // 3x stretch for breaks
}

} // namespace

GameReplayResult BacktestReplay::replay_game(
    const PbpGameSummary& summary,
    const std::vector<PbpEvent>& events,
    IKalshiPriceProvider& price_provider) {

    GameReplayResult result;
    result.game_id = summary.game_id;
    result.date_iso = summary.game_date_iso;
    result.home_tricode = summary.home_tricode;
    result.away_tricode = summary.away_tricode;
    result.home_final_score = summary.home_final_score;
    result.away_final_score = summary.away_final_score;
    result.home_won = summary.home_won();

    if (events.empty()) {
        spdlog::warn("replay: game {} has no PBP events, skipping",
                     summary.game_id);
        return result;
    }

    // Fresh risk manager + calibration per game. The aggregate metrics are
    // computed across games separately — we don't want one game's positions
    // to block another's.
    RiskConfig rc;
    rc.max_position_per_market = 1000;
    rc.max_total_exposure = cfg_.simulated_bankroll * 2.0;
    rc.max_daily_loss = cfg_.simulated_bankroll * 2.0;
    rc.kill_switch_loss = cfg_.simulated_bankroll * 5.0;
    rc.cash_reserve_pct = 0.0;
    rc.maker_only = true;
    RiskManager risk_mgr(rc);
    risk_mgr.set_balance(cfg_.simulated_bankroll);

    ::trader::kalshi::CalibrationLogger cal("");  // in-memory only

    // Disable jitter / make poll instantaneous so refresh fires every tick.
    auto nba_cfg = cfg_.nba;
    nba_cfg.quote_jitter_pct = 0.0;
    nba_cfg.scoreboard_poll_interval = std::chrono::seconds(0);
    StubScoreFeed feed;
    ::trader::nba::NbaStrategy nba_strat(risk_mgr, cal, feed, nba_cfg);

    ::trader::nba::ResolutionLagStrategy reslag(risk_mgr, cal, cfg_.reslag);

    // Track each fill we've simulated so we can settle at the end.
    auto record_fill = [&](const TradeSignal& s, const std::string& strat_name) {
        // Figure out yes_is_home from ticker suffix.
        bool yes_is_home = true;
        const std::string upper = to_upper(s.ticker);
        const std::string home_suffix = "-" + to_upper(summary.home_tricode);
        const std::string away_suffix = "-" + to_upper(summary.away_tricode);
        if (upper.size() >= home_suffix.size() &&
            upper.compare(upper.size() - home_suffix.size(),
                           home_suffix.size(), home_suffix) == 0) {
            yes_is_home = true;
        } else if (upper.size() >= away_suffix.size() &&
                   upper.compare(upper.size() - away_suffix.size(),
                                  away_suffix.size(), away_suffix) == 0) {
            yes_is_home = false;
        }
        SimFill f;
        f.ticker = s.ticker;
        f.contract_side = s.contract_side;
        f.yes_is_home = yes_is_home;
        f.quantity = s.quantity;
        f.entry_price = s.market_price;
        f.model_probability = s.model_probability;
        f.maker_fee = kalshi_maker_fee(s.quantity, s.market_price);
        f.strategy = strat_name;
        result.fills.push_back(f);
        result.total_fees += f.maker_fee;
        // Also tell the risk manager — so future ticks see the position
        // and skip duplicate entries on the same ticker.
        risk_mgr.on_fill(s.ticker, s.contract_side, s.quantity, s.market_price);
    };

    for (const auto& e : events) {
        // Strategy ignores periods outside regulation; we still walk PBP
        // events for state tracking even when no signal can fire.
        auto snap = snapshot_from_pbp(summary, e, cfg_.pregame_spread);

        // Tell the price provider what wp to price around. The synthetic
        // provider needs this; real-candle providers can ignore it.
        double home_wp = compute_home_wp(snap.home_lead(),
                                          snap.regulation_seconds_remaining,
                                          cfg_.pregame_spread);
        if (auto* synth = dynamic_cast<SyntheticKalshiPriceProvider*>(
                &price_provider)) {
            synth->set_home_wp(home_wp);
        }

        // Build the per-side Kalshi markets for this tick.
        int64_t wall_ts = pbp_wall_clock_ts_sec(summary, e);
        auto markets = build_markets(summary, price_provider, wall_ts);

        feed.snapshot = snap;
        feed.valid = true;
        nba_strat.set_markets(markets);
        if (cfg_.run_resolution_lag) {
            reslag.set_markets(markets);
        }

        auto sigs = nba_strat.generate_signals();
        result.signals_arcsine += static_cast<int>(sigs.size());
        for (const auto& s : sigs) record_fill(s, "arcsine");

        if (cfg_.run_resolution_lag) {
            reslag.set_snapshots(nba_strat.last_snapshots());
            auto rs = reslag.generate_signals();
            result.signals_resolution_lag += static_cast<int>(rs.size());
            for (const auto& s : rs) record_fill(s, "resolution-lag");
        }

        // Update game state (last fill's home_lead) — used for the
        // settlement decision via the final-event check below.
        // Note: PbpEvent already carries score state, so the last event
        // ends the loop with a fresh score.
    }

    // Settlement: each YES contract pays $1 if held side wins, else $0.
    // For each fill, compute outcome based on which side is YES + who won.
    const bool home_won = result.home_won;
    double cumulative_pnl = 0.0;
    for (const auto& f : result.fills) {
        const bool contract_pays = f.yes_is_home ? home_won : !home_won;
        const double settle_price = contract_pays ? 1.0 : 0.0;
        const double pnl =
            (settle_price - f.entry_price) * static_cast<double>(f.quantity) -
            f.maker_fee;
        cumulative_pnl += pnl;
        result.brier_samples.emplace_back(
            f.model_probability, contract_pays ? 1 : 0);
    }
    result.settled_pnl = cumulative_pnl;

    return result;
}

BacktestSummary aggregate(const std::vector<GameReplayResult>& per_game) {
    BacktestSummary s;
    s.per_game = per_game;
    s.games_replayed = static_cast<int>(per_game.size());

    double running = 0.0;
    double peak = 0.0;
    double brier_sum_sq = 0.0;
    int brier_n = 0;

    for (const auto& g : per_game) {
        s.total_signals += g.signals_arcsine + g.signals_resolution_lag;
        s.total_fills += static_cast<int>(g.fills.size());
        s.total_pnl += g.settled_pnl;
        s.total_fees += g.total_fees;
        for (const auto& [pred, outcome] : g.brier_samples) {
            const double diff = pred - static_cast<double>(outcome);
            brier_sum_sq += diff * diff;
            ++brier_n;
            if (outcome == 1) ++s.wins; else ++s.losses;
        }
        running += g.settled_pnl;
        if (running > peak) peak = running;
        double dd = peak - running;
        if (dd > s.max_drawdown) s.max_drawdown = dd;
    }
    s.brier_samples = brier_n;
    if (brier_n > 0) {
        s.brier_score = brier_sum_sq / static_cast<double>(brier_n);
    }
    return s;
}

} // namespace trader::backtest
