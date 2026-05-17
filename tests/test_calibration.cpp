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

TEST(Calibration, ResolveSplitsPnlAcrossMultipleTradesOnSameTicker) {
    // Two real trades on the same ticker: first at entry 0.50 * 4 contracts = 2.0 notional,
    // second at entry 0.60 * 6 contracts = 3.6 notional. Total notional = 5.6.
    // A settlement PnL of $5.60 (won both fills) should split proportionally:
    //   rec1 = 5.60 * (2.0 / 5.6)  = $2.00
    //   rec2 = 5.60 * (3.6 / 5.6)  = $3.60
    // The bug was assigning the full $5.60 to both records → total_pnl = $11.20.
    CalibrationLogger logger;
    auto r1 = make_record("MKT1", "weather", 0.70, 0.50);
    r1.quantity = 4;
    r1.entry_price = 0.50;
    logger.log_trade(r1);

    auto r2 = make_record("MKT1", "weather", 0.72, 0.60);
    r2.quantity = 6;
    r2.entry_price = 0.60;
    logger.log_trade(r2);

    logger.resolve("MKT1", true, 5.60);

    EXPECT_EQ(logger.resolved_trades(), 2);
    EXPECT_NEAR(logger.total_pnl(), 5.60, 0.01);
}

TEST(Calibration, ResolveDoesNotDoubleCountWhenShadowAndRealShareTicker) {
    // One real trade + one shadow on the same ticker. Shadow must not
    // absorb any pnl; real record gets the full settlement amount.
    CalibrationLogger logger;
    auto real = make_record("MKT1", "weather", 0.70, 0.50);
    real.quantity = 10;
    real.entry_price = 0.50;
    logger.log_trade(real);

    auto shadow = make_record("MKT1", "weather", 0.80, 0.50);
    shadow.quantity = 1;
    logger.log_shadow_prediction(shadow);

    logger.resolve("MKT1", true, 5.00);

    EXPECT_NEAR(logger.total_pnl(), 5.00, 0.01);
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

// ===== Rolling Brier / drift detection =====

TEST(Calibration, RollingBrierEmptyReturnsZeroSamples) {
    CalibrationLogger logger;
    auto r = logger.rolling_brier_score("weather", 30);
    EXPECT_EQ(r.num_predictions, 0);
}

TEST(Calibration, RollingBrierMatchesAllTimeWhenWindowCoversAll) {
    CalibrationLogger logger;
    // 5 calibrated predictions — model=0.70, side="yes", 3 yes, 2 no.
    // per-record error² = (0.70-1)² = 0.09 on wins, (0.70-0)² = 0.49 on losses.
    for (int i = 0; i < 5; ++i) {
        auto r = make_record("M" + std::to_string(i), "weather", 0.70, 0.50);
        logger.log_trade(r);
    }
    logger.resolve("M0", true, 0);
    logger.resolve("M1", true, 0);
    logger.resolve("M2", true, 0);
    logger.resolve("M3", false, 0);
    logger.resolve("M4", false, 0);

    auto all = logger.brier_score("weather");
    auto rolling = logger.rolling_brier_score("weather", 30);
    EXPECT_EQ(rolling.num_predictions, all.num_predictions);
    EXPECT_NEAR(rolling.score, all.score, 1e-9);
}

TEST(Calibration, RollingBrierCapturesRecentDegradation) {
    // 10 well-calibrated (model=0.70, 7 wins), then 10 badly-calibrated (model=0.90, all losses).
    // All-time Brier is diluted; rolling over the last 10 is much worse.
    CalibrationLogger logger;
    for (int i = 0; i < 10; ++i) {
        logger.log_trade(make_record("GOOD" + std::to_string(i), "weather", 0.70, 0.50));
        logger.resolve("GOOD" + std::to_string(i), i < 7, 0);
    }
    for (int i = 0; i < 10; ++i) {
        logger.log_trade(make_record("BAD" + std::to_string(i), "weather", 0.90, 0.50));
        logger.resolve("BAD" + std::to_string(i), false, 0);
    }

    auto all = logger.brier_score("weather");
    auto rolling = logger.rolling_brier_score("weather", 10);

    EXPECT_EQ(rolling.num_predictions, 10);
    EXPECT_NEAR(rolling.score, 0.81, 1e-6);          // (0.9-0)² = 0.81 over all 10 recent losses
    EXPECT_GT(rolling.score, all.score + 0.2);       // rolling much worse than all-time
}

TEST(Calibration, CategoryAggregateIncludesRollingWindow) {
    CalibrationLogger logger;
    for (int i = 0; i < 35; ++i) {
        logger.log_trade(make_record("M" + std::to_string(i), "weather", 0.60, 0.50));
        logger.resolve("M" + std::to_string(i), i % 2 == 0, 0);
    }
    auto agg = logger.category_aggregate("weather", 30);
    EXPECT_EQ(agg.rolling_window, 30);
    EXPECT_GT(agg.rolling_brier, 0.0);
    EXPECT_LT(agg.rolling_brier, 0.5);
}

TEST(Calibration, RollingBrierRespectsCategoryFilter) {
    CalibrationLogger logger;
    for (int i = 0; i < 5; ++i) {
        logger.log_trade(make_record("W" + std::to_string(i), "weather", 0.70, 0.50));
        logger.resolve("W" + std::to_string(i), true, 0);   // weather all won
    }
    for (int i = 0; i < 5; ++i) {
        logger.log_trade(make_record("C" + std::to_string(i), "cpi", 0.70, 0.50));
        logger.resolve("C" + std::to_string(i), false, 0);  // cpi all lost
    }
    auto w = logger.rolling_brier_score("weather", 10);
    auto c = logger.rolling_brier_score("cpi", 10);
    EXPECT_NEAR(w.score, 0.09, 1e-6);  // (0.7-1)²
    EXPECT_NEAR(c.score, 0.49, 1e-6);  // (0.7-0)²
    EXPECT_EQ(w.num_predictions, 5);
    EXPECT_EQ(c.num_predictions, 5);
}
