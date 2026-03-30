#include <gtest/gtest.h>
#include "strategy/kalshi/econ_models.hpp"

using namespace trader::kalshi;

// ===== CPI Model =====

TEST(CpiModel, NormalCdfBasicValues) {
    EXPECT_NEAR(CpiModel::normal_cdf(0.0), 0.5, 0.001);
    EXPECT_NEAR(CpiModel::normal_cdf(1.0), 0.8413, 0.001);
    EXPECT_NEAR(CpiModel::normal_cdf(-1.0), 0.1587, 0.001);
    EXPECT_NEAR(CpiModel::normal_cdf(2.0), 0.9772, 0.001);
    EXPECT_NEAR(CpiModel::normal_cdf(-2.0), 0.0228, 0.001);
}

TEST(CpiModel, EstimateCpiYoY) {
    CpiModel model;
    model.set_nowcast(3.2);      // Cleveland Fed says 3.2% YoY
    model.set_gasoline_change_pct(5.0);  // Gas up 5% since nowcast

    // Estimate = 3.2 + 0.07 * 5.0 = 3.55%
    double estimate = model.estimate_cpi_yoy();
    EXPECT_NEAR(estimate, 3.55, 0.01);
}

TEST(CpiModel, ProbabilityAboveThreshold) {
    CpiModel model;
    model.set_nowcast(3.2);

    // P(CPI > 3.0%) should be high (estimate = 3.2, threshold = 3.0)
    double prob = model.estimate("CPI-26MAR-T3.0", 3.0);
    EXPECT_GT(prob, 0.5);

    // P(CPI > 4.0%) should be low
    prob = model.estimate("CPI-26MAR-T4.0", 4.0);
    EXPECT_LT(prob, 0.3);
}

TEST(CpiModel, NoDataReturnsHalf) {
    CpiModel model;
    double prob = model.estimate("CPI-26MAR-T3.0", 3.0);
    EXPECT_DOUBLE_EQ(prob, 0.5);
    EXPECT_DOUBLE_EQ(model.confidence(), 0.0);
}

// ===== NFP Model =====

TEST(NfpModel, EstimateNfp) {
    NfpModel model;
    model.set_adp(180.0);      // ADP says 180K
    model.set_claims_trend(-2.0);  // Claims falling (good)
    model.set_prior_nfp(200.0);

    // Estimate = 50 + 0.6*180 + -2*(-2) + 0.2*200 = 50 + 108 + 4 + 40 = 202K
    double estimate = model.estimate_nfp();
    EXPECT_NEAR(estimate, 202.0, 1.0);
}

TEST(NfpModel, ProbabilityAboveThreshold) {
    NfpModel model;
    model.set_adp(180.0);
    model.set_prior_nfp(200.0);

    // P(NFP > 150K) should be high
    double prob = model.estimate("NFP-26APR-T150", 150.0);
    EXPECT_GT(prob, 0.6);

    // P(NFP > 300K) should be low
    prob = model.estimate("NFP-26APR-T300", 300.0);
    EXPECT_LT(prob, 0.3);
}

// ===== Fed Rate Model =====

TEST(FedRateModel, UsesFeWatchProbability) {
    FedRateModel model;
    model.set_fedwatch_probability(0.90);  // 90% chance of hold

    double prob = model.estimate("FED-26MAY-HOLD", 0.0);
    EXPECT_NEAR(prob, 0.90, 0.01);
    EXPECT_GT(model.confidence(), 0.5);
}

// ===== Consistency Checker =====

TEST(ConsistencyChecker, DetectsMonotonicityViolation) {
    // Markets: P(temp > 70) should be >= P(temp > 75) should be >= P(temp > 80)
    // But prices show violation: temp>75 costs MORE than temp>70
    std::vector<std::pair<std::string, double>> markets = {
        {"MKT-T70", 0.80},  // P(>70) = 80%
        {"MKT-T75", 0.85},  // P(>75) = 85% — VIOLATION! Should be <= 80%
        {"MKT-T80", 0.60},  // P(>80) = 60% — OK relative to T75
    };

    auto violations = ConsistencyChecker::check_monotonicity(markets);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0].market_higher, "MKT-T70");
    EXPECT_EQ(violations[0].market_lower, "MKT-T75");
    EXPECT_NEAR(violations[0].violation, 0.05, 0.001);  // 0.85 - 0.80
}

TEST(ConsistencyChecker, NoViolationWhenMonotonic) {
    std::vector<std::pair<std::string, double>> markets = {
        {"MKT-T70", 0.90},
        {"MKT-T75", 0.75},
        {"MKT-T80", 0.50},
    };

    auto violations = ConsistencyChecker::check_monotonicity(markets);
    EXPECT_TRUE(violations.empty());
}

TEST(ConsistencyChecker, MultipleViolations) {
    std::vector<std::pair<std::string, double>> markets = {
        {"MKT-T60", 0.70},
        {"MKT-T65", 0.75},  // Violation vs T60
        {"MKT-T70", 0.80},  // Violation vs T65
        {"MKT-T75", 0.50},  // OK
    };

    auto violations = ConsistencyChecker::check_monotonicity(markets);
    EXPECT_EQ(violations.size(), 2u);
}
