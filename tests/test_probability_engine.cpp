#include <gtest/gtest.h>
#include "strategy/kalshi/probability_engine.hpp"

using namespace trader;
using namespace trader::kalshi;

// ===== Raw Ensemble Probability =====

TEST(WeatherModel, RawProbabilityAllAbove) {
    std::vector<double> highs = {80, 82, 78, 81, 79};
    double prob = WeatherEnsembleModel::raw_ensemble_probability(highs, 75.0);
    EXPECT_DOUBLE_EQ(prob, 1.0);  // All 5 above 75
}

TEST(WeatherModel, RawProbabilityAllBelow) {
    std::vector<double> highs = {70, 72, 68, 71, 69};
    double prob = WeatherEnsembleModel::raw_ensemble_probability(highs, 75.0);
    EXPECT_DOUBLE_EQ(prob, 0.0);  // None above 75
}

TEST(WeatherModel, RawProbabilityMixed) {
    std::vector<double> highs = {73, 76, 74, 78, 72, 77, 75, 71, 79, 74};
    // Above 75: 76, 78, 77, 79 = 4 out of 10
    double prob = WeatherEnsembleModel::raw_ensemble_probability(highs, 75.0);
    EXPECT_DOUBLE_EQ(prob, 0.4);
}

TEST(WeatherModel, RawProbabilityEmpty) {
    double prob = WeatherEnsembleModel::raw_ensemble_probability({}, 75.0);
    EXPECT_DOUBLE_EQ(prob, 0.5);  // Default when no data
}

// ===== Bias Correction =====

TEST(WeatherModel, BiasCorrectionPositive) {
    // Model runs warm by 2°F → subtract 2 from all members
    std::vector<double> highs = {80, 82, 78};
    auto corrected = WeatherEnsembleModel::apply_bias_correction(highs, 2.0);

    ASSERT_EQ(corrected.size(), 3u);
    EXPECT_DOUBLE_EQ(corrected[0], 78.0);
    EXPECT_DOUBLE_EQ(corrected[1], 80.0);
    EXPECT_DOUBLE_EQ(corrected[2], 76.0);
}

TEST(WeatherModel, BiasCorrectionNegative) {
    // Model runs cold by 1°F → add 1 to all members
    std::vector<double> highs = {72, 74, 70};
    auto corrected = WeatherEnsembleModel::apply_bias_correction(highs, -1.0);

    EXPECT_DOUBLE_EQ(corrected[0], 73.0);
    EXPECT_DOUBLE_EQ(corrected[1], 75.0);
    EXPECT_DOUBLE_EQ(corrected[2], 71.0);
}

TEST(WeatherModel, BiasAffectsProbability) {
    // Without bias: 3 of 5 above 75 → 60%
    std::vector<double> highs = {76, 77, 78, 74, 73};
    double prob_no_bias = WeatherEnsembleModel::raw_ensemble_probability(highs, 75.0);
    EXPECT_DOUBLE_EQ(prob_no_bias, 0.6);

    // With +2°F warm bias correction → subtract 2: {74, 75, 76, 72, 71}
    // Only 76 is above 75 → 20%
    auto corrected = WeatherEnsembleModel::apply_bias_correction(highs, 2.0);
    double prob_with_bias = WeatherEnsembleModel::raw_ensemble_probability(corrected, 75.0);
    EXPECT_DOUBLE_EQ(prob_with_bias, 0.2);
}

// ===== Horizon Sigma =====

TEST(WeatherModel, HorizonSigmaIncreases) {
    double sigma_1 = WeatherEnsembleModel::horizon_sigma(1);
    double sigma_3 = WeatherEnsembleModel::horizon_sigma(3);
    double sigma_7 = WeatherEnsembleModel::horizon_sigma(7);

    EXPECT_DOUBLE_EQ(sigma_1, 2.0);
    EXPECT_DOUBLE_EQ(sigma_3, 4.0);
    EXPECT_DOUBLE_EQ(sigma_7, 6.0);

    EXPECT_LT(sigma_1, sigma_3);
    EXPECT_LT(sigma_3, sigma_7);
}

// ===== Model Blending =====

TEST(WeatherModel, BlendModels) {
    // GFS says 70%, ECMWF says 80%
    // Blend: 0.4*0.7 + 0.6*0.8 = 0.28 + 0.48 = 0.76
    double blended = WeatherEnsembleModel::blend_models(0.7, 0.8);
    EXPECT_NEAR(blended, 0.76, 0.001);
}

TEST(WeatherModel, BlendEqualWeights) {
    double blended = WeatherEnsembleModel::blend_models(0.6, 0.8, 0.5, 0.5);
    EXPECT_NEAR(blended, 0.70, 0.001);
}

// ===== Ticker Parsing =====

TEST(WeatherModel, ParseWeatherTickerNYC) {
    auto info = WeatherEnsembleModel::parse_weather_ticker("KXHIGHNY-26APR-T75");
    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.station_id, "KNYC");
    EXPECT_DOUBLE_EQ(info.threshold, 75.0);
    EXPECT_EQ(info.date, "26APR");
}

TEST(WeatherModel, ParseWeatherTickerChicago) {
    auto info = WeatherEnsembleModel::parse_weather_ticker("KXHIGHCHI-26APR-T60");
    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.station_id, "KORD");
    EXPECT_DOUBLE_EQ(info.threshold, 60.0);
}

TEST(WeatherModel, ParseWeatherTickerMiami) {
    auto info = WeatherEnsembleModel::parse_weather_ticker("KXHIGHMIA-15JUN-T90");
    EXPECT_TRUE(info.valid);
    EXPECT_EQ(info.station_id, "KMIA");
    EXPECT_DOUBLE_EQ(info.threshold, 90.0);
}

TEST(WeatherModel, ParseWeatherTickerInvalid) {
    auto info = WeatherEnsembleModel::parse_weather_ticker("CPI-26MAR-T3.5");
    EXPECT_FALSE(info.valid);
}

// ===== Full Estimate Pipeline =====

TEST(WeatherModel, EstimateWithEnsembleData) {
    auto model = std::make_shared<WeatherEnsembleModel>();

    // Create ensemble with 10 members
    EnsembleForecast ef;
    ef.station_id = "KNYC";
    ef.model = "gfs";
    ef.target_date = "2026-04-26";
    ef.num_members = 10;
    ef.member_highs = {73, 76, 74, 78, 72, 77, 75, 71, 79, 74};
    // Above 75: 76, 78, 77, 79 = 4/10 = 40%

    model->set_ensemble(ef);

    double prob = model->estimate("KXHIGHNY-26APR-T75", 75.0);
    EXPECT_NEAR(prob, 0.4, 0.01);
    EXPECT_GT(model->confidence(), 0.0);
    EXPECT_FALSE(model->rationale().empty());
}

TEST(WeatherModel, EstimateWithBiasCorrection) {
    auto model = std::make_shared<WeatherEnsembleModel>();

    EnsembleForecast ef;
    ef.station_id = "KNYC";
    ef.model = "gfs";
    ef.target_date = "2026-04-26";
    ef.num_members = 5;
    ef.member_highs = {76, 77, 78, 74, 73};
    // Without bias: 3/5 above 75 = 60%

    model->set_ensemble(ef);
    model->set_bias_correction("KNYC", 2.0);  // Model runs 2°F warm

    double prob = model->estimate("KXHIGHNY-26APR-T75", 75.0);
    // After correction: {74, 75, 76, 72, 71} → 1/5 above 75 = 20%
    EXPECT_NEAR(prob, 0.2, 0.01);
}

TEST(WeatherModel, EstimateNoDataReturnsHalf) {
    auto model = std::make_shared<WeatherEnsembleModel>();
    double prob = model->estimate("KXHIGHNY-26APR-T75", 75.0);
    EXPECT_DOUBLE_EQ(prob, 0.5);
    EXPECT_DOUBLE_EQ(model->confidence(), 0.0);
}

// ===== ProbabilityEngine =====

TEST(ProbabilityEngine, RegisterAndDispatch) {
    ProbabilityEngine engine;
    auto weather_model = std::make_shared<WeatherEnsembleModel>();

    EnsembleForecast ef;
    ef.station_id = "KNYC";
    ef.target_date = "2026-04-26";
    ef.num_members = 10;
    ef.member_highs = {80, 82, 78, 81, 79, 83, 77, 84, 76, 85};
    weather_model->set_ensemble(ef);

    engine.register_model("weather", weather_model);

    // All 10 members above 75 → probability = 1.0
    double prob = engine.estimate("KXHIGHNY-26APR-T75", "weather", 75.0);
    EXPECT_DOUBLE_EQ(prob, 1.0);
}

TEST(ProbabilityEngine, UnknownCategoryReturnsHalf) {
    ProbabilityEngine engine;
    double prob = engine.estimate("SOME-TICKER", "unknown", 0.0);
    EXPECT_DOUBLE_EQ(prob, 0.5);
}

TEST(ProbabilityEngine, ListCategories) {
    ProbabilityEngine engine;
    engine.register_model("weather", std::make_shared<WeatherEnsembleModel>());
    engine.register_model("economics", std::make_shared<WeatherEnsembleModel>());  // placeholder

    auto cats = engine.categories();
    EXPECT_EQ(cats.size(), 2u);
}
