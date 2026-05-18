#pragma once

#include <string>
#include <vector>

namespace trader::nba {

// Live NBA game state snapshot from cdn.nba.com.
struct NbaGameSnapshot {
    // Empty when the snapshot is invalid / placeholder.
    bool valid = false;

    // NBA Stats game ID, e.g. "0042500301" (8 digits typically; first 2 are
    // season-type, next 2 the season year, last 5 the game number). Doesn't
    // appear in Kalshi tickers — use date + tricodes to bridge.
    std::string game_id;

    // gameStatus from cdn.nba.com: 1=scheduled (pre-game), 2=live, 3=final.
    int game_status = 0;
    std::string game_status_text;  // raw "Q3 5:23" / "Final" / "7:00 pm ET"

    // ISO date of the game (NBA wall-clock date for the matchup),
    // e.g. "2026-05-18". Use with home/away tricodes to derive Kalshi ticker.
    std::string game_date_iso;

    // Current period: 1..4 in regulation; 5,6,...  in successive overtimes.
    int period = 0;

    // Seconds remaining in the *current* period only. Use
    // regulation_seconds_remaining for end-of-regulation horizon.
    int game_clock_seconds = 0;

    // Convenience: seconds remaining until end of regulation. Computed as
    //   (4 - period) * 720 + game_clock_seconds         for period in [1..4]
    //   0                                                 for period >= 5 (OT)
    // Returns 0 when status is pre-game or final.
    int regulation_seconds_remaining = 0;

    // NBA tricodes — uppercase as returned by the API. Match against
    // lowercase Kalshi ticker codes by lowercasing here.
    std::string home_tricode;
    std::string away_tricode;
    int home_score = 0;
    int away_score = 0;

    // Convenience accessors.
    bool is_live() const { return game_status == 2; }
    bool is_final() const { return game_status == 3; }
    int home_lead() const { return home_score - away_score; }
};

// Parse an ISO 8601 duration of the form "PT{minutes}M{seconds}S" or
// "PT{seconds}S" (with optional decimal seconds) into integer seconds,
// rounding down. Returns 0 on parse failure. Public for tests.
//   "PT05M23.00S" → 323
//   "PT0M0.00S"   → 0
//   "PT5M23S"     → 323
//   "PT12M00.50S" → 720
int parse_iso8601_clock_seconds(const std::string& iso);

// Parse a cdn.nba.com `todaysScoreboard_00.json` response body into one
// snapshot per game. Caller passes the raw body string; this never throws on
// malformed input — returns an empty vector and logs at warn level.
std::vector<NbaGameSnapshot> parse_scoreboard_response(const std::string& body);

class NbaScoreFeed {
public:
    virtual ~NbaScoreFeed() = default;

    // Fetches https://cdn.nba.com/static/json/liveData/scoreboard/todaysScoreboard_00.json
    // with a browser-like User-Agent (the CDN returns inconsistent results for
    // bot-style UAs). Returns one snapshot per game listed. Empty on transport
    // failure or parse failure (both logged). Virtual so test code can stub it
    // without touching the network.
    virtual std::vector<NbaGameSnapshot> fetch_today_scoreboard();

    // Default endpoint; override only in tests or for staging mirrors.
    void set_scoreboard_url(const std::string& url) { scoreboard_url_ = url; }
    const std::string& scoreboard_url() const { return scoreboard_url_; }

private:
    std::string scoreboard_url_ =
        "https://cdn.nba.com/static/json/liveData/scoreboard/todaysScoreboard_00.json";
};

} // namespace trader::nba
