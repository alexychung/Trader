#include <gtest/gtest.h>
#include "strategy/kalshi/econ_feed.hpp"
#include <nlohmann/json.hpp>

using namespace trader::kalshi;

// ===== BLS Parsing =====

TEST(BlsClient, ParseCPIResponse) {
    nlohmann::json j = {
        {"Results", {
            {"series", {
                {
                    {"seriesID", "CUUR0000SA0"},
                    {"data", {
                        {{"year", "2026"}, {"period", "M03"}, {"value", "315.200"}, {"footnotes", nlohmann::json::array()}},
                        {{"year", "2026"}, {"period", "M02"}, {"value", "314.800"}, {"footnotes", nlohmann::json::array()}},
                        {{"year", "2026"}, {"period", "M01"}, {"value", "314.200"}, {"footnotes", nlohmann::json::array()}}
                    }}
                }
            }}
        }}
    };

    auto points = BlsClient::parse_response(j, "CUUR0000SA0");
    ASSERT_EQ(points.size(), 3u);
    // Reversed: oldest first
    EXPECT_EQ(points[0].period, "M01");
    EXPECT_DOUBLE_EQ(points[0].value, 314.200);
    EXPECT_EQ(points[2].period, "M03");
    EXPECT_DOUBLE_EQ(points[2].value, 315.200);
}

TEST(BlsClient, ParseResponseWithFootnotes) {
    nlohmann::json j = {
        {"Results", {
            {"series", {
                {
                    {"seriesID", "TEST"},
                    {"data", {
                        {{"year", "2026"}, {"period", "M01"}, {"value", "100.5"},
                         {"footnotes", {{{"text", "Preliminary"}}}}}
                    }}
                }
            }}
        }}
    };

    auto points = BlsClient::parse_response(j, "TEST");
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(points[0].footnotes, "Preliminary");
}

TEST(BlsClient, ParseEmptyResponse) {
    nlohmann::json j = {};
    auto points = BlsClient::parse_response(j, "CUUR0000SA0");
    EXPECT_TRUE(points.empty());
}

TEST(BlsClient, ComputeYoYChange) {
    // 13 months of data: 300.0, 301.0, ..., 312.0
    std::vector<BlsDataPoint> data;
    for (int i = 0; i < 13; ++i) {
        BlsDataPoint p;
        p.value = 300.0 + i;
        data.push_back(p);
    }

    auto yoy = BlsClient::compute_yoy_change(data);
    ASSERT_TRUE(yoy.has_value());
    // (312 - 300) / 300 * 100 = 4.0%
    EXPECT_NEAR(*yoy, 4.0, 0.01);
}

TEST(BlsClient, ComputeYoYNotEnoughData) {
    std::vector<BlsDataPoint> data(5);
    auto yoy = BlsClient::compute_yoy_change(data);
    EXPECT_FALSE(yoy.has_value());
}

TEST(BlsClient, ComputeMoMChange) {
    std::vector<BlsDataPoint> data = {{.value = 310.0}, {.value = 311.5}};
    auto mom = BlsClient::compute_mom_change(data);
    ASSERT_TRUE(mom.has_value());
    // (311.5 - 310.0) / 310.0 * 100 = 0.4839%
    EXPECT_NEAR(*mom, 0.4839, 0.01);
}

// ===== FRED Parsing =====

TEST(FredClient, ParseObservations) {
    nlohmann::json j = {
        {"observations", {
            {{"date", "2026-03-01"}, {"value", "4.33"}},
            {{"date", "2026-03-15"}, {"value", "4.33"}},
            {{"date", "2026-02-01"}, {"value", "4.50"}}
        }}
    };

    auto obs = FredClient::parse_response(j, "FEDFUNDS");
    ASSERT_EQ(obs.size(), 3u);
    EXPECT_EQ(obs[0].date, "2026-03-01");
    EXPECT_DOUBLE_EQ(obs[0].value, 4.33);
    EXPECT_EQ(obs[0].series_id, "FEDFUNDS");
}

TEST(FredClient, ParseMissingValues) {
    nlohmann::json j = {
        {"observations", {
            {{"date", "2026-03-01"}, {"value", "4.33"}},
            {{"date", "2026-03-02"}, {"value", "."}},  // FRED missing marker
            {{"date", "2026-03-03"}, {"value", "4.35"}}
        }}
    };

    auto obs = FredClient::parse_response(j, "TEST");
    ASSERT_EQ(obs.size(), 2u);  // Missing value skipped
    EXPECT_EQ(obs[0].date, "2026-03-01");
    EXPECT_EQ(obs[1].date, "2026-03-03");
}

TEST(FredClient, ParseEmptyResponse) {
    nlohmann::json j = {};
    auto obs = FredClient::parse_response(j, "FEDFUNDS");
    EXPECT_TRUE(obs.empty());
}

TEST(FredClient, LatestValue) {
    std::vector<FredObservation> data = {
        {.series_id="FF", .date="2026-01-01", .value=4.50},
        {.series_id="FF", .date="2026-02-01", .value=4.33},
        {.series_id="FF", .date="2026-03-01", .value=4.25}
    };

    auto latest = FredClient::latest_value(data);
    ASSERT_TRUE(latest.has_value());
    EXPECT_DOUBLE_EQ(*latest, 4.25);
}

TEST(FredClient, LatestValueEmpty) {
    auto latest = FredClient::latest_value({});
    EXPECT_FALSE(latest.has_value());
}

// ===== EconFeed DataSignals =====

TEST(EconFeed, ToSignalsEmpty) {
    EconFeed feed;
    auto signals = feed.to_signals();
    EXPECT_TRUE(signals.empty());
}
