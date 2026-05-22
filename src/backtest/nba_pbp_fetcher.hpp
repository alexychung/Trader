#pragma once

// Historical NBA play-by-play fetcher (cdn.nba.com).
//
// We deliberately use cdn.nba.com rather than stats.nba.com — the latter
// is geo/edge-blocked at TCP-connect from many networks (the user's
// included), while cdn.nba.com Akamai cluster is widely reachable.
//
// Three endpoints:
//   1) /static/json/staticData/scheduleLeagueV2.json
//        → full season schedule (gameDates[].games[]) with gameId, tricodes,
//          status, final scores. ~8MB JSON, cached once per backtest run.
//   2) /static/json/liveData/playbyplay/playbyplay_{gameId}.json
//        → per-game ordered action list with score state at each event.
//          Available for past games for the current season window.
//   3) /static/json/liveData/scoreboard/todaysScoreboard_00.json
//        → today's live scoreboard (used by the live bot, NOT by this
//          backtest fetcher).
//
// All endpoints are public but require a browser-like User-Agent + Referer
// (otherwise cdn.nba.com returns 403). Responses are cached to `cache_dir`
// so backtest re-runs don't hit the network.
//
// Used by the backtest replay engine — each PbpEvent is one "tick" the
// strategy sees, with the regulation clock + score state at that moment.

#include <chrono>
#include <string>
#include <vector>

namespace trader::backtest {

struct PbpEvent {
    int action_number = 0;        // monotonically increasing within a game
    int period = 0;               // 1-4 regulation, 5+ OT
    int clock_seconds = 0;        // seconds left in *current* period
    int regulation_seconds_remaining = 0;  // (4-period)*720 + clock_seconds; 0 in OT
    int score_home = 0;
    int score_away = 0;
    std::string action_type;      // "made", "missed", "rebound", "foul", "timeout", ...
    std::string sub_type;          // free-form qualifier ("3pt", "drawn", ...)
    std::string description;       // human-readable

    int home_lead() const { return score_home - score_away; }
};

struct PbpGameSummary {
    std::string game_id;
    std::string game_date_iso;
    std::string home_tricode;
    std::string away_tricode;
    int home_final_score = 0;
    int away_final_score = 0;
    int game_status = 0;          // 1 pre, 2 live, 3 final
    bool home_won() const { return home_final_score > away_final_score; }
};

class NbaPbpFetcher {
public:
    // cache_dir: directory for JSON cache files. Created if absent.
    explicit NbaPbpFetcher(std::string cache_dir);

    // List games played on `date_iso` (YYYY-MM-DD). Cached per date.
    std::vector<PbpGameSummary> fetch_games_on_date(const std::string& date_iso);

    // Full PBP for one game, ordered by action_number. Cached per game_id.
    std::vector<PbpEvent> fetch_pbp(const std::string& game_id);

    // Diagnostics.
    int cache_hits() const { return cache_hits_; }
    int cache_misses() const { return cache_misses_; }

    // Pure parsers — exposed for unit testing without HTTP. Pass the raw
    // JSON response body string.
    //
    // parse_schedule_for_date filters scheduleLeagueV2.json's games for the
    // requested YYYY-MM-DD date_iso. parse_pbp handles both the cdn.nba.com
    // and stats.nba.com response shapes (they're identical at the action level).
    static std::vector<PbpGameSummary> parse_schedule_for_date(
        const std::string& body, const std::string& date_iso);
    static std::vector<PbpEvent> parse_pbp(const std::string& body);

private:
    std::string cache_dir_;
    int cache_hits_ = 0;
    int cache_misses_ = 0;
    // Schedule JSON is cached on first access for the lifetime of the
    // fetcher to avoid re-reading the 8MB file from disk per date.
    std::string schedule_body_cached_;

    std::string schedule_cache_path() const;
    std::string pbp_cache_path(const std::string& game_id) const;
    // Loads (with on-disk + in-memory caching) the full league schedule.
    const std::string& load_schedule();

    // Read whole file into a string; returns "" if missing or unreadable.
    static std::string read_file(const std::string& path);
    // Atomically write content to path (tmp + rename).
    static void write_file_atomic(const std::string& path,
                                   const std::string& content);
};

} // namespace trader::backtest
