#include "strategy/nba/nba_strategy.hpp"

#include "strategy/nba/kalshi_nba_parser.hpp"
#include "strategy/nba/win_probability.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace trader::nba {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
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
    if (last_scoreboard_fetch_.time_since_epoch().count() != 0 &&
        now - last_scoreboard_fetch_ < cfg_.scoreboard_poll_interval) {
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

    for (const auto& game : last_snapshots_) {
        if (!game.is_live()) continue;

        // Time-window gating.
        const int t = game.regulation_seconds_remaining;
        if (t < cfg_.min_seconds_remaining || t > cfg_.max_seconds_remaining) {
            continue;
        }

        // Date → 2-digit year/month/day so we can format the Kalshi ticker.
        int yy = 0, mm = 0, dd = 0;
        if (!parse_date_iso(game.game_date_iso, yy, mm, dd)) {
            spdlog::warn("NBA: bad game_date_iso for {} vs {}: '{}'",
                         game.away_tricode, game.home_tricode, game.game_date_iso);
            continue;
        }
        std::string ticker = format_nba_game_ticker(
            yy, mm, dd, to_lower(game.away_tricode), to_lower(game.home_tricode));

        auto market_it = markets_.find(ticker);
        if (market_it == markets_.end()) {
            // Kalshi doesn't list every game; common for early-round preseason.
            spdlog::debug("NBA: no Kalshi market for {} ({}@{}, gid={})",
                          ticker, game.away_tricode, game.home_tricode, game.game_id);
            continue;
        }

        // Merge REST + WS view of the book.
        double yes_bid = market_it->second.yes_bid;
        double yes_ask = market_it->second.yes_ask;
        auto wu = latest_updates_.find(ticker);
        if (wu != latest_updates_.end()) {
            yes_bid = wu->second.yes_bid;
            yes_ask = wu->second.yes_ask;
        }

        // Book sanity. Placeholder books (0.00/1.00, 0.01/1.00) and absurd
        // spreads have no actionable liquidity.
        if (yes_bid <= 0.0 || yes_ask <= 0.0 || yes_ask >= 1.0) continue;
        double spread = yes_ask - yes_bid;
        if (spread <= 0.0 || spread > cfg_.max_spread) continue;

        // Our fair value: probability the HOME team wins from current state.
        // Assumes Kalshi YES = home (see header note).
        double home_wp = WinProbability::survival_probability(
            static_cast<double>(game.home_lead()), static_cast<double>(t));
        double mid = (yes_bid + yes_ask) * 0.5;
        double edge = home_wp - mid;

        if (std::abs(edge) < cfg_.min_edge_threshold) continue;

        // Pick side + maker-safe price.
        std::string contract_side;
        double target_price;
        double our_p;
        if (edge > 0) {
            // We think home wins more often than market — buy YES at ask.
            contract_side = "yes";
            target_price = yes_ask;
            our_p = home_wp;
        } else {
            // We think away wins more often — buy NO at the NO ask
            //   NO ask = 1 - YES bid
            contract_side = "no";
            target_price = 1.0 - yes_bid;
            our_p = 1.0 - home_wp;
        }
        if (target_price <= 0.0 || target_price >= 1.0) continue;

        // Position size: fractional binomial Kelly, capped by max_position_per_game.
        double kelly_f = binomial_kelly(our_p, target_price) * cfg_.kelly_fraction;
        if (kelly_f <= 0.0) continue;
        double bet_dollars = std::min(cfg_.max_position_per_game_dollars,
                                       risk_.balance() * kelly_f);
        int qty = static_cast<int>(std::floor(bet_dollars / target_price));
        if (qty <= 0) continue;

        // One position per market: if we already hold contracts (either side)
        // skip to avoid stacking / dual-side accounting.
        if (risk_.position_quantity(ticker) != 0) {
            spdlog::debug("NBA: skip {} — existing position", ticker);
            continue;
        }

        auto risk_check = risk_.check_trade(ticker, qty, target_price);
        if (!risk_check.allowed) {
            spdlog::debug("NBA trade rejected {} : {}", ticker, risk_check.reason);
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
        signal.edge = std::abs(edge);
        signal.confidence = 0.90;  // arcsine is deterministic; 0.9 reflects model misspec risk
        signal.quantity = qty;

        std::ostringstream rationale;
        rationale << "NBA arcsine: " << game.away_tricode << "@" << game.home_tricode
                  << " Q" << game.period << " " << (t / 60) << ":"
                  << std::setfill('0') << std::setw(2) << (t % 60)
                  << " home_lead=" << game.home_lead()
                  << " home_wp=" << std::fixed << std::setprecision(3) << home_wp
                  << " mkt_mid=" << mid << " edge=" << edge << " → buy "
                  << contract_side << " " << qty << "x @ " << target_price;
        signal.rationale = rationale.str();
        spdlog::info("NBA signal: {}", signal.rationale);

        signals.push_back(signal);
        ++signals_generated_;

        // Calibration log entry.
        ::trader::kalshi::CalibrationRecord cal;
        cal.market_ticker = ticker;
        cal.category = "nba";
        cal.trade_time = std::chrono::system_clock::now();
        cal.model_probability = our_p;
        cal.market_price = target_price;
        cal.edge = std::abs(edge);
        cal.side = contract_side;
        cal.quantity = qty;
        cal.entry_price = target_price;
        cal.maker_fee = kalshi_maker_fee(qty, target_price);
        cal.is_exploration = false;
        cal.model_source = "arcsine";
        calibration_.log_trade(cal);
    }

    return signals;
}

} // namespace trader::nba
