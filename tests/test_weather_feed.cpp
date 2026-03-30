#include <gtest/gtest.h>
#include "strategy/kalshi/weather_feed.hpp"
#include <nlohmann/json.hpp>

using namespace trader::kalshi;

// ===== Station Data =====

TEST(WeatherFeed, KalshiStationsHaveSixEntries) {
    auto stations = kalshi_weather_stations();
    EXPECT_EQ(stations.size(), 6u);
}

TEST(WeatherFeed, StationDataCorrect) {
    auto stations = kalshi_weather_stations();
    // Find NYC
    auto it = std::find_if(stations.begin(), stations.end(),
                           [](const WeatherStation& s) { return s.id == "KNYC"; });
    ASSERT_NE(it, stations.end());
    EXPECT_EQ(it->city, "NYC");
    EXPECT_NEAR(it->lat, 40.7128, 0.01);
    EXPECT_NEAR(it->lon, -74.0060, 0.01);
    EXPECT_EQ(it->kalshi_ticker_prefix, "KXHIGHNY");
}

// ===== Open-Meteo Parsing =====

TEST(OpenMeteo, ExtractDailyHighs) {
    // 48 hours of data (2 days)
    std::vector<double> hourly(48);
    // Day 0: temps 60-80
    for (int h = 0; h < 24; ++h) {
        hourly[h] = 60.0 + (h < 14 ? h * 1.5 : (24 - h) * 1.5);  // Peak at hour 14
    }
    // Day 1: temps 55-75
    for (int h = 0; h < 24; ++h) {
        hourly[24 + h] = 55.0 + (h < 14 ? h * 1.5 : (24 - h) * 1.5);
    }

    auto day0_temps = OpenMeteoClient::extract_daily_highs(hourly, 0);
    ASSERT_EQ(day0_temps.size(), 24u);
    double max0 = *std::max_element(day0_temps.begin(), day0_temps.end());
    EXPECT_GT(max0, 75.0);

    auto day1_temps = OpenMeteoClient::extract_daily_highs(hourly, 1);
    ASSERT_EQ(day1_temps.size(), 24u);
    double max1 = *std::max_element(day1_temps.begin(), day1_temps.end());
    EXPECT_GT(max1, 70.0);
}

TEST(OpenMeteo, ExtractDailyHighsOutOfRange) {
    std::vector<double> hourly(24);  // Only 1 day
    auto result = OpenMeteoClient::extract_daily_highs(hourly, 5);  // Day 5 doesn't exist
    EXPECT_TRUE(result.empty());
}

TEST(OpenMeteo, ParseEnsembleResponse) {
    // Simulate a 2-day, 3-member ensemble response
    nlohmann::json j;
    j["hourly"]["time"] = nlohmann::json::array();
    j["hourly"]["temperature_2m_member00"] = nlohmann::json::array();
    j["hourly"]["temperature_2m_member01"] = nlohmann::json::array();
    j["hourly"]["temperature_2m_member02"] = nlohmann::json::array();

    // Generate 48 hours of data
    for (int h = 0; h < 48; ++h) {
        std::string date = (h < 24) ? "2026-04-26" : "2026-04-27";
        int hour = h % 24;
        j["hourly"]["time"].push_back(date + "T" + (hour < 10 ? "0" : "") + std::to_string(hour) + ":00");

        // Member 0: peaks at 78
        double base0 = 60.0 + (hour < 14 ? hour * 1.3 : (24 - hour) * 1.3);
        j["hourly"]["temperature_2m_member00"].push_back(base0);

        // Member 1: peaks at 82 (warmer)
        j["hourly"]["temperature_2m_member01"].push_back(base0 + 4.0);

        // Member 2: peaks at 74 (cooler)
        j["hourly"]["temperature_2m_member02"].push_back(base0 - 4.0);
    }

    auto forecasts = OpenMeteoClient::parse_response(j, "KNYC");
    ASSERT_GE(forecasts.size(), 2u);  // At least 2 days

    // Day 1 forecast
    EXPECT_EQ(forecasts[0].station_id, "KNYC");
    EXPECT_EQ(forecasts[0].target_date, "2026-04-26");
    EXPECT_EQ(forecasts[0].num_members, 3);
    ASSERT_EQ(forecasts[0].member_highs.size(), 3u);

    // Member highs should be the daily maximum per member
    // Member 0 peaks around hour 14: 60 + 14*1.3 = 78.2
    // Member 1 = member 0 + 4 = 82.2
    // Member 2 = member 0 - 4 = 74.2
    EXPECT_GT(forecasts[0].member_highs[0], 75.0);
    EXPECT_GT(forecasts[0].member_highs[1], 79.0);
    EXPECT_GT(forecasts[0].member_highs[2], 71.0);
}

TEST(OpenMeteo, ParseEmptyResponse) {
    nlohmann::json j = {};
    auto forecasts = OpenMeteoClient::parse_response(j, "KNYC");
    EXPECT_TRUE(forecasts.empty());
}

TEST(OpenMeteo, ParseResponseMissingHourly) {
    nlohmann::json j = {{"daily", {}}};
    auto forecasts = OpenMeteoClient::parse_response(j, "KNYC");
    EXPECT_TRUE(forecasts.empty());
}

// ===== NWS Parsing =====

TEST(NwsForecast, ParseForecastPeriods) {
    nlohmann::json j = {
        {"properties", {
            {"periods", {
                {
                    {"name", "Monday"},
                    {"isDaytime", true},
                    {"temperature", 75},
                    {"startTime", "2026-04-27T06:00:00-04:00"},
                    {"shortForecast", "Sunny, with a high near 75"},
                    {"probabilityOfPrecipitation", {{"value", 10}}}
                },
                {
                    {"name", "Monday Night"},
                    {"isDaytime", false},
                    {"temperature", 55},
                    {"startTime", "2026-04-27T18:00:00-04:00"},
                    {"shortForecast", "Clear"},
                    {"probabilityOfPrecipitation", {{"value", 5}}}
                },
                {
                    {"name", "Tuesday"},
                    {"isDaytime", true},
                    {"temperature", 80},
                    {"startTime", "2026-04-28T06:00:00-04:00"},
                    {"shortForecast", "Mostly sunny"},
                    {"probabilityOfPrecipitation", {{"value", nullptr}}}
                },
                {
                    {"name", "Tuesday Night"},
                    {"isDaytime", false},
                    {"temperature", 60},
                    {"startTime", "2026-04-28T18:00:00-04:00"},
                    {"shortForecast", "Partly cloudy"},
                    {"probabilityOfPrecipitation", {{"value", 20}}}
                }
            }}
        }}
    };

    auto forecasts = NwsClient::parse_forecast(j, "KNYC");
    ASSERT_EQ(forecasts.size(), 2u);

    EXPECT_EQ(forecasts[0].station_id, "KNYC");
    EXPECT_EQ(forecasts[0].date, "2026-04-27");
    EXPECT_DOUBLE_EQ(forecasts[0].high_f, 75.0);
    EXPECT_DOUBLE_EQ(forecasts[0].low_f, 55.0);
    EXPECT_EQ(forecasts[0].short_forecast, "Sunny, with a high near 75");
    EXPECT_NEAR(forecasts[0].precip_probability, 0.10, 0.01);

    EXPECT_EQ(forecasts[1].date, "2026-04-28");
    EXPECT_DOUBLE_EQ(forecasts[1].high_f, 80.0);
    EXPECT_DOUBLE_EQ(forecasts[1].low_f, 60.0);
}

TEST(NwsForecast, ParseEmptyResponse) {
    nlohmann::json j = {};
    auto forecasts = NwsClient::parse_forecast(j, "KNYC");
    EXPECT_TRUE(forecasts.empty());
}

// ===== WeatherFeed Cache =====

TEST(WeatherFeed, CacheEmptyInitially) {
    WeatherFeed feed;
    auto ensemble = feed.get_ensemble("KNYC");
    EXPECT_TRUE(ensemble.empty());

    auto nws = feed.get_nws_forecast("KNYC");
    EXPECT_TRUE(nws.empty());
}
