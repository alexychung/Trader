#include "backtest/settled_markets_fetcher.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace trader::backtest {

namespace {

// YYYY-MM-DD -> unix seconds (UTC). Kalshi uses seconds since epoch for
// min_close_ts/max_close_ts query params.
long iso_date_to_unix(const std::string& iso_date, bool end_of_day) {
    if (iso_date.size() < 10) return 0;
    std::tm tm{};
    tm.tm_year = std::stoi(iso_date.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(iso_date.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(iso_date.substr(8, 2));
    tm.tm_hour = end_of_day ? 23 : 0;
    tm.tm_min  = end_of_day ? 59 : 0;
    tm.tm_sec  = end_of_day ? 59 : 0;
#ifdef _WIN32
    return static_cast<long>(_mkgmtime(&tm));
#else
    return static_cast<long>(timegm(&tm));
#endif
}

std::string sanitize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') out += c;
        else out += '_';
    }
    return out;
}

// Kalshi's `category` field on settled markets is often empty, so we also
// accept well-known ticker prefixes per logical category.
// Polite inter-request delay for Kalshi's public API. The unauthenticated rate
// limit trips around 5-10 rps; 300ms keeps us well below.
constexpr std::chrono::milliseconds kPageDelay{300};

// Retry a REST GET once on HTTP 429 with exponential backoff.
trader::kalshi::HttpResponse get_with_retry(trader::kalshi::KalshiRestClient& rest,
                                              const std::string& path) {
    auto resp = rest.get(path);
    if (resp.status_code != 429) return resp;
    spdlog::debug("rate limited, backing off 2s: {}", path);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    resp = rest.get(path);
    if (resp.status_code == 429) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        resp = rest.get(path);
    }
    return resp;
}

bool matches_category(const trader::kalshi::KalshiMarket& m,
                      const std::string& category) {
    if (category.empty()) return true;
    if (!m.category.empty() && m.category == category) return true;
    if (category == "weather" && m.ticker.rfind("KXHIGH", 0) == 0) return true;
    if (category == "economics"
        && (m.ticker.rfind("KXCPI", 0) == 0 || m.ticker.rfind("KXNFP", 0) == 0)) return true;
    if (category == "fed" && m.ticker.rfind("KXFED", 0) == 0) return true;
    return false;
}

} // namespace

SettledMarketsFetcher::SettledMarketsFetcher(trader::kalshi::KalshiRestClient& rest,
                                              std::string cache_dir)
    : rest_(rest), cache_dir_(std::move(cache_dir)) {
    if (!cache_dir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(cache_dir_, ec);
    }
}

std::string SettledMarketsFetcher::markets_cache_path(const std::string& start_date,
                                                       const std::string& end_date,
                                                       const std::string& category) const {
    if (cache_dir_.empty()) return "";
    std::ostringstream p;
    p << cache_dir_ << "/markets_" << sanitize(start_date) << "_"
      << sanitize(end_date) << "_" << sanitize(category.empty() ? "all" : category) << ".json";
    return p.str();
}

std::string SettledMarketsFetcher::trades_cache_path(const std::string& ticker) const {
    if (cache_dir_.empty()) return "";
    return cache_dir_ + "/trades_" + sanitize(ticker) + ".json";
}

std::vector<trader::kalshi::KalshiMarket> SettledMarketsFetcher::fetch_settled_markets(
    const std::string& start_date, const std::string& end_date,
    const std::string& category, int max_markets) {

    auto path = markets_cache_path(start_date, end_date, category);
    if (!path.empty()) {
        std::ifstream in(path);
        if (in.good()) {
            try {
                nlohmann::json j; in >> j;
                std::vector<trader::kalshi::KalshiMarket> out;
                for (const auto& m : j) {
                    out.push_back(trader::kalshi::KalshiRestClient::parse_market(m));
                }
                ++cache_hits_;
                return out;
            } catch (...) {
                // fall through to refetch
            }
        }
    }
    ++cache_misses_;

    long min_ts = iso_date_to_unix(start_date, false);
    long max_ts = iso_date_to_unix(end_date, true);

    std::vector<trader::kalshi::KalshiMarket> out;
    std::string cursor;
    nlohmann::json all = nlohmann::json::array();

    // Cap total pages scanned regardless of filter match count. Kalshi's
    // `status=settled` endpoint returns markets across all categories with
    // sparse coverage for some (e.g., weather markets didn't exist in
    // volume before 2026-02), so a category filter with a large max_markets
    // can otherwise paginate the entire settled history.
    constexpr int kMaxPages = 100;  // 100 pages × 100/page = 10,000 raw markets
    int pages = 0;
    while (static_cast<int>(out.size()) < max_markets && pages < kMaxPages) {
        std::ostringstream q;
        q << "/markets?status=settled&limit=100"
          << "&min_close_ts=" << min_ts
          << "&max_close_ts=" << max_ts;
        if (!cursor.empty()) q << "&cursor=" << cursor;

        auto resp = get_with_retry(rest_, q.str());
        ++pages;
        std::this_thread::sleep_for(kPageDelay);
        if (!resp.ok()) {
            spdlog::warn("settled markets fetch failed HTTP {} body={}",
                         resp.status_code, resp.body.substr(0, 200));
            break;
        }

        try {
            auto j = nlohmann::json::parse(resp.body);
            if (!j.contains("markets") || !j["markets"].is_array()) break;
            for (const auto& item : j["markets"]) {
                auto m = trader::kalshi::KalshiRestClient::parse_market(item);
                if (matches_category(m, category)) {
                    out.push_back(m);
                    all.push_back(item);
                    if (static_cast<int>(out.size()) >= max_markets) break;
                }
            }
            cursor = j.value("cursor", "");
            if (cursor.empty()) break;
        } catch (const nlohmann::json::exception& e) {
            spdlog::error("settled markets parse failed: {}", e.what());
            break;
        }
    }
    if (pages >= kMaxPages && static_cast<int>(out.size()) < max_markets) {
        spdlog::warn("settled markets fetch hit kMaxPages={} cap with {} matches "
                     "(category='{}'); widen date range or lower max_markets",
                     kMaxPages, out.size(), category);
    }

    if (!path.empty() && !all.empty()) {
        std::ofstream outf(path);
        if (outf.good()) outf << all.dump();
    }
    return out;
}

std::vector<SettledTrade> SettledMarketsFetcher::fetch_trades(const std::string& ticker,
                                                                int max_trades) {
    auto path = trades_cache_path(ticker);
    if (!path.empty()) {
        std::ifstream in(path);
        if (in.good()) {
            try {
                nlohmann::json j; in >> j;
                std::vector<SettledTrade> out;
                for (const auto& t : j) {
                    SettledTrade st;
                    st.ticker = ticker;
                    st.yes_price = t.value("yes_price", 0.0);
                    st.count = t.value("count", 0);
                    st.taker_side = t.value("taker_side", "");
                    st.created_at = t.value("created_at", "");
                    out.push_back(st);
                }
                // Cache on disk is stored in fetch order (API returns newest
                // first). The driver assumes oldest-first timelines, so sort
                // to match the fresh-fetch path at the bottom of this method.
                std::sort(out.begin(), out.end(),
                          [](const SettledTrade& a, const SettledTrade& b) {
                              return a.created_at < b.created_at;
                          });
                ++cache_hits_;
                return out;
            } catch (...) {}
        }
    }
    ++cache_misses_;

    std::vector<SettledTrade> out;
    std::string cursor;
    nlohmann::json cached = nlohmann::json::array();

    while (static_cast<int>(out.size()) < max_trades) {
        std::ostringstream q;
        q << "/markets/trades?ticker=" << ticker << "&limit=1000";
        if (!cursor.empty()) q << "&cursor=" << cursor;
        auto resp = get_with_retry(rest_, q.str());
        std::this_thread::sleep_for(kPageDelay);
        if (!resp.ok()) {
            spdlog::debug("trades fetch {} failed HTTP {}", ticker, resp.status_code);
            break;
        }
        try {
            auto j = nlohmann::json::parse(resp.body);
            if (!j.contains("trades") || !j["trades"].is_array()) break;
            for (const auto& t : j["trades"]) {
                SettledTrade st;
                st.ticker = ticker;
                // Kalshi returns yes_price_dollars as a decimal string (e.g., "0.5500").
                // Also accept yes_price as an integer cents fallback.
                auto read_price = [&](const char* key) -> double {
                    if (!t.contains(key)) return -1.0;
                    const auto& v = t[key];
                    if (v.is_string()) {
                        try { return std::stod(v.get<std::string>()); } catch (...) { return -1.0; }
                    }
                    if (v.is_number()) return v.get<double>();
                    return -1.0;
                };
                double raw_price = read_price("yes_price_dollars");
                if (raw_price < 0.0) raw_price = read_price("yes_price");
                if (raw_price > 1.0) raw_price /= 100.0;  // cents -> dollars
                if (raw_price < 0.0) raw_price = 0.0;
                st.yes_price = raw_price;

                auto read_count = [&](const char* key) -> int {
                    if (!t.contains(key)) return -1;
                    const auto& v = t[key];
                    if (v.is_number()) return v.get<int>();
                    if (v.is_string()) {
                        try { return static_cast<int>(std::stod(v.get<std::string>())); }
                        catch (...) { return -1; }
                    }
                    return -1;
                };
                int c = read_count("count_fp");
                if (c < 0) c = read_count("count");
                st.count = std::max(0, c);

                st.taker_side = t.value("taker_side", "");
                st.created_at = t.value("created_time", t.value("created_at", ""));
                out.push_back(st);

                nlohmann::json norm;
                norm["yes_price"] = st.yes_price;
                norm["count"] = st.count;
                norm["taker_side"] = st.taker_side;
                norm["created_at"] = st.created_at;
                cached.push_back(norm);

                if (static_cast<int>(out.size()) >= max_trades) break;
            }
            cursor = j.value("cursor", "");
            if (cursor.empty()) break;
        } catch (const nlohmann::json::exception& e) {
            spdlog::error("trades parse {}: {}", ticker, e.what());
            break;
        }
    }

    // Oldest-first.
    std::sort(out.begin(), out.end(),
              [](const SettledTrade& a, const SettledTrade& b) {
                  return a.created_at < b.created_at;
              });

    if (!path.empty() && !cached.empty()) {
        std::ofstream outf(path);
        if (outf.good()) outf << cached.dump();
    }
    return out;
}

std::vector<SettledMarket> SettledMarketsFetcher::fetch_all(const std::string& start_date,
                                                              const std::string& end_date,
                                                              const std::string& category,
                                                              int max_markets) {
    auto markets = fetch_settled_markets(start_date, end_date, category, max_markets);
    std::vector<SettledMarket> out;
    out.reserve(markets.size());
    for (const auto& m : markets) {
        SettledMarket sm;
        sm.market = m;
        sm.outcome_yes = (m.result == "yes");
        sm.resolution_time = m.expiration_time.empty() ? m.close_time : m.expiration_time;
        sm.trades = fetch_trades(m.ticker);
        out.push_back(std::move(sm));
    }
    spdlog::info("Fetched {} settled markets (category='{}', {}..{})",
                 out.size(), category, start_date, end_date);
    return out;
}

} // namespace trader::backtest
