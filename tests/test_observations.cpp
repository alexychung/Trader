// Tests for the settled-max observation gate.
//
// The network-facing fetch_and_compute() is covered only by the integration
// smoke test (trader_feeds_smoke) — it hits api.weather.gov. Here we test
// the pure parse_observations() function, which is what actually implements
// the "is the day's max locked?" logic given a sample NWS payload.

#include <gtest/gtest.h>
#include "strategy/kalshi/observations.hpp"
#include "strategy/kalshi/probability_engine.hpp"
#include "strategy/kalshi/calibration.hpp"
#include <nlohmann/json.hpp>

using namespace trader::kalshi;
using json = nlohmann::json;

namespace {

// Build a synthetic NWS-shaped observation payload.
// Each entry gives (minutes_ago, celsius). Timestamps are derived relative
// to a fixed "now" so tests are deterministic.
json make_obs_payload(const std::vector<std::pair<int, double>>& samples) {
    // Anchor "now" at an arbitrary fixed point so ISO formatting is
    // reproducible. The parser treats timestamps as UTC; only relative
    // ordering matters for the gate.
    const std::string base = "2026-04-21T22:00:00+00:00";
    json payload;
    payload["features"] = json::array();
    for (const auto& [minutes_ago, c] : samples) {
        // Subtract minutes_ago from base — simplest: precompute hh:mm strings.
        // Base is 22:00. Valid minutes_ago ranges here: 0..840 (14h).
        int total_minutes = 22 * 60 - minutes_ago;
        int hh = (total_minutes / 60 + 24) % 24;
        int mm = (total_minutes % 60 + 60) % 60;
        char ts[32];
        std::snprintf(ts, sizeof(ts), "2026-04-21T%02d:%02d:00+00:00", hh, mm);

        json feat;
        feat["properties"]["timestamp"] = ts;
        feat["properties"]["temperature"]["value"] = c;
        feat["properties"]["temperature"]["unitCode"] = "wmoUnit:degC";
        payload["features"].push_back(feat);
    }
    return payload;
}

} // namespace

TEST(Observations, EmptyPayloadReturnsNullopt) {
    auto out = StationObservationsClient::parse_observations(
        "KNYC", json{{"features", json::array()}}, 180, 4.0, 30);
    EXPECT_FALSE(out.has_value());
}

TEST(Observations, TooFewSamplesReturnsNullopt) {
    auto payload = make_obs_payload({{60, 20.0}, {30, 21.0}, {0, 21.5}});
    auto out = StationObservationsClient::parse_observations(
        "KNYC", payload, 180, 4.0, 30);
    EXPECT_FALSE(out.has_value());
}

TEST(Observations, NotLockedWhenMaxIsRecent) {
    // 40 samples, max at minute-0 (now). Latest == max. Neither gate passes.
    std::vector<std::pair<int, double>> s;
    for (int i = 40; i > 0; --i) s.push_back({i, 20.0});
    s.push_back({0, 25.0});                    // current = max
    auto out = StationObservationsClient::parse_observations(
        "KNYC", make_obs_payload(s), 180, 4.0, 30);
    ASSERT_TRUE(out.has_value());
    EXPECT_FALSE(out->locked) << out->reason;
}

TEST(Observations, NotLockedWhenCooldownInsufficient) {
    // Max 4 hours ago, but current temp only 1°C below max — fails cooldown.
    std::vector<std::pair<int, double>> s;
    for (int i = 480; i > 240; --i) s.push_back({i, 15.0});
    s.push_back({240, 25.0});                  // max 4h ago
    for (int i = 239; i >= 0; --i) s.push_back({i, 24.0});  // cooled only 1°C
    auto out = StationObservationsClient::parse_observations(
        "KNYC", make_obs_payload(s), 180, 4.0, 30);
    ASSERT_TRUE(out.has_value());
    EXPECT_FALSE(out->locked) << out->reason;
}

TEST(Observations, LockedWhenBothGatesPass) {
    // Max 4h ago, current temp 6°C below max. Enough samples.
    std::vector<std::pair<int, double>> s;
    for (int i = 480; i > 240; --i) s.push_back({i, 15.0});
    s.push_back({240, 28.0});                  // max 4h ago
    for (int i = 200; i >= 0; --i) s.push_back({i, 22.0});  // cooled 6°C
    auto out = StationObservationsClient::parse_observations(
        "KNYC", make_obs_payload(s), 180, 4.0, 30);
    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(out->locked) << out->reason;
    // 28C ≈ 82.4F — confirm the unit conversion held.
    EXPECT_NEAR(out->max_f, 28.0 * 9.0 / 5.0 + 32.0, 0.01);
}

TEST(Observations, NullTemperatureSkipped) {
    // Mix of valid and null-temperature observations; null ones must be
    // dropped silently (NWS sometimes reports sensor outages as null).
    auto payload = make_obs_payload({{60, 20.0}, {0, 21.0}});
    json null_feat;
    null_feat["properties"]["timestamp"] = "2026-04-21T20:30:00+00:00";
    null_feat["properties"]["temperature"]["value"] = nullptr;
    null_feat["properties"]["temperature"]["unitCode"] = "wmoUnit:degC";
    payload["features"].push_back(null_feat);
    // Only 2 real samples — below the 30 minimum, should still return nullopt
    // (the null entry shouldn't bump the count).
    auto out = StationObservationsClient::parse_observations(
        "KNYC", payload, 180, 4.0, 30);
    EXPECT_FALSE(out.has_value());
}

// ===== Integration with WeatherEnsembleModel =====

TEST(WeatherEnsembleModel, SettledMaxOverridesBelowThreshold) {
    // Observed max 78°F, locked. Threshold 72°F is clearly below — model
    // should return ~0.99 (YES) regardless of any ensemble data.
    StationObservationsTracker tracker;
    StationMaxObservation obs;
    obs.station_id = "KNYC";
    obs.max_f = 78.0;
    obs.latest_f = 70.0;
    obs.max_time_utc = std::chrono::system_clock::now() - std::chrono::hours(4);
    obs.latest_time_utc = std::chrono::system_clock::now() - std::chrono::minutes(10);
    obs.locked = true;
    obs.reason = "test: locked";
    tracker.set(obs);

    WeatherEnsembleModel model;
    model.set_observations_tracker(&tracker);

    auto p = model.estimate("KXHIGHNY-26APR21-T72", 72.0);
    EXPECT_NEAR(p, 0.99, 0.001);
    EXPECT_GT(model.confidence(), 0.9);
}

TEST(WeatherEnsembleModel, SettledMaxOverridesAboveThreshold) {
    // Observed max 78°F, locked. Threshold 82°F is clearly above —
    // should return ~0.01 (NO).
    StationObservationsTracker tracker;
    StationMaxObservation obs;
    obs.station_id = "KNYC";
    obs.max_f = 78.0;
    obs.latest_f = 70.0;
    obs.max_time_utc = std::chrono::system_clock::now() - std::chrono::hours(4);
    obs.latest_time_utc = std::chrono::system_clock::now() - std::chrono::minutes(10);
    obs.locked = true;
    tracker.set(obs);

    WeatherEnsembleModel model;
    model.set_observations_tracker(&tracker);

    auto p = model.estimate("KXHIGHNY-26APR21-T82", 82.0);
    EXPECT_NEAR(p, 0.01, 0.001);
    EXPECT_GT(model.confidence(), 0.9);
}

TEST(WeatherEnsembleModel, DefersWhenThresholdNearMax) {
    // Threshold 77°F, observed max 78°F — gap of 1°F is within the 2°F
    // margin. Model should NOT override; falls back to ensemble (which
    // returns 0.5 with zero confidence when no ensemble is set).
    StationObservationsTracker tracker;
    StationMaxObservation obs;
    obs.station_id = "KNYC";
    obs.max_f = 78.0;
    obs.latest_f = 70.0;
    obs.max_time_utc = std::chrono::system_clock::now() - std::chrono::hours(4);
    obs.latest_time_utc = std::chrono::system_clock::now() - std::chrono::minutes(10);
    obs.locked = true;
    tracker.set(obs);

    WeatherEnsembleModel model;
    model.set_observations_tracker(&tracker);

    auto p = model.estimate("KXHIGHNY-26APR21-T77", 77.0);
    EXPECT_NEAR(p, 0.5, 0.001);   // ensemble-not-set fallback
    EXPECT_EQ(model.confidence(), 0.0);
}

TEST(WeatherEnsembleModel, DefersWhenObservationNotLocked) {
    // Locked = false → no override even with a clear gap.
    StationObservationsTracker tracker;
    StationMaxObservation obs;
    obs.station_id = "KNYC";
    obs.max_f = 78.0;
    obs.latest_f = 77.0;
    obs.max_time_utc = std::chrono::system_clock::now() - std::chrono::minutes(30);
    obs.latest_time_utc = std::chrono::system_clock::now() - std::chrono::minutes(5);
    obs.locked = false;
    tracker.set(obs);

    WeatherEnsembleModel model;
    model.set_observations_tracker(&tracker);

    auto p = model.estimate("KXHIGHNY-26APR21-T72", 72.0);
    EXPECT_NEAR(p, 0.5, 0.001);
    EXPECT_EQ(model.confidence(), 0.0);
}

TEST(WeatherEnsembleModel, DefersWhenObservationStale) {
    // Locked but obs is 5 hours old — don't trust it for today's contract.
    StationObservationsTracker tracker;
    StationMaxObservation obs;
    obs.station_id = "KNYC";
    obs.max_f = 78.0;
    obs.latest_f = 70.0;
    obs.max_time_utc = std::chrono::system_clock::now() - std::chrono::hours(10);
    obs.latest_time_utc = std::chrono::system_clock::now() - std::chrono::hours(5);
    obs.locked = true;
    tracker.set(obs);

    WeatherEnsembleModel model;
    model.set_observations_tracker(&tracker);

    auto p = model.estimate("KXHIGHNY-26APR21-T72", 72.0);
    EXPECT_NEAR(p, 0.5, 0.001);
    EXPECT_EQ(model.confidence(), 0.0);
}

TEST(WeatherEnsembleModel, WithoutTrackerBehavesUnchanged) {
    // No tracker plugged in — old behavior preserved (neutral default).
    WeatherEnsembleModel model;
    auto p = model.estimate("KXHIGHNY-26APR21-T72", 72.0);
    EXPECT_NEAR(p, 0.5, 0.001);
    EXPECT_EQ(model.confidence(), 0.0);
}

// ===== last_source() tagging =====

TEST(WeatherEnsembleModel, LastSourceIsEnsembleOnNormalPath) {
    WeatherEnsembleModel model;
    model.estimate("KXHIGHNY-26APR21-T72", 72.0);
    EXPECT_EQ(model.last_source(), "ensemble");
}

TEST(WeatherEnsembleModel, LastSourceIsSettledMaxOnOverride) {
    StationObservationsTracker tracker;
    StationMaxObservation obs;
    obs.station_id = "KNYC";
    obs.max_f = 78.0;
    obs.latest_f = 70.0;
    obs.max_time_utc = std::chrono::system_clock::now() - std::chrono::hours(4);
    obs.latest_time_utc = std::chrono::system_clock::now() - std::chrono::minutes(10);
    obs.locked = true;
    tracker.set(obs);

    WeatherEnsembleModel model;
    model.set_observations_tracker(&tracker);
    model.estimate("KXHIGHNY-26APR21-T72", 72.0);
    EXPECT_EQ(model.last_source(), "settled_max");
}

TEST(WeatherEnsembleModel, LastSourceFallsBackToEnsembleWhenOverrideDefers) {
    // Locked observation but threshold too close to max — model defers to
    // ensemble and should tag the source as "ensemble".
    StationObservationsTracker tracker;
    StationMaxObservation obs;
    obs.station_id = "KNYC";
    obs.max_f = 78.0;
    obs.latest_f = 70.0;
    obs.max_time_utc = std::chrono::system_clock::now() - std::chrono::hours(4);
    obs.latest_time_utc = std::chrono::system_clock::now() - std::chrono::minutes(10);
    obs.locked = true;
    tracker.set(obs);

    WeatherEnsembleModel model;
    model.set_observations_tracker(&tracker);
    model.estimate("KXHIGHNY-26APR21-T77", 77.0);  // gap only 1°F
    EXPECT_EQ(model.last_source(), "ensemble");
}

TEST(WeatherEnsembleModel, LastSourceIsEmptyOnInvalidTicker) {
    WeatherEnsembleModel model;
    model.estimate("not-a-weather-ticker", 0.0);
    EXPECT_EQ(model.last_source(), "");
}

// ===== CalibrationLogger::brier_score_by_source =====

TEST(Calibration, BrierScoreBySourcePartitionsCorrectly) {
    CalibrationLogger logger;

    auto make = [](const std::string& t, const std::string& src,
                   double prob, bool outcome) {
        CalibrationRecord r;
        r.market_ticker = t;
        r.category = "weather";
        r.trade_time = std::chrono::system_clock::now();
        r.model_probability = prob;
        r.market_price = 0.5;
        r.edge = prob - 0.5;
        r.side = "yes";
        r.quantity = 1;
        r.entry_price = 0.5;
        r.model_source = src;
        return r;
    };

    // 5 settled_max predictions at 0.99, all correct → Brier ~ 0.0001
    for (int i = 0; i < 5; ++i) {
        logger.log_trade(make("SM" + std::to_string(i), "settled_max", 0.99, true));
        logger.resolve("SM" + std::to_string(i), true, 0.01);
    }
    // 5 ensemble predictions at 0.70, 3 correct → Brier worse
    for (int i = 0; i < 5; ++i) {
        logger.log_trade(make("EN" + std::to_string(i), "ensemble", 0.70, true));
        logger.resolve("EN" + std::to_string(i), i < 3, 0.0);
    }

    auto sm = logger.brier_score_by_source("settled_max", "weather");
    auto en = logger.brier_score_by_source("ensemble", "weather");

    EXPECT_EQ(sm.num_predictions, 5);
    EXPECT_EQ(en.num_predictions, 5);
    EXPECT_NEAR(sm.score, 0.0001, 0.0005);      // (0.99 - 1)^2 = 0.0001
    // Ensemble: 3×(0.70-1)² + 2×(0.70-0)² = 3×0.09 + 2×0.49 = 1.25 / 5 = 0.25
    EXPECT_NEAR(en.score, 0.25, 0.001);
    // Overall category Brier averages the two populations.
    auto all = logger.brier_score("weather");
    EXPECT_EQ(all.num_predictions, 10);
}

TEST(Calibration, BrierScoreBySourceHandlesUntaggedRecords) {
    // Records with empty model_source (pre-upgrade data) should be matched
    // by an empty-source query and ignored by a named-source query.
    CalibrationLogger logger;
    CalibrationRecord r;
    r.market_ticker = "MKT1";
    r.category = "weather";
    r.trade_time = std::chrono::system_clock::now();
    r.model_probability = 0.7;
    r.market_price = 0.5;
    r.edge = 0.2;
    r.side = "yes";
    r.quantity = 1;
    r.entry_price = 0.5;
    // model_source intentionally left empty
    logger.log_trade(r);
    logger.resolve("MKT1", true, 0.5);

    auto empty = logger.brier_score_by_source("", "weather");
    auto named = logger.brier_score_by_source("ensemble", "weather");
    EXPECT_EQ(empty.num_predictions, 1);
    EXPECT_EQ(named.num_predictions, 0);
}
