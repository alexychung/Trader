#include "backtest/nba_pbp_fetcher.hpp"

#include "core/http.hpp"
#include "strategy/nba/nba_score_feed.hpp"  // parse_iso8601_clock_seconds

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace trader::backtest {

namespace {

// cdn.nba.com requires the browser-flavored UA + nba.com Referer/Origin to
// accept the request (otherwise 403). The stats.* extra tokens are kept for
// historical compat with a future stats.nba.com fallback even though they
// have no effect on cdn.nba.com.
const std::vector<std::pair<std::string, std::string>>& nba_headers() {
    static const std::vector<std::pair<std::string, std::string>> h = {
        {"User-Agent",
         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
         "(KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36"},
        {"Referer", "https://www.nba.com/"},
        {"Origin", "https://www.nba.com"},
        {"Accept-Language", "en-US,en;q=0.9"},
    };
    return h;
}

int compute_regulation_remaining(int period, int clock_seconds) {
    if (period < 1) return 0;
    if (period >= 5) return 0;
    return (4 - period) * 720 + clock_seconds;
}

// scheduleLeagueV2.json represents gameDate as "MM/DD/YYYY HH:MM:SS".
// Convert to ISO "YYYY-MM-DD". Returns empty on parse failure.
std::string to_iso_date(const std::string& schedule_date) {
    if (schedule_date.size() < 10) return {};
    if (schedule_date[2] != '/' || schedule_date[5] != '/') return {};
    try {
        int mm = std::stoi(schedule_date.substr(0, 2));
        int dd = std::stoi(schedule_date.substr(3, 2));
        int yyyy = std::stoi(schedule_date.substr(6, 4));
        char buf[11];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", yyyy, mm, dd);
        return buf;
    } catch (...) { return {}; }
}

} // namespace

NbaPbpFetcher::NbaPbpFetcher(std::string cache_dir)
    : cache_dir_(std::move(cache_dir)) {
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
    if (ec) {
        spdlog::warn("NbaPbpFetcher: could not create cache dir {}: {}",
                     cache_dir_, ec.message());
    }
}

std::string NbaPbpFetcher::schedule_cache_path() const {
    return cache_dir_ + "/scheduleLeagueV2.json";
}

std::string NbaPbpFetcher::pbp_cache_path(const std::string& game_id) const {
    return cache_dir_ + "/pbp_" + game_id + ".json";
}

const std::string& NbaPbpFetcher::load_schedule() {
    if (!schedule_body_cached_.empty()) return schedule_body_cached_;
    const std::string cache_path = schedule_cache_path();
    std::string body = read_file(cache_path);
    if (!body.empty()) {
        ++cache_hits_;
    } else {
        ++cache_misses_;
        const std::string url =
            "https://cdn.nba.com/static/json/staticData/scheduleLeagueV2.json";
        auto resp = trader::https_get_with_headers(url, nba_headers());
        if (!resp.ok()) {
            spdlog::warn("NBA schedule fetch failed: HTTP {} body[0:200]={}",
                         resp.status_code, resp.body.substr(0, 200));
            return schedule_body_cached_;  // empty
        }
        body = resp.body;
        write_file_atomic(cache_path, body);
    }
    schedule_body_cached_ = std::move(body);
    return schedule_body_cached_;
}

std::string NbaPbpFetcher::read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void NbaPbpFetcher::write_file_atomic(const std::string& path,
                                       const std::string& content) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            spdlog::warn("NbaPbpFetcher: write_file_atomic open failed: {}", tmp);
            return;
        }
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // On Windows, rename fails if the destination exists. Fall back to
        // remove + rename.
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            spdlog::warn("NbaPbpFetcher: rename {} -> {} failed: {}",
                         tmp, path, ec.message());
        }
    }
}

std::vector<PbpGameSummary> NbaPbpFetcher::fetch_games_on_date(
    const std::string& date_iso) {
    const std::string& sched = load_schedule();
    if (sched.empty()) return {};
    return parse_schedule_for_date(sched, date_iso);
}

std::vector<PbpEvent> NbaPbpFetcher::fetch_pbp(const std::string& game_id) {
    const std::string cache_path = pbp_cache_path(game_id);

    std::string body = read_file(cache_path);
    if (!body.empty()) {
        ++cache_hits_;
    } else {
        ++cache_misses_;
        // cdn.nba.com PBP path: /static/json/liveData/playbyplay/playbyplay_{gameId}.json
        const std::string url =
            "https://cdn.nba.com/static/json/liveData/playbyplay/playbyplay_" +
            game_id + ".json";
        auto resp = trader::https_get_with_headers(url, nba_headers());
        if (!resp.ok()) {
            spdlog::warn("NBA PBP fetch failed for {}: HTTP {} body[0:200]={}",
                         game_id, resp.status_code, resp.body.substr(0, 200));
            return {};
        }
        body = resp.body;
        write_file_atomic(cache_path, body);
    }

    return parse_pbp(body);
}

std::vector<PbpGameSummary> NbaPbpFetcher::parse_schedule_for_date(
    const std::string& body, const std::string& date_iso) {
    std::vector<PbpGameSummary> out;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        spdlog::warn("NBA schedule JSON parse failed: {}", e.what());
        return out;
    }
    if (!j.contains("leagueSchedule") || !j["leagueSchedule"].is_object()) {
        return out;
    }
    const auto& sched = j["leagueSchedule"];
    if (!sched.contains("gameDates") || !sched["gameDates"].is_array()) {
        return out;
    }
    for (const auto& gd : sched["gameDates"]) {
        const std::string raw_date = gd.value("gameDate", std::string{});
        const std::string iso = to_iso_date(raw_date);
        if (iso != date_iso) continue;
        if (!gd.contains("games") || !gd["games"].is_array()) continue;
        for (const auto& g : gd["games"]) {
            PbpGameSummary s;
            s.game_id = g.value("gameId", std::string{});
            s.game_status = g.value("gameStatus", 0);
            s.game_date_iso = date_iso;
            if (g.contains("homeTeam") && g["homeTeam"].is_object()) {
                const auto& ht = g["homeTeam"];
                s.home_tricode = ht.value("teamTricode", std::string{});
                s.home_final_score = ht.value("score", 0);
            }
            if (g.contains("awayTeam") && g["awayTeam"].is_object()) {
                const auto& at = g["awayTeam"];
                s.away_tricode = at.value("teamTricode", std::string{});
                s.away_final_score = at.value("score", 0);
            }
            if (!s.game_id.empty() && !s.home_tricode.empty() &&
                !s.away_tricode.empty()) {
                out.push_back(std::move(s));
            }
        }
    }
    return out;
}

std::vector<PbpEvent> NbaPbpFetcher::parse_pbp(const std::string& body) {
    std::vector<PbpEvent> out;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        spdlog::warn("NBA PBP JSON parse failed: {}", e.what());
        return out;
    }

    // playbyplayv3 shape: { game: { actions: [ ... ] } }
    const nlohmann::json* actions = nullptr;
    if (j.contains("game") && j["game"].is_object()) {
        const auto& g = j["game"];
        if (g.contains("actions") && g["actions"].is_array()) {
            actions = &g["actions"];
        }
    }
    if (actions == nullptr) {
        // Fall back to root-level "actions" (some endpoints variant).
        if (j.contains("actions") && j["actions"].is_array()) {
            actions = &j["actions"];
        }
    }
    if (actions == nullptr) return out;

    out.reserve(actions->size());
    for (const auto& a : *actions) {
        PbpEvent e;
        e.action_number = a.value("actionNumber", 0);
        e.period = a.value("period", 0);
        // scoreHome/scoreAway can be string or int in different NBA Stats
        // generations. Be defensive.
        auto parse_score = [&a](const char* key) -> int {
            if (!a.contains(key)) return 0;
            const auto& v = a.at(key);
            if (v.is_number_integer()) return v.get<int>();
            if (v.is_number()) return static_cast<int>(v.get<double>());
            if (v.is_string()) {
                try { return std::stoi(v.get<std::string>()); }
                catch (...) { return 0; }
            }
            return 0;
        };
        e.score_home = parse_score("scoreHome");
        e.score_away = parse_score("scoreAway");
        // Clock comes as ISO 8601 duration: "PT05M23.00S"
        const std::string clock_iso = a.value("clock", std::string{});
        e.clock_seconds = ::trader::nba::parse_iso8601_clock_seconds(clock_iso);
        e.regulation_seconds_remaining =
            compute_regulation_remaining(e.period, e.clock_seconds);
        e.action_type = a.value("actionType", std::string{});
        e.sub_type = a.value("subType", std::string{});
        e.description = a.value("description", std::string{});
        out.push_back(std::move(e));
    }

    // Ensure chronological order. NBA Stats usually returns them in order
    // already, but don't assume.
    std::sort(out.begin(), out.end(),
              [](const PbpEvent& a, const PbpEvent& b) {
                  return a.action_number < b.action_number;
              });
    return out;
}

} // namespace trader::backtest
