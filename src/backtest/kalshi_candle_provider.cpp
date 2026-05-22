#include "backtest/kalshi_candle_provider.hpp"

#include "strategy/nba/kalshi_nba_parser.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

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

// Midnight UTC epoch of YYYY-MM-DD.
int64_t midnight_utc_of(const std::string& iso_date) {
    if (iso_date.size() < 10) return 0;
    int yy = 0, mm = 0, dd = 0;
    try {
        yy = std::stoi(iso_date.substr(0, 4));
        mm = std::stoi(iso_date.substr(5, 2));
        dd = std::stoi(iso_date.substr(8, 2));
    } catch (...) { return 0; }
    std::tm tm{};
    tm.tm_year = yy - 1900;
    tm.tm_mon = mm - 1;
    tm.tm_mday = dd;
#ifdef _WIN32
    return static_cast<int64_t>(_mkgmtime(&tm));
#else
    return static_cast<int64_t>(timegm(&tm));
#endif
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file_atomic(const std::string& path, const std::string& content) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
}

nlohmann::json candle_to_json(
    const ::trader::kalshi::KalshiRestClient::KalshiCandle& c) {
    return {
        {"end_period_ts", c.end_period_ts},
        {"open_interest", c.open_interest},
        {"volume", c.volume},
        {"price_open", c.price_open},
        {"price_high", c.price_high},
        {"price_low", c.price_low},
        {"price_close", c.price_close},
        {"yes_bid_close", c.yes_bid_close},
        {"yes_ask_close", c.yes_ask_close},
    };
}

::trader::kalshi::KalshiRestClient::KalshiCandle candle_from_json(
    const nlohmann::json& j) {
    ::trader::kalshi::KalshiRestClient::KalshiCandle c;
    c.end_period_ts = j.value("end_period_ts", int64_t{0});
    c.open_interest = j.value("open_interest", 0);
    c.volume = j.value("volume", 0);
    c.price_open = j.value("price_open", 0.0);
    c.price_high = j.value("price_high", 0.0);
    c.price_low = j.value("price_low", 0.0);
    c.price_close = j.value("price_close", 0.0);
    c.yes_bid_close = j.value("yes_bid_close", 0.0);
    c.yes_ask_close = j.value("yes_ask_close", 0.0);
    return c;
}

} // namespace

KalshiCandlePriceProvider::KalshiCandlePriceProvider(
    ::trader::kalshi::KalshiRestClient& rest, std::string cache_dir)
    : rest_(rest), cache_dir_(std::move(cache_dir)) {
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
    if (ec) {
        spdlog::warn("KalshiCandlePriceProvider: create cache dir {} failed: {}",
                     cache_dir_, ec.message());
    }
}

std::string KalshiCandlePriceProvider::cache_path_for(
    const std::string& ticker) const {
    return cache_dir_ + "/" + ticker + ".json";
}

std::vector<KalshiCandlePriceProvider::KalshiCandle>
KalshiCandlePriceProvider::load_from_cache(const std::string& ticker) {
    const std::string body = read_file(cache_path_for(ticker));
    if (body.empty()) return {};
    try {
        auto j = nlohmann::json::parse(body);
        if (!j.is_array()) return {};
        std::vector<KalshiCandle> out;
        out.reserve(j.size());
        for (const auto& el : j) out.push_back(candle_from_json(el));
        return out;
    } catch (const std::exception& e) {
        spdlog::warn("KalshiCandlePriceProvider: cache parse failed for {}: {}",
                     ticker, e.what());
        return {};
    }
}

void KalshiCandlePriceProvider::save_to_cache(
    const std::string& ticker,
    const std::vector<KalshiCandle>& candles) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& c : candles) j.push_back(candle_to_json(c));
    write_file_atomic(cache_path_for(ticker), j.dump());
}

std::vector<KalshiCandlePriceProvider::KalshiCandle>
KalshiCandlePriceProvider::fetch_uncached(const std::string& ticker,
                                           int64_t start_ts_sec,
                                           int64_t end_ts_sec) {
    try {
        return rest_.get_candlesticks(kSeries, ticker, start_ts_sec,
                                       end_ts_sec, /*period_interval_min=*/1);
    } catch (const std::exception& e) {
        spdlog::warn("KalshiCandlePriceProvider: fetch failed for {}: {}",
                     ticker, e.what());
        return {};
    }
}

bool KalshiCandlePriceProvider::prefetch_game(
    const std::string& game_date_iso,
    const std::string& home_tricode,
    const std::string& away_tricode) {
    int yy = 0, mm = 0, dd = 0;
    if (game_date_iso.size() >= 10) {
        try {
            yy = std::stoi(game_date_iso.substr(0, 4)) % 100;
            mm = std::stoi(game_date_iso.substr(5, 2));
            dd = std::stoi(game_date_iso.substr(8, 2));
        } catch (...) { return false; }
    }
    if (mm < 1 || mm > 12) return false;

    // Kalshi prod stores NBA tickers in UPPERCASE (e.g.
    // KXNBAGAME-26MAY19CLENYK-CLE). The candle endpoint is case-sensitive
    // so we have to call it with that exact case. The replay engine,
    // meanwhile, builds market tickers using format_nba_game_ticker (which
    // returns lowercase) — the strategy's filter is case-insensitive so
    // both shapes work for it. We store the candles map under BOTH cases
    // so get_quote() finds them regardless of how the replay engine
    // labeled the ticker.
    const std::string event_prefix_lower = ::trader::nba::format_nba_game_ticker(
        yy, mm, dd, to_lower(away_tricode), to_lower(home_tricode));
    const std::string event_prefix_upper = to_upper(event_prefix_lower);

    // 8h window centered on common NBA tipoff slots — 22:00 UTC of game
    // date (= 6pm EDT) through 06:00 UTC next day (= 2am EDT). Covers
    // east-coast prime time + west-coast prime time + OT.
    const int64_t day_start = midnight_utc_of(game_date_iso);
    const int64_t start_ts = day_start + 22 * 3600;
    const int64_t end_ts = day_start + 30 * 3600;

    bool any_data = false;
    for (const std::string& side_tricode : {home_tricode, away_tricode}) {
        const std::string ticker_upper =
            event_prefix_upper + "-" + to_upper(side_tricode);
        const std::string ticker_lower =
            event_prefix_lower + "-" + to_upper(side_tricode);
        // Cache keyed by the uppercase form (matches Kalshi's canonical
        // case). load_from_cache uses cache_path_for which embeds the
        // ticker — store under uppercase to keep on-disk filenames stable.
        auto candles = load_from_cache(ticker_upper);
        if (!candles.empty()) {
            ++cache_hits_;
        } else {
            ++cache_misses_;
            candles = fetch_uncached(ticker_upper, start_ts, end_ts);
            if (!candles.empty()) {
                save_to_cache(ticker_upper, candles);
            } else {
                ++empty_fetches_;
            }
        }
        // Sort by end_period_ts for binary-search lookup later.
        std::sort(candles.begin(), candles.end(),
                  [](const KalshiCandle& a, const KalshiCandle& b) {
                      return a.end_period_ts < b.end_period_ts;
                  });
        if (!candles.empty()) any_data = true;
        // Make both cases find the same data — replay_engine currently
        // uses lowercase, but live or future callers might use uppercase.
        by_ticker_[ticker_upper] = candles;
        by_ticker_[ticker_lower] = std::move(candles);
    }
    return any_data;
}

std::optional<IKalshiPriceProvider::Quote>
KalshiCandlePriceProvider::get_quote(const std::string& ticker,
                                      int64_t wall_clock_ts_sec) {
    auto it = by_ticker_.find(ticker);
    if (it == by_ticker_.end() || it->second.empty()) return std::nullopt;
    const auto& candles = it->second;
    // 1-minute bars labeled by end_period_ts. The bar covering
    // wall_clock_ts_sec is the first whose end >= wall_clock_ts_sec.
    auto cand_it = std::lower_bound(
        candles.begin(), candles.end(), wall_clock_ts_sec,
        [](const KalshiCandle& c, int64_t ts) {
            return c.end_period_ts < ts;
        });
    if (cand_it == candles.end()) {
        // Past the last bar — fall back to last bar (game has ended).
        cand_it = std::prev(candles.end());
    }
    const auto& c = *cand_it;
    // Skip "no-trade" bars where Kalshi reports zero bid/ask. The
    // strategy treats a 0/0 quote as a malformed market — better to
    // return nullopt and let it skip the tick.
    if (c.yes_bid_close <= 0.0 && c.yes_ask_close <= 0.0) {
        return std::nullopt;
    }
    Quote q;
    q.yes_bid = c.yes_bid_close;
    q.yes_ask = c.yes_ask_close;
    q.volume = c.volume;
    return q;
}

} // namespace trader::backtest
