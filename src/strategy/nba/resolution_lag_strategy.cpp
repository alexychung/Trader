#include "strategy/nba/resolution_lag_strategy.hpp"

#include "strategy/nba/kalshi_nba_parser.hpp"

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

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

bool parse_date_iso(const std::string& iso, int& yy, int& mm, int& dd) {
    if (iso.size() < 10) return false;
    try {
        yy = std::stoi(iso.substr(0, 4)) % 100;
        mm = std::stoi(iso.substr(5, 2));
        dd = std::stoi(iso.substr(8, 2));
    } catch (...) { return false; }
    return mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31;
}

} // namespace

ResolutionLagStrategy::ResolutionLagStrategy(RiskManager& risk_manager,
                                              CalibrationLogger& calibration,
                                              Config cfg)
    : risk_(risk_manager), calibration_(calibration), cfg_(cfg) {}

void ResolutionLagStrategy::set_markets(const std::vector<KalshiMarket>& markets) {
    markets_.clear();
    for (const auto& m : markets) {
        markets_[m.ticker] = m;
    }
}

void ResolutionLagStrategy::on_market_update(const MarketUpdate& update) {
    latest_updates_[update.ticker] = update;
}

void ResolutionLagStrategy::on_settlement(const Settlement& settlement) {
    calibration_.resolve(settlement.ticker, settlement.outcome, settlement.pnl);
    spdlog::info("ResolutionLag settlement: {} → {} (PnL: ${:.2f})",
                 settlement.ticker, settlement.outcome ? "YES" : "NO",
                 settlement.pnl);
}

std::vector<TradeSignal> ResolutionLagStrategy::generate_signals() {
    std::vector<TradeSignal> signals;

    for (const auto& game : last_snapshots_) {
        // "Effectively decided" detector — (a) status final OR (b) late Q4
        // with sustained large lead.
        const bool is_final = game.is_final();
        const bool is_q4_blowout =
            game.is_live() && game.period == 4 &&
            game.game_clock_seconds <= cfg_.cutoff_clock_seconds &&
            std::abs(game.home_lead()) >= cfg_.cutoff_lead_points;

        if (!is_q4_blowout && !(cfg_.include_status_final && is_final)) {
            continue;
        }

        int yy = 0, mm = 0, dd = 0;
        if (!parse_date_iso(game.game_date_iso, yy, mm, dd)) continue;

        const std::string event_prefix = format_nba_game_ticker(
            yy, mm, dd, to_lower(game.away_tricode),
            to_lower(game.home_tricode));
        const std::string event_prefix_upper = to_upper(event_prefix);
        const std::string home_upper = to_upper(game.home_tricode);
        const std::string away_upper = to_upper(game.away_tricode);

        // Pick the winning side (per current score).
        const bool home_is_winning = game.home_lead() > 0;
        const std::string winning_tricode_upper =
            home_is_winning ? home_upper : away_upper;

        // Find the ticker for the winning side. Case-insensitive prefix
        // match — identical pattern to NbaStrategy.
        const KalshiMarket* winning_market = nullptr;
        std::string winning_ticker;
        for (const auto& [key, market] : markets_) {
            const std::string key_upper = to_upper(key);
            if (key_upper.size() < event_prefix_upper.size() + 1) continue;
            if (key_upper.compare(0, event_prefix_upper.size(),
                                   event_prefix_upper) != 0) continue;
            if (key_upper[event_prefix_upper.size()] != '-') continue;
            const std::string suffix =
                key_upper.substr(event_prefix_upper.size() + 1);
            if (suffix == winning_tricode_upper) {
                winning_market = &market;
                winning_ticker = key;
                break;
            }
        }
        if (winning_market == nullptr) continue;

        // Use WS update if available (fresher).
        double yes_bid = winning_market->yes_bid;
        double yes_ask = winning_market->yes_ask;
        auto wu = latest_updates_.find(winning_ticker);
        if (wu != latest_updates_.end()) {
            yes_bid = wu->second.yes_bid;
            yes_ask = wu->second.yes_ask;
        }

        // Volume sanity — skip stale low-volume books that won't fill or
        // that signal a market no one is watching.
        if (winning_market->volume < cfg_.min_market_volume) {
            spdlog::debug("ResolutionLag: skip {} — volume {} below min {}",
                          winning_ticker, winning_market->volume,
                          cfg_.min_market_volume);
            continue;
        }

        // Price window — only buy in [min_entry, max_entry].
        if (yes_ask < cfg_.min_entry_price || yes_ask > cfg_.max_entry_price) {
            spdlog::debug(
                "ResolutionLag: skip {} — ask {:.4f} outside window [{:.2f}, {:.2f}]",
                winning_ticker, yes_ask, cfg_.min_entry_price,
                cfg_.max_entry_price);
            continue;
        }

        // Sizing — flat dollar cap per game (no Kelly, this is near-certain).
        int qty = static_cast<int>(
            std::floor(cfg_.max_position_per_game_dollars / yes_ask));
        if (qty < cfg_.min_lot_size) continue;

        // Existing-position dedup.
        if (risk_.position_quantity(winning_ticker) != 0) continue;

        auto risk_check = risk_.check_trade(winning_ticker, qty, yes_ask);
        if (!risk_check.allowed) {
            spdlog::debug("ResolutionLag rejected {} : {}",
                          winning_ticker, risk_check.reason);
            continue;
        }

        TradeSignal signal;
        signal.ticker = winning_ticker;
        signal.side = Side::Buy;
        signal.contract_side = "yes";
        signal.model_probability = 0.99;  // we treat this as near-certain
        signal.market_price = yes_ask;
        signal.yes_bid_snapshot = yes_bid;
        signal.yes_ask_snapshot = yes_ask;
        signal.edge = 0.99 - yes_ask;
        signal.confidence = 0.99;
        signal.quantity = qty;

        std::ostringstream rationale;
        rationale << "ResolutionLag: " << game.away_tricode << "@"
                  << game.home_tricode << " "
                  << (is_final ? "[FINAL]" : "Q4-blowout")
                  << " score " << game.away_score << "-" << game.home_score
                  << " → buy YES " << qty << "x @ " << yes_ask
                  << " (settle to $1.00 = +" << std::fixed << std::setprecision(3)
                  << (1.0 - yes_ask) << "/contract gross)";
        signal.rationale = rationale.str();
        spdlog::info("ResolutionLag signal: {}", signal.rationale);

        signals.push_back(signal);
        ++signals_generated_;

        CalibrationRecord cal;
        cal.market_ticker = winning_ticker;
        cal.category = "nba-resolution-lag";
        cal.trade_time = std::chrono::system_clock::now();
        cal.model_probability = 0.99;
        cal.market_price = yes_ask;
        cal.edge = signal.edge;
        cal.side = "yes";
        cal.quantity = qty;
        cal.entry_price = yes_ask;
        cal.maker_fee = kalshi_maker_fee(qty, yes_ask);
        cal.is_exploration = false;
        cal.model_source = "resolution-lag";
        calibration_.log_trade(cal);
    }

    return signals;
}

} // namespace trader::nba
