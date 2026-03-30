#include <gtest/gtest.h>
#include "strategy/kalshi/calibration.hpp"

using namespace trader::kalshi;

static CalibrationRecord make_record(const std::string& ticker, const std::string& cat,
                                       double model_prob, double market_price,
                                       const std::string& side = "yes") {
    CalibrationRecord rec;
    rec.market_ticker = ticker;
    rec.category = cat;
    rec.trade_time = std::chrono::system_clock::now();
    rec.model_probability = model_prob;
    rec.market_price = market_price;
    rec.edge = model_prob - market_price;
    rec.side = side;
    rec.quantity = 5;
    rec.entry_price = market_price;
    return rec;
}

TEST(Calibration, LogAndRetrieve) {
    CalibrationLogger logger;
    logger.log_trade(make_record("MKT1", "weather", 0.80, 0.65));
    EXPECT_EQ(logger.total_trades(), 1);
    EXPECT_EQ(logger.resolved_trades(), 0);
}

TEST(Calibration, ResolveUpdatesTrade) {
    CalibrationLogger logger;
    logger.log_trade(make_record("MKT1", "weather", 0.80, 0.65));
    logger.resolve("MKT1", true, 1.75);

    EXPECT_EQ(logger.resolved_trades(), 1);
    EXPECT_NEAR(logger.total_pnl(), 1.75, 0.01);
}

TEST(Calibration, BrierScorePerfect) {
    CalibrationLogger logger;

    // Perfect predictions: predicted 90%, outcome YES (5 times)
    for (int i = 0; i < 5; ++i) {
        auto rec = make_record("MKT" + std::to_string(i), "weather", 0.90, 0.70);
        logger.log_trade(rec);
        logger.resolve("MKT" + std::to_string(i), true, 1.50);
    }

    auto brier = logger.brier_score();
    // Brier = mean((0.90 - 1.0)^2) = 0.01
    EXPECT_NEAR(brier.score, 0.01, 0.001);
    EXPECT_EQ(brier.num_predictions, 5);
}

TEST(Calibration, BrierScoreRandom) {
    CalibrationLogger logger;

    // 50/50 predictions with 50/50 outcomes → Brier ≈ 0.25
    for (int i = 0; i < 100; ++i) {
        auto rec = make_record("MKT" + std::to_string(i), "weather", 0.50, 0.50);
        logger.log_trade(rec);
        logger.resolve("MKT" + std::to_string(i), (i % 2 == 0), 0.0);
    }

    auto brier = logger.brier_score();
    // Brier = mean((0.5 - 0 or 1)^2) = mean(0.25) = 0.25
    EXPECT_NEAR(brier.score, 0.25, 0.001);
}

TEST(Calibration, BrierScoreByCategory) {
    CalibrationLogger logger;

    // Weather: good predictions
    for (int i = 0; i < 5; ++i) {
        logger.log_trade(make_record("W" + std::to_string(i), "weather", 0.85, 0.70));
        logger.resolve("W" + std::to_string(i), true, 1.0);
    }

    // Economics: bad predictions
    for (int i = 0; i < 5; ++i) {
        logger.log_trade(make_record("E" + std::to_string(i), "economics", 0.85, 0.70));
        logger.resolve("E" + std::to_string(i), false, -0.70);
    }

    auto weather_brier = logger.brier_score("weather");
    auto econ_brier = logger.brier_score("economics");

    EXPECT_LT(weather_brier.score, econ_brier.score);
    EXPECT_NEAR(weather_brier.score, 0.0225, 0.001);  // (0.85-1)^2 = 0.0225
    EXPECT_NEAR(econ_brier.score, 0.7225, 0.001);     // (0.85-0)^2 = 0.7225
}

TEST(Calibration, CalibrationCurve) {
    CalibrationLogger logger;

    // Create predictions across different probability buckets
    // Bucket 70-80%: 10 predictions, 8 correct
    for (int i = 0; i < 10; ++i) {
        logger.log_trade(make_record("B7_" + std::to_string(i), "weather", 0.75, 0.60));
        logger.resolve("B7_" + std::to_string(i), (i < 8), i < 8 ? 0.40 : -0.60);
    }

    auto curve = logger.calibration_curve();
    ASSERT_EQ(curve.size(), 10u);

    // Bucket index 7 (0.70-0.80) should have our data
    auto& bucket = curve[7];
    EXPECT_EQ(bucket.sample_count, 10);
    EXPECT_NEAR(bucket.avg_prediction, 0.75, 0.01);
    EXPECT_NEAR(bucket.actual_frequency, 0.80, 0.01);
}

TEST(Calibration, CalibrationCurveEmpty) {
    CalibrationLogger logger;
    auto curve = logger.calibration_curve();
    EXPECT_EQ(curve.size(), 10u);
    for (const auto& b : curve) {
        EXPECT_EQ(b.sample_count, 0);
    }
}

TEST(Calibration, TotalPnlAggregates) {
    CalibrationLogger logger;
    logger.log_trade(make_record("MKT1", "weather", 0.80, 0.65));
    logger.log_trade(make_record("MKT2", "weather", 0.70, 0.55));
    logger.resolve("MKT1", true, 1.75);
    logger.resolve("MKT2", false, -2.75);

    EXPECT_NEAR(logger.total_pnl(), -1.0, 0.01);
}
