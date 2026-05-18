// Smoke test for the NBA pipeline.
//
// 1. Hits cdn.nba.com/static/json/liveData/scoreboard/todaysScoreboard_00.json
//    and prints today's games — verifies the HTTP path + JSON shape + header
//    requirements end-to-end against the live CDN.
// 2. For each live game, computes our arcsine win-probability and the derived
//    Kalshi ticker so we can eyeball whether the math + ticker-mapping look
//    sane.
//
// Run after a code change to the NBA components; no Kalshi auth required.

#include "strategy/nba/kalshi_nba_parser.hpp"
#include "strategy/nba/nba_score_feed.hpp"
#include "strategy/nba/win_probability.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace trader::nba;

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string status_label(int s) {
    switch (s) {
        case 1: return "scheduled";
        case 2: return "LIVE";
        case 3: return "final";
        default: return "?";
    }
}

} // namespace

int main() {
    auto logger = spdlog::stdout_color_mt("nba_smoke");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);

    std::cout << "=== NBA smoke test ===\n";
    std::cout << "Fetching cdn.nba.com/static/json/liveData/scoreboard/todaysScoreboard_00.json ...\n\n";

    NbaScoreFeed feed;
    auto games = feed.fetch_today_scoreboard();
    if (games.empty()) {
        std::cerr << "FAIL: scoreboard returned 0 games.\n"
                  << "Likely causes:\n"
                  << "  - off-season / no games today\n"
                  << "  - cdn.nba.com response shape changed\n"
                  << "  - browser-like UA + Referer were not accepted\n"
                  << "Check trader.log warnings for HTTP status and parse errors.\n";
        return 2;
    }

    std::cout << "Got " << games.size() << " game(s):\n\n";
    int live = 0;
    for (const auto& g : games) {
        if (g.is_live()) ++live;
        std::cout << "  " << g.away_tricode << " @ " << g.home_tricode
                  << "  [" << status_label(g.game_status) << "]";
        if (g.is_live()) {
            std::cout << "  Q" << g.period
                      << "  " << (g.game_clock_seconds / 60) << ":"
                      << std::setfill('0') << std::setw(2)
                      << (g.game_clock_seconds % 60)
                      << "  " << g.away_score << "-" << g.home_score
                      << "  (reg_remaining=" << g.regulation_seconds_remaining << "s)";
        } else if (g.is_final()) {
            std::cout << "  " << g.away_score << "-" << g.home_score;
        } else {
            std::cout << "  (" << g.game_status_text << ")";
        }
        std::cout << "\n";

        // Compute the Kalshi ticker the strategy would look for.
        if (g.game_date_iso.size() >= 10) {
            int yy = std::stoi(g.game_date_iso.substr(2, 2));
            int mm = std::stoi(g.game_date_iso.substr(5, 2));
            int dd = std::stoi(g.game_date_iso.substr(8, 2));
            std::string ticker = format_nba_game_ticker(
                yy, mm, dd, to_lower(g.away_tricode), to_lower(g.home_tricode));
            std::cout << "      kalshi_ticker_expected: " << ticker << "\n";
        }

        // If live, compute our home WP.
        if (g.is_live() && g.regulation_seconds_remaining > 0) {
            double home_wp = WinProbability::survival_probability(
                static_cast<double>(g.home_lead()),
                static_cast<double>(g.regulation_seconds_remaining));
            std::cout << "      arcsine_home_wp=" << std::fixed
                      << std::setprecision(3) << home_wp
                      << "  (home_lead=" << g.home_lead() << ")\n";
        }
    }
    std::cout << "\nSummary: " << games.size() << " games, " << live << " live.\n";
    std::cout << "PASS\n";
    return 0;
}
