#include "strategy/nba/nba_score_feed.hpp"

#include "core/http.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cctype>
#include <cstdlib>

namespace trader::nba {

namespace {

// Compute end-of-regulation horizon from period + per-period seconds.
int compute_regulation_remaining(int period, int clock_seconds, int status) {
    if (status != 2) return 0;        // pre-game or final
    if (period < 1) return 0;
    if (period >= 5) return 0;        // overtime — no "end of regulation" left
    return (4 - period) * 720 + clock_seconds;
}

} // namespace

int parse_iso8601_clock_seconds(const std::string& iso) {
    // Accept "PT[m]M[s][.f]S" or "PT[s][.f]S". Anything else → 0.
    // We never trust the decimal portion; truncate to integer seconds.
    if (iso.size() < 4 || iso[0] != 'P' || iso[1] != 'T') return 0;

    int minutes = 0;
    int seconds = 0;
    int cursor = 2;
    int num_start = cursor;
    bool saw_minutes = false;

    while (cursor < static_cast<int>(iso.size())) {
        char c = iso[cursor];
        if (c == 'M') {
            if (cursor == num_start) return 0;  // empty number
            std::string digits = iso.substr(num_start, cursor - num_start);
            try {
                minutes = std::stoi(digits);
            } catch (...) { return 0; }
            saw_minutes = true;
            ++cursor;
            num_start = cursor;
        } else if (c == 'S') {
            if (cursor == num_start) return 0;
            std::string digits = iso.substr(num_start, cursor - num_start);
            // Truncate at decimal point if present.
            auto dot = digits.find('.');
            if (dot != std::string::npos) digits = digits.substr(0, dot);
            if (digits.empty()) return 0;
            try {
                seconds = std::stoi(digits);
            } catch (...) { return 0; }
            ++cursor;
            num_start = cursor;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            ++cursor;
        } else {
            return 0;
        }
    }
    (void)saw_minutes;
    return minutes * 60 + seconds;
}

std::vector<NbaGameSnapshot> parse_scoreboard_response(const std::string& body) {
    std::vector<NbaGameSnapshot> out;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        spdlog::warn("NBA scoreboard JSON parse failed: {}", e.what());
        return out;
    }

    if (!j.contains("scoreboard") || !j["scoreboard"].is_object()) {
        spdlog::warn("NBA scoreboard response missing 'scoreboard' object");
        return out;
    }
    const auto& sb = j["scoreboard"];
    std::string date_iso = sb.value("gameDate", std::string{});

    if (!sb.contains("games") || !sb["games"].is_array()) {
        return out;
    }
    for (const auto& g : sb["games"]) {
        NbaGameSnapshot snap;
        snap.game_id = g.value("gameId", std::string{});
        snap.game_status = g.value("gameStatus", 0);
        snap.game_status_text = g.value("gameStatusText", std::string{});
        snap.period = g.value("period", 0);
        snap.game_clock_seconds = parse_iso8601_clock_seconds(
            g.value("gameClock", std::string{}));
        snap.game_date_iso = date_iso;

        if (g.contains("homeTeam") && g["homeTeam"].is_object()) {
            const auto& ht = g["homeTeam"];
            snap.home_tricode = ht.value("teamTricode", std::string{});
            snap.home_score = ht.value("score", 0);
        }
        if (g.contains("awayTeam") && g["awayTeam"].is_object()) {
            const auto& at = g["awayTeam"];
            snap.away_tricode = at.value("teamTricode", std::string{});
            snap.away_score = at.value("score", 0);
        }

        snap.regulation_seconds_remaining = compute_regulation_remaining(
            snap.period, snap.game_clock_seconds, snap.game_status);

        // Minimum validity bar: must have a game ID and both tricodes.
        snap.valid = !snap.game_id.empty() &&
                     !snap.home_tricode.empty() && !snap.away_tricode.empty();
        if (snap.valid) out.push_back(std::move(snap));
    }
    return out;
}

std::vector<NbaGameSnapshot> NbaScoreFeed::fetch_today_scoreboard() {
    // cdn.nba.com static JSON is unauthenticated but has been observed to
    // 403 or return stale cached payloads under bot-pattern User-Agents.
    // Sending a recent Chrome UA + nba.com Referer/Origin is the recipe
    // every community client uses; we follow it.
    const std::vector<std::pair<std::string, std::string>> headers = {
        {"User-Agent",
         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
         "(KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36"},
        {"Referer", "https://www.nba.com/"},
        {"Origin", "https://www.nba.com"},
        {"Accept-Language", "en-US,en;q=0.9"},
    };
    auto resp = trader::https_get_with_headers(scoreboard_url_, headers);
    if (!resp.ok()) {
        spdlog::warn("NBA scoreboard fetch failed: HTTP {} body[0:200]={}",
                     resp.status_code, resp.body.substr(0, 200));
        return {};
    }
    auto games = parse_scoreboard_response(resp.body);
    spdlog::debug("NBA scoreboard: parsed {} games", games.size());
    return games;
}

} // namespace trader::nba
