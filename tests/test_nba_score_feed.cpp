#include "strategy/nba/nba_score_feed.hpp"

#include <gtest/gtest.h>

using trader::nba::parse_iso8601_clock_seconds;
using trader::nba::parse_scoreboard_response;

TEST(NbaIsoClock, ParsesStandardForms) {
    EXPECT_EQ(parse_iso8601_clock_seconds("PT05M23.00S"), 323);
    EXPECT_EQ(parse_iso8601_clock_seconds("PT0M0.00S"), 0);
    EXPECT_EQ(parse_iso8601_clock_seconds("PT12M00.50S"), 720);
    EXPECT_EQ(parse_iso8601_clock_seconds("PT5M23S"), 323);
    EXPECT_EQ(parse_iso8601_clock_seconds("PT0M59S"), 59);
}

TEST(NbaIsoClock, ParsesSecondsOnly) {
    EXPECT_EQ(parse_iso8601_clock_seconds("PT45S"), 45);
    EXPECT_EQ(parse_iso8601_clock_seconds("PT45.32S"), 45);
}

TEST(NbaIsoClock, ReturnsZeroOnGarbage) {
    EXPECT_EQ(parse_iso8601_clock_seconds(""), 0);
    EXPECT_EQ(parse_iso8601_clock_seconds("garbage"), 0);
    EXPECT_EQ(parse_iso8601_clock_seconds("PT"), 0);
    EXPECT_EQ(parse_iso8601_clock_seconds("PTM5S"), 0);
    EXPECT_EQ(parse_iso8601_clock_seconds("PT5MS"), 0);
}

TEST(NbaScoreboardParse, ParsesLiveAndScheduledGames) {
    // Minimal realistic shape of cdn.nba.com todaysScoreboard_00.json.
    const std::string body = R"({
      "scoreboard": {
        "gameDate": "2026-05-18",
        "leagueId": "00",
        "games": [
          {
            "gameId": "0042500301",
            "gameStatus": 2,
            "gameStatusText": "Q3 5:23",
            "period": 3,
            "gameClock": "PT05M23.00S",
            "homeTeam": { "teamTricode": "OKC", "score": 67 },
            "awayTeam": { "teamTricode": "SAS", "score": 58 }
          },
          {
            "gameId": "0042500302",
            "gameStatus": 1,
            "gameStatusText": "8:00 pm ET",
            "period": 0,
            "gameClock": "",
            "homeTeam": { "teamTricode": "DEN", "score": 0 },
            "awayTeam": { "teamTricode": "LAL", "score": 0 }
          },
          {
            "gameId": "0042500303",
            "gameStatus": 3,
            "gameStatusText": "Final",
            "period": 4,
            "gameClock": "PT00M00.00S",
            "homeTeam": { "teamTricode": "BOS", "score": 104 },
            "awayTeam": { "teamTricode": "NYK", "score": 98 }
          }
        ]
      }
    })";

    auto games = parse_scoreboard_response(body);
    ASSERT_EQ(games.size(), 3);

    // Live game
    EXPECT_EQ(games[0].game_id, "0042500301");
    EXPECT_EQ(games[0].game_status, 2);
    EXPECT_EQ(games[0].period, 3);
    EXPECT_EQ(games[0].game_clock_seconds, 323);
    EXPECT_EQ(games[0].home_tricode, "OKC");
    EXPECT_EQ(games[0].away_tricode, "SAS");
    EXPECT_EQ(games[0].home_score, 67);
    EXPECT_EQ(games[0].away_score, 58);
    EXPECT_EQ(games[0].home_lead(), 9);
    EXPECT_EQ(games[0].game_date_iso, "2026-05-18");
    EXPECT_TRUE(games[0].is_live());
    EXPECT_FALSE(games[0].is_final());
    // (4-3) * 720 + 323 = 720 + 323 = 1043
    EXPECT_EQ(games[0].regulation_seconds_remaining, 1043);

    // Scheduled (pre-game) — regulation_seconds_remaining stays 0 because status != 2
    EXPECT_EQ(games[1].game_status, 1);
    EXPECT_EQ(games[1].regulation_seconds_remaining, 0);

    // Final — same: 0 remaining
    EXPECT_EQ(games[2].game_status, 3);
    EXPECT_TRUE(games[2].is_final());
    EXPECT_EQ(games[2].regulation_seconds_remaining, 0);
}

TEST(NbaScoreboardParse, RegulationRemainingByPeriod) {
    // Period 1, 8:00 left → (4-1)*720 + 480 = 2160 + 480 = 2640
    // Period 4, 0:30 left → 30
    // Period 5 (OT)        → 0 (regulation already over)
    const std::string body = R"({
      "scoreboard": {
        "gameDate": "2026-05-18",
        "games": [
          { "gameId": "1", "gameStatus": 2, "period": 1, "gameClock": "PT08M00.00S",
            "homeTeam": {"teamTricode":"A","score":10}, "awayTeam":{"teamTricode":"B","score":10} },
          { "gameId": "2", "gameStatus": 2, "period": 4, "gameClock": "PT00M30.00S",
            "homeTeam": {"teamTricode":"C","score":90}, "awayTeam":{"teamTricode":"D","score":88} },
          { "gameId": "3", "gameStatus": 2, "period": 5, "gameClock": "PT04M30.00S",
            "homeTeam": {"teamTricode":"E","score":100}, "awayTeam":{"teamTricode":"F","score":100} }
        ]
      }
    })";
    auto games = parse_scoreboard_response(body);
    ASSERT_EQ(games.size(), 3);
    EXPECT_EQ(games[0].regulation_seconds_remaining, 2640);
    EXPECT_EQ(games[1].regulation_seconds_remaining, 30);
    EXPECT_EQ(games[2].regulation_seconds_remaining, 0);
    EXPECT_EQ(games[2].game_clock_seconds, 270);
    EXPECT_EQ(games[2].period, 5);  // overtime period preserved
}

TEST(NbaScoreboardParse, IgnoresInvalidGamesGracefully) {
    // First entry missing gameId, second valid, third missing tricodes.
    const std::string body = R"({
      "scoreboard": {
        "gameDate": "2026-05-18",
        "games": [
          { "gameStatus": 2, "period": 1, "gameClock": "PT08M00.00S",
            "homeTeam": {"teamTricode":"A","score":0}, "awayTeam":{"teamTricode":"B","score":0} },
          { "gameId": "0001", "gameStatus": 2, "period": 1, "gameClock": "PT05M00.00S",
            "homeTeam": {"teamTricode":"C","score":0}, "awayTeam":{"teamTricode":"D","score":0} },
          { "gameId": "0002", "gameStatus": 2, "period": 1, "gameClock": "PT05M00.00S",
            "homeTeam": {"teamTricode":"","score":0}, "awayTeam":{"teamTricode":"D","score":0} }
        ]
      }
    })";
    auto games = parse_scoreboard_response(body);
    EXPECT_EQ(games.size(), 1);
    EXPECT_EQ(games[0].game_id, "0001");
}

TEST(NbaScoreboardParse, ReturnsEmptyOnMalformedJson) {
    EXPECT_TRUE(parse_scoreboard_response("not json").empty());
    EXPECT_TRUE(parse_scoreboard_response("{}").empty());
    EXPECT_TRUE(parse_scoreboard_response(R"({"scoreboard":{}})").empty());
}
