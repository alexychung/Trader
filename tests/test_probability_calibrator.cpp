#include <gtest/gtest.h>
#include "strategy/kalshi/probability_calibrator.hpp"

using namespace trader::kalshi;

static CategoryAggregate agg(int total, double brier) {
    CategoryAggregate a;
    a.category = "weather";
    a.resolved_total = total;
    a.brier = brier;
    return a;
}

static ProbabilityCalibrator::Config shrinkage_on() {
    ProbabilityCalibrator::Config c;
    c.enabled = true;
    return c;
}

TEST(ProbabilityCalibrator, DefaultDisabledPassesThroughRawProb) {
    // Default: shrinkage is off (see probability_calibrator.hpp). Stacking
    // shrinkage on top of AdaptiveSizer + fractional Kelly produced ~1.25%
    // of full Kelly at cold start — one uncertainty layer is the ceiling.
    ProbabilityCalibrator pc;
    auto a = agg(0, 0.25);  // worst-case cold start
    double adj = pc.calibrate("weather", 0.90, 0.65, a);
    EXPECT_NEAR(adj, 0.90, 1e-9);
}

TEST(ProbabilityCalibrator, DefaultDisabledClampsOutOfRange) {
    ProbabilityCalibrator pc;
    auto a = agg(0, 0.25);
    EXPECT_LE(pc.calibrate("x", 1.5, 0.5, a), 1.0);
    EXPECT_GE(pc.calibrate("x", -0.5, 0.5, a), 0.0);
}

TEST(ProbabilityCalibrator, EnabledColdStartShrinksHardToMarket) {
    ProbabilityCalibrator pc(shrinkage_on());
    auto a = agg(0, 0.25);
    double adj = pc.calibrate("weather", 0.90, 0.65, a);
    // w = 0.20 → adjusted = 0.20*0.90 + 0.80*0.65 = 0.70
    EXPECT_NEAR(adj, 0.70, 0.001);
}

TEST(ProbabilityCalibrator, EnabledProvenModelMostlyTrusted) {
    ProbabilityCalibrator pc(shrinkage_on());
    auto a = agg(50, 0.10);
    double adj = pc.calibrate("weather", 0.90, 0.65, a);
    // samples_factor = 1, cal_factor = (0.25-0.10)/0.15 = 1, trust = 1
    // w = 0.20 + 0.60*1 = 0.80 → adjusted = 0.80*0.90 + 0.20*0.65 = 0.85
    EXPECT_NEAR(adj, 0.85, 0.001);
}

TEST(ProbabilityCalibrator, EnabledBadBrierShrinksHardEvenWithSamples) {
    ProbabilityCalibrator pc(shrinkage_on());
    auto a = agg(50, 0.25);
    double adj = pc.calibrate("weather", 0.90, 0.65, a);
    // cal_factor = 0 → trust = 0 → w = 0.20 → adjusted = 0.70
    EXPECT_NEAR(adj, 0.70, 0.001);
}

TEST(ProbabilityCalibrator, TrustWeightNeverExitsBounds) {
    ProbabilityCalibrator pc(shrinkage_on());
    auto a = agg(1000, 0.0);  // perfect, lots of samples
    double w = pc.trust_weight(a);
    EXPECT_LE(w, 0.80 + 1e-6);  // hard cap at w_max
    EXPECT_GE(w, 0.20 - 1e-6);
}

TEST(ProbabilityCalibrator, EnabledClampsResultTo01) {
    ProbabilityCalibrator pc(shrinkage_on());
    auto a = agg(0, 0.25);
    EXPECT_LE(pc.calibrate("x", 1.5, 0.5, a), 1.0);
    EXPECT_GE(pc.calibrate("x", -0.5, 0.5, a), 0.0);
}
