#include "backtest/driver.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdio>

namespace trader::backtest {

namespace {

struct Event {
    Timestamp time;
    enum Kind { Decision, Settle } kind;
    std::size_t market_idx = 0;
    double yes_price = 0.0;  // Decision only
};

Timestamp iso_or_epoch(const std::string& s) {
    if (s.empty()) return {};
    return parse_iso8601(s);
}

// Decode a weather-ticker's embedded date like "26MAR30" -> "2026-03-30".
// Falls back to a date derived from close_time when the ticker lacks a day
// (legacy "26APR" format). Returns empty on failure.
std::string ticker_encoded_to_iso(const std::string& encoded,
                                    const std::string& fallback_close_time) {
    static const std::unordered_map<std::string, int> month_idx = {
        {"JAN",1},{"FEB",2},{"MAR",3},{"APR",4},{"MAY",5},{"JUN",6},
        {"JUL",7},{"AUG",8},{"SEP",9},{"OCT",10},{"NOV",11},{"DEC",12},
    };
    if (encoded.size() < 5) return {};
    int yy = 0;
    try { yy = std::stoi(encoded.substr(0, 2)); } catch (...) { return {}; }
    auto it = month_idx.find(encoded.substr(2, 3));
    if (it == month_idx.end()) return {};
    int year = 2000 + yy;
    int month = it->second;
    int day = 0;
    if (encoded.size() >= 7) {
        try { day = std::stoi(encoded.substr(5, 2)); } catch (...) { day = 0; }
    }
    if (day == 0) {
        // Legacy "26APR" without day — derive from close_time (subtract 1 day
        // because Kalshi close_time is 00:00 local next day).
        if (fallback_close_time.empty()) return {};
        auto ts = parse_iso8601(fallback_close_time);
        if (ts.time_since_epoch().count() == 0) return {};
        ts -= std::chrono::hours(12);  // roll back into local trading day
        return format_date_utc(ts);
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    return buf;
}

trader::kalshi::WeatherStation* find_station(const std::string& station_id,
                                               std::vector<trader::kalshi::WeatherStation>& stations) {
    for (auto& s : stations) if (s.id == station_id) return &s;
    return nullptr;
}

// Find nearest prior trade price (yes) for a given timestamp. Returns -1 if none.
double price_at(const std::vector<SettledTrade>& trades, Timestamp t) {
    double last = -1.0;
    for (const auto& tr : trades) {
        auto tt = iso_or_epoch(tr.created_at);
        if (tt.time_since_epoch().count() == 0) continue;
        if (tt > t) break;
        last = tr.yes_price;
    }
    return last;
}

} // namespace

Timestamp parse_iso8601(const std::string& s) {
    if (s.size() < 19) return {};
    std::tm tm{};
    try {
        tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        tm.tm_mon  = std::stoi(s.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(s.substr(8, 2));
        tm.tm_hour = std::stoi(s.substr(11, 2));
        tm.tm_min  = std::stoi(s.substr(14, 2));
        tm.tm_sec  = std::stoi(s.substr(17, 2));
    } catch (...) {
        return {};
    }
#ifdef _WIN32
    auto t = _mkgmtime(&tm);
#else
    auto t = timegm(&tm);
#endif
    if (t == -1) return {};
    return std::chrono::system_clock::from_time_t(t);
}

std::string format_date_utc(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

std::string format_iso8601(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

BacktestDriver::BacktestDriver(trader::kalshi::KalshiEventStrategy& strategy,
                                trader::kalshi::ProbabilityEngine& prob_engine,
                                std::shared_ptr<trader::kalshi::WeatherEnsembleModel> weather_model,
                                trader::StalenessGate& staleness,
                                trader::MockExchange& exchange,
                                Config config)
    : strategy_(strategy), prob_engine_(prob_engine),
      weather_model_(std::move(weather_model)), staleness_(staleness),
      exchange_(exchange), config_(std::move(config)) {}

BacktestDriver::RunResult BacktestDriver::run(std::vector<SettledMarket> markets) {
    RunResult result;
    std::vector<Event> events;
    auto stations = trader::kalshi::kalshi_weather_stations();

    // Defensive sort: price_at() assumes trades are chronological and breaks
    // silently on out-of-order input (returns stale prices without warning).
    // The fetcher documents "ordered oldest-first" but does not enforce it
    // across paginated concatenation, so we sort here before any scanning.
    // Unparseable timestamps sort to the front and are skipped by price_at()
    // on the time comparison.
    for (auto& sm : markets) {
        std::stable_sort(sm.trades.begin(), sm.trades.end(),
            [](const SettledTrade& a, const SettledTrade& b) {
                return iso_or_epoch(a.created_at) < iso_or_epoch(b.created_at);
            });
    }

    // Build decision timeline per market.
    for (std::size_t i = 0; i < markets.size(); ++i) {
        const auto& sm = markets[i];
        CoverageEntry entry;
        entry.ticker = sm.market.ticker;
        entry.category = sm.market.category;

        auto info = trader::kalshi::WeatherEnsembleModel::parse_weather_ticker(sm.market.ticker);
        if (!info.valid) {
            entry.skip_reason = "not_weather_ticker";
            result.coverage.push_back(std::move(entry));
            continue;
        }
        if (!find_station(info.station_id, stations)) {
            entry.skip_reason = "unknown_station:" + info.station_id;
            result.coverage.push_back(std::move(entry));
            continue;
        }

        auto resolution_ts = iso_or_epoch(sm.resolution_time);
        if (resolution_ts.time_since_epoch().count() == 0) {
            entry.skip_reason = "no_resolution_time";
            result.coverage.push_back(std::move(entry));
            continue;
        }

        // Decision points: from trade history, keep trades whose price moved
        // ≥ min_price_move since the last kept trade. Cap at max_decisions.
        std::vector<Event> decisions;
        double last_kept = -1.0;
        auto cutoff = resolution_ts - std::chrono::hours(config_.skip_last_hours);
        for (const auto& tr : sm.trades) {
            auto tt = iso_or_epoch(tr.created_at);
            if (tt.time_since_epoch().count() == 0) continue;
            if (tt >= cutoff) break;
            if (last_kept < 0.0 || std::abs(tr.yes_price - last_kept) >= config_.min_price_move) {
                Event e;
                e.time = tt;
                e.kind = Event::Decision;
                e.market_idx = i;
                e.yes_price = tr.yes_price;
                decisions.push_back(e);
                last_kept = tr.yes_price;
                if (static_cast<int>(decisions.size()) >= config_.max_decisions_per_market) break;
            }
        }

        // Fallback: no trades or no meaningful moves. Sample evenly between
        // close_time-skew and resolution-skew. Use market mid as yes_price if
        // available, else 0.50.
        if (decisions.empty()) {
            double fallback_price = (sm.market.yes_bid + sm.market.yes_ask) * 0.5;
            if (fallback_price <= 0.0) fallback_price = sm.market.last_price;
            if (fallback_price <= 0.0) fallback_price = 0.50;
            auto open_ts = resolution_ts - std::chrono::hours(72);  // 3-day prior window
            int n = std::max(1, config_.fallback_decisions_per_market);
            auto span = cutoff - open_ts;
            for (int k = 0; k < n; ++k) {
                Event e;
                e.time = open_ts + std::chrono::duration_cast<std::chrono::seconds>(span) * k / n;
                e.kind = Event::Decision;
                e.market_idx = i;
                e.yes_price = fallback_price;
                decisions.push_back(e);
            }
        }

        // Settlement event.
        Event settle;
        settle.time = resolution_ts;
        settle.kind = Event::Settle;
        settle.market_idx = i;

        entry.attempted = true;
        entry.decisions = static_cast<int>(decisions.size());
        result.coverage.push_back(std::move(entry));

        for (auto& d : decisions) events.push_back(d);
        events.push_back(settle);
    }

    // Chronological order.
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) { return a.time < b.time; });

    for (const auto& ev : events) {
        const auto& sm = markets[ev.market_idx];
        exchange_.set_clock(ev.time);

        if (ev.kind == Event::Settle) {
            Settlement s;
            s.ticker = sm.market.ticker;
            s.outcome = sm.outcome_yes;
            s.pnl = 0.0;  // per-record pnl is recomputed in the report
            s.resolution_time = ev.time;
            strategy_.on_settlement(s);
            ++result.settlements_applied;
            continue;
        }

        // Decision point.
        auto info = trader::kalshi::WeatherEnsembleModel::parse_weather_ticker(sm.market.ticker);
        if (!info.valid) continue;
        auto* station = find_station(info.station_id, stations);
        if (!station) continue;

        // Target date: prefer the day encoded in the ticker (e.g., "26MAR30"
        // -> "2026-03-30"), which is the LOCAL trading day the weather matters
        // for. Fall back to close_time-derived date for legacy tickers.
        std::string target_date = ticker_encoded_to_iso(info.date, sm.market.close_time);
        auto target_ts = parse_iso8601(target_date + "T12:00:00Z");
        auto close_ts = iso_or_epoch(
            !sm.market.close_time.empty() ? sm.market.close_time : sm.resolution_time);
        if (target_date.empty() || close_ts.time_since_epoch().count() == 0) continue;

        auto delta = target_ts - ev.time;
        auto hours = std::chrono::duration_cast<std::chrono::hours>(delta).count();
        int days_ahead = std::max(1, static_cast<int>(hours / 24));

        auto ef = trader::kalshi::OpenMeteoClient::fetch_archived_forecast(
            *station, target_date, days_ahead, config_.weather_cache_dir);
        if (ef.member_highs.empty()) {
            spdlog::debug("archive miss {} {}", sm.market.ticker, target_date);
            continue;  // skip this decision point silently — coverage says "attempted"
        }
        weather_model_->set_ensemble(ef);

        // Construct a KalshiMarket for the strategy from the snapshot + yes_price.
        trader::kalshi::KalshiMarket m = sm.market;
        // Derive bid/ask bracket around the trade price to pass market filter.
        double p = ev.yes_price;
        if (p <= 0.0) p = 0.50;
        // The settled snapshot reports status="settled" (or "finalized"); at
        // decision time the market WAS open. Rewrite so MarketFilter replays
        // as-of the decision, not the terminal state.
        m.status = "open";
        m.yes_bid = std::max(0.01, p - config_.half_spread);
        m.yes_ask = std::min(0.99, p + config_.half_spread);
        m.last_price = p;
        // Preserve historical cumulative volume. Previously we defaulted to 200
        // "to keep the filter happy" — that silently converted zero-volume
        // dead markets into tradeable ones in backtest, overstating
        // pass-through rate vs live trading. Trust the server value; if the
        // response truly lacked a volume field (Kalshi has used several
        // field names across the 2026 migration), fall back to a liquidity
        // floor that matches the filter so the backtest doesn't get
        // artificially empty runs from a parse gap alone.
        if (m.volume <= 0) m.volume = config_.fallback_volume_when_missing;
        m.category = m.category.empty() ? "weather" : m.category;

        strategy_.set_markets({m});

        // Feed a MarketUpdate so the staleness gate is fresh.
        MarketUpdate upd;
        upd.ticker = m.ticker;
        upd.yes_bid = m.yes_bid;
        upd.yes_ask = m.yes_ask;
        upd.last_price = m.last_price;
        upd.volume = m.volume;
        upd.timestamp = ev.time;
        strategy_.on_market_update(upd);
        // Strategy calls staleness_.on_market_update inside — but that uses
        // system clock, not ev.time. Seed explicitly so our backtest clock wins.
        staleness_.seed_market(m.ticker, ev.time);
        staleness_.seed_feed("open_meteo", ev.time);

        auto signals = strategy_.generate_signals();
        ++result.decisions_made;

        for (const auto& sig : signals) {
            Order order;
            order.ticker = sig.ticker;
            order.side = sig.side;
            order.contract_side = sig.contract_side;
            order.price = sig.market_price;
            order.quantity = sig.quantity;
            order.post_only = true;
            exchange_.place_order(order);
            ++result.orders_placed;
        }
    }

    return result;
}

} // namespace trader::backtest
