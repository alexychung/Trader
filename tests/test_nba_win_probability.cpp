#include "strategy/nba/win_probability.hpp"

#include <gtest/gtest.h>
#include <cmath>

using trader::nba::WinProbability;

// Reference values from Clauset/Kogan/Redner 2015 and the Angry Statistician
// rederivation. See win_probability.hpp.

TEST(NbaWinProbability, SafeLeadAtKnownTimepoints) {
    // 90%-safe lead = z(0.90) × σ × √t  with z(0.90) ≈ 1.2816, σ ≈ 0.4602.

    // Halftime: t = 1440s (24:00 remaining). Reference ~17.5–18.1 pts.
    double L_half = WinProbability::safe_lead(1440.0, 0.10);
    EXPECT_GT(L_half, 17.0);
    EXPECT_LT(L_half, 19.0);

    // Start of Q4: t = 720s (12:00 remaining). Reference ~12.4–12.8 pts.
    double L_q4 = WinProbability::safe_lead(720.0, 0.10);
    EXPECT_GT(L_q4, 12.0);
    EXPECT_LT(L_q4, 13.5);

    // 5:00 remaining: t = 300s. Reference ~8.0 pts.
    double L_5min = WinProbability::safe_lead(300.0, 0.10);
    EXPECT_GT(L_5min, 7.5);
    EXPECT_LT(L_5min, 8.5);

    // 1:00 remaining: t = 60s. Should be ~3.6 pts at 90%.
    double L_1min = WinProbability::safe_lead(60.0, 0.10);
    EXPECT_GT(L_1min, 3.0);
    EXPECT_LT(L_1min, 4.5);
}

TEST(NbaWinProbability, SafeLeadHigherConfidenceMeansBiggerLead) {
    double L90 = WinProbability::safe_lead(720.0, 0.10);
    double L95 = WinProbability::safe_lead(720.0, 0.05);
    double L99 = WinProbability::safe_lead(720.0, 0.01);
    EXPECT_LT(L90, L95);
    EXPECT_LT(L95, L99);
}

TEST(NbaWinProbability, SafeLeadGrowsWithSqrtT) {
    // Doubling t should multiply safe lead by √2.
    double L1 = WinProbability::safe_lead(100.0, 0.10);
    double L2 = WinProbability::safe_lead(400.0, 0.10);  // 4x → √4 = 2x
    EXPECT_NEAR(L2, 2.0 * L1, 0.01);
}

TEST(NbaWinProbability, SurvivalProbabilityMidpoint) {
    EXPECT_NEAR(WinProbability::survival_probability(0.0, 720.0), 0.5, 1e-9);
}

TEST(NbaWinProbability, SurvivalProbabilitySymmetric) {
    // P(+L wins) = 1 - P(-L wins) for any L, t.
    for (double L : {1.0, 5.0, 12.4, 25.0}) {
        for (double t : {60.0, 300.0, 720.0, 1440.0}) {
            double p_pos = WinProbability::survival_probability(L, t);
            double p_neg = WinProbability::survival_probability(-L, t);
            EXPECT_NEAR(p_pos + p_neg, 1.0, 1e-9)
                << "L=" << L << " t=" << t;
        }
    }
}

TEST(NbaWinProbability, SurvivalProbabilityMonotonicInLead) {
    double prev = 0.5;
    for (double L = 1.0; L <= 30.0; L += 1.0) {
        double p = WinProbability::survival_probability(L, 720.0);
        EXPECT_GT(p, prev) << "non-monotonic at L=" << L;
        prev = p;
    }
}

TEST(NbaWinProbability, SafeLeadAtThatProbability) {
    // Internal consistency: if safe_lead(t, α) returns L*, then
    // survival_probability(L*, t) should be ≈ 1 - α.
    for (double t : {60.0, 300.0, 720.0, 1440.0}) {
        for (double alpha : {0.01, 0.05, 0.10, 0.20}) {
            double L = WinProbability::safe_lead(t, alpha);
            double p = WinProbability::survival_probability(L, t);
            EXPECT_NEAR(p, 1.0 - alpha, 1e-3)
                << "t=" << t << " alpha=" << alpha << " L=" << L;
        }
    }
}

TEST(NbaWinProbability, TimeZeroBoundary) {
    EXPECT_DOUBLE_EQ(WinProbability::safe_lead(0.0, 0.10), 0.0);
    EXPECT_DOUBLE_EQ(WinProbability::survival_probability(1.0, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(WinProbability::survival_probability(-1.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(WinProbability::survival_probability(0.0, 0.0), 0.5);
}

TEST(NbaWinProbability, InverseNormalCdfCanonicalValues) {
    // z(0.50) = 0, z(0.975) ≈ 1.96, z(0.995) ≈ 2.576, z(0.9) ≈ 1.2816
    EXPECT_NEAR(WinProbability::inverse_normal_cdf(0.50), 0.0, 1e-6);
    EXPECT_NEAR(WinProbability::inverse_normal_cdf(0.975), 1.95996, 1e-4);
    EXPECT_NEAR(WinProbability::inverse_normal_cdf(0.995), 2.57583, 1e-4);
    EXPECT_NEAR(WinProbability::inverse_normal_cdf(0.90),  1.28155, 1e-4);
}
