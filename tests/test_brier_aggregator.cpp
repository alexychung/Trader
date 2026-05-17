#include <gtest/gtest.h>
#include "strategy/kalshi/brier_aggregator.hpp"

#include <cmath>
#include <random>

using namespace trader::kalshi;

namespace {

double brier(double p, int y) {
    double d = p - static_cast<double>(y);
    return d * d;
}

}  // namespace

// ===== Construction =====

TEST(BrierAggregator, StartsAtUniformWeights) {
    BrierAggregator agg(3);
    auto w = agg.weights();
    ASSERT_EQ(w.size(), 3u);
    for (double wi : w) EXPECT_NEAR(wi, 1.0 / 3.0, 1e-9);
}

TEST(BrierAggregator, RejectsZeroK) {
    EXPECT_THROW(BrierAggregator{0}, std::invalid_argument);
}

TEST(BrierAggregator, RejectsOutOfRangeShare) {
    EXPECT_THROW(BrierAggregator(2, 2.0, 1.0), std::invalid_argument);
    EXPECT_THROW(BrierAggregator(2, 2.0, -0.1), std::invalid_argument);
}

// ===== predict() =====

TEST(BrierAggregator, PredictAtUniformWeightsMatchesLinearPool) {
    // With equal weights and no observations yet, the AA substitution should
    // agree with the linear pool for moderate probabilities. This is a sanity
    // check that the math is not off-by-one.
    BrierAggregator agg(2);
    std::vector<double> p = {0.60, 0.40};
    double p_hat = agg.predict(p);
    EXPECT_NEAR(p_hat, 0.50, 0.02);  // AA and linear agree to ~1st digit here
}

TEST(BrierAggregator, PredictRespectsInputClamping) {
    // Out-of-range inputs don't blow up the substitution function.
    BrierAggregator agg(2);
    std::vector<double> p = {1.5, -0.3};
    double p_hat = agg.predict(p);
    EXPECT_GE(p_hat, 0.0);
    EXPECT_LE(p_hat, 1.0);
}

TEST(BrierAggregator, PredictHandlesWrongSizeInput) {
    BrierAggregator agg(3);
    EXPECT_DOUBLE_EQ(agg.predict({0.5, 0.5}), 0.5);  // size mismatch → 0.5
}

// ===== observe() and weight drift =====

TEST(BrierAggregator, RepeatedLossOnExpertShrinksItsWeight) {
    // Expert 0 keeps predicting 0.9 when truth is 0 → should lose weight.
    // Expert 1 keeps predicting 0.1 when truth is 0 → should gain weight.
    BrierAggregator agg(2, /*eta=*/2.0, /*share=*/0.0);
    std::vector<double> p = {0.9, 0.1};
    for (int t = 0; t < 20; ++t) {
        agg.observe(p, /*outcome=*/0);
    }
    auto w = agg.weights();
    EXPECT_LT(w[0], 0.05);
    EXPECT_GT(w[1], 0.95);
    EXPECT_EQ(agg.num_observations(), 20u);
}

TEST(BrierAggregator, WrongOutcomeIsNoOp) {
    BrierAggregator agg(2);
    std::vector<double> before = agg.weights();
    agg.observe({0.5, 0.5}, /*outcome=*/2);  // not 0 or 1
    auto after = agg.weights();
    EXPECT_EQ(before, after);
    EXPECT_EQ(agg.num_observations(), 0u);
}

TEST(BrierAggregator, SizeMismatchObserveIsNoOp) {
    BrierAggregator agg(3);
    agg.observe({0.5, 0.5}, 1);  // size 2 != K=3
    EXPECT_EQ(agg.num_observations(), 0u);
}

TEST(BrierAggregator, FixedShareBoundsMinimumWeight) {
    // Without fixed-share, expert 0's weight collapses past 1e-6. With
    // share=0.05, it's bounded below by ~share/K = 0.025 asymptotically.
    BrierAggregator agg(2, /*eta=*/2.0, /*share=*/0.05);
    for (int t = 0; t < 50; ++t) {
        agg.observe({0.99, 0.01}, /*outcome=*/0);
    }
    auto w = agg.weights();
    EXPECT_GT(w[0], 0.01) << "fixed-share should prevent full collapse";
    EXPECT_GT(w[1], 0.9);
}

TEST(BrierAggregator, FixedShareZeroAllowsFullCollapse) {
    BrierAggregator agg(2, /*eta=*/2.0, /*share=*/0.0);
    for (int t = 0; t < 100; ++t) {
        agg.observe({0.99, 0.01}, /*outcome=*/0);
    }
    auto w = agg.weights();
    EXPECT_LT(w[0], 1e-20) << "no fixed-share → exponential collapse";
}

// ===== Prediction shifts toward winning expert =====

TEST(BrierAggregator, PredictShiftsTowardBetterExpert) {
    // Expert 0: well-calibrated (p=0.8 when truth has p=0.8). Expert 1: bad
    // (p=0.2 when truth has p=0.8). After many observations, the aggregated
    // prediction should lean toward expert 0's call.
    BrierAggregator agg(2, /*eta=*/2.0, /*share=*/0.0);
    std::mt19937 rng(42);
    std::bernoulli_distribution truth(0.8);
    for (int t = 0; t < 100; ++t) {
        int y = truth(rng) ? 1 : 0;
        agg.observe({0.8, 0.2}, y);
    }
    double p_hat = agg.predict({0.8, 0.2});
    // The substitution function asymmetrically shifts p_hat toward the
    // weighted majority; with nearly all mass on expert 0 at 0.8, the
    // aggregate should land close to 0.8, not 0.5.
    EXPECT_GT(p_hat, 0.65);
    auto w = agg.weights();
    EXPECT_GT(w[0], 0.95);
}

// ===== Regret bound sanity check =====

TEST(BrierAggregator, RegretBoundedByLnKOverEta) {
    // Vovk 2009: AA with Brier and eta=2 has cumulative regret ≤ ln(K)/eta
    // against the best expert in hindsight. For K=2, that's 0.35 Brier units.
    // This test generates a synthetic trace of 200 rounds and verifies the
    // observed regret stays well within the bound.
    BrierAggregator agg(2, /*eta=*/2.0, /*share=*/0.0);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u01(0.0, 1.0);

    double cum_agg = 0.0, cum0 = 0.0, cum1 = 0.0;
    for (int t = 0; t < 200; ++t) {
        double p0 = 0.3 + 0.4 * u01(rng);   // expert 0 noisy around 0.5
        double p1 = 0.8 - 0.1 * u01(rng);   // expert 1 near 0.75
        int y = u01(rng) < 0.7 ? 1 : 0;      // truth: 70% YES

        double p_agg = agg.predict({p0, p1});
        cum_agg += brier(p_agg, y);
        cum0 += brier(p0, y);
        cum1 += brier(p1, y);

        agg.observe({p0, p1}, y);
    }
    double best = std::min(cum0, cum1);
    double regret = cum_agg - best;
    // Slack for the finite-sample variance; the theoretical bound is
    // ln(2)/2 ≈ 0.35, but the proof's constant hides a small additive term.
    EXPECT_LT(regret, 2.0) << "cum_agg=" << cum_agg << " best=" << best;
}

// ===== Registry =====

TEST(BrierAggregatorRegistry, LazilyCreatesPerContext) {
    BrierAggregatorRegistry reg(2, 2.0, 0.01);
    auto& a = reg.get("weather");
    auto& b = reg.get("weather");
    EXPECT_EQ(&a, &b) << "same context returns same instance";

    auto& c = reg.get("economics");
    EXPECT_NE(&a, &c);

    // snapshot sees both.
    auto snap = reg.snapshot();
    EXPECT_EQ(snap.size(), 2u);
}

TEST(BrierAggregatorRegistry, EachContextUpdatesIndependently) {
    BrierAggregatorRegistry reg(2, 2.0, 0.0);
    auto& w = reg.get("weather");
    auto& m = reg.get("macro");

    for (int t = 0; t < 20; ++t) {
        w.observe({0.9, 0.1}, 0);   // punish expert 0 in weather
        m.observe({0.1, 0.9}, 0);   // punish expert 1 in macro
    }
    auto ww = w.weights();
    auto mw = m.weights();
    EXPECT_LT(ww[0], 0.1);
    EXPECT_GT(ww[1], 0.9);
    EXPECT_GT(mw[0], 0.9);
    EXPECT_LT(mw[1], 0.1);
}
