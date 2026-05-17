// Exhaustive tests for Kalshi fee rounding.
//
// Kalshi fees (per the official fee schedule, still accurate in 2026):
//   maker_fee_cents = ceil(0.0175 * C * P * (1 - P) * 100)
//   taker_fee_cents = ceil(0.07   * C * P * (1 - P) * 100)
//
// Our helpers return dollars, so the ceil is to the cent and the return must
// always be a multiple of 0.01 to within floating-point precision. These tests
// pin that contract — if the formula or the ceil rounding ever drifts, the
// rest of the bot's EV math silently loses money.

#include <gtest/gtest.h>
#include "core/types.hpp"
#include <cmath>

using namespace trader;

// Helper: the fee-in-dollars must land on a cent grid.
static void expect_on_cent_grid(double fee) {
    double cents = fee * 100.0;
    double residual = std::abs(cents - std::round(cents));
    EXPECT_LT(residual, 1e-9) << "fee " << fee << " is not on a cent boundary";
}

// ===== Hand-computed exact values =====

TEST(KalshiFees, MakerAtMid_1Contract_IsOneCent) {
    // 0.0175 * 1 * 0.50 * 0.50 = 0.004375 → ceil to 0.01
    EXPECT_NEAR(kalshi_maker_fee(1, 0.50), 0.01, 1e-12);
}

TEST(KalshiFees, MakerAtMid_100Contracts_IsFortyFourCents) {
    // 0.0175 * 100 * 0.25 = 0.4375 → ceil to 0.44
    // (documented cap: ~0.44¢/contract at P=0.50)
    EXPECT_NEAR(kalshi_maker_fee(100, 0.50), 0.44, 1e-12);
}

TEST(KalshiFees, TakerAtMid_1Contract_IsTwoCents) {
    // 0.07 * 1 * 0.25 = 0.0175 → ceil to 0.02
    EXPECT_NEAR(kalshi_taker_fee(1, 0.50), 0.02, 1e-12);
}

TEST(KalshiFees, TakerAtMid_100Contracts_IsOneDollarSeventyFive) {
    // 0.07 * 100 * 0.25 = 1.75
    EXPECT_NEAR(kalshi_taker_fee(100, 0.50), 1.75, 1e-12);
}

// ===== Boundary prices: fees approach 0 at $0.01 / $0.99 =====

TEST(KalshiFees, MakerAtExtremePrices_RoundsUpToMinCent) {
    // At P=0.01: raw = 0.0175 * 1 * 0.01 * 0.99 = 0.00017325 → ceil to 0.01
    // At P=0.99: symmetric
    EXPECT_NEAR(kalshi_maker_fee(1, 0.01), 0.01, 1e-12);
    EXPECT_NEAR(kalshi_maker_fee(1, 0.99), 0.01, 1e-12);
}

TEST(KalshiFees, MakerIsSymmetricAroundHalf) {
    // p*(1-p) == (1-p)*p — exact.
    for (double p = 0.05; p < 0.50; p += 0.05) {
        double lo = kalshi_maker_fee(17, p);
        double hi = kalshi_maker_fee(17, 1.0 - p);
        EXPECT_NEAR(lo, hi, 1e-12) << "asymmetry at p=" << p;
    }
}

TEST(KalshiFees, TakerIsSymmetricAroundHalf) {
    for (double p = 0.05; p < 0.50; p += 0.05) {
        double lo = kalshi_taker_fee(17, p);
        double hi = kalshi_taker_fee(17, 1.0 - p);
        EXPECT_NEAR(lo, hi, 1e-12) << "asymmetry at p=" << p;
    }
}

// ===== Degenerate inputs =====

TEST(KalshiFees, ZeroContractsReturnsZeroFee) {
    EXPECT_DOUBLE_EQ(kalshi_maker_fee(0, 0.50), 0.0);
    EXPECT_DOUBLE_EQ(kalshi_taker_fee(0, 0.50), 0.0);
}

TEST(KalshiFees, PriceAtExactZeroOrOneReturnsZeroFee) {
    EXPECT_DOUBLE_EQ(kalshi_maker_fee(10, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(kalshi_maker_fee(10, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(kalshi_taker_fee(10, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(kalshi_taker_fee(10, 1.0), 0.0);
}

// ===== Ceil behavior: any sub-cent raw amount bumps to next cent =====

TEST(KalshiFees, MakerCeilsUpEvenOnMicroRemainder) {
    // 0.0175 * 3 * 0.30 * 0.70 = 0.011025 → must ceil to 0.02 (not round to 0.01)
    EXPECT_NEAR(kalshi_maker_fee(3, 0.30), 0.02, 1e-12);
}

TEST(KalshiFees, TakerCeilsUpEvenOnMicroRemainder) {
    // 0.07 * 2 * 0.30 * 0.70 = 0.0294 → ceil to 0.03
    EXPECT_NEAR(kalshi_taker_fee(2, 0.30), 0.03, 1e-12);
}

// ===== Grid verification across a wide sweep =====

TEST(KalshiFees, AllFeesLandOnCentGrid) {
    for (int c : {1, 2, 5, 7, 10, 13, 17, 50, 99, 137, 500}) {
        for (double p = 0.01; p <= 0.99 + 1e-9; p += 0.01) {
            expect_on_cent_grid(kalshi_maker_fee(c, p));
            expect_on_cent_grid(kalshi_taker_fee(c, p));
        }
    }
}

// ===== Scaling: fees are (nearly) linear in contract count =====

TEST(KalshiFees, MakerFeeRoughlyScalesWithContractCount) {
    // Not exactly linear due to per-batch ceil, but the ratio must be within
    // one cent per contract of the linear projection.
    double f1 = kalshi_maker_fee(1, 0.50);
    double f100 = kalshi_maker_fee(100, 0.50);
    EXPECT_GE(f100, 100.0 * f1 - 1.0);
    EXPECT_LE(f100, 100.0 * f1 + 1.0);
}

// ===== Taker is strictly higher than maker at the same (C, P) =====

TEST(KalshiFees, TakerAlwaysAtLeastAsHighAsMaker) {
    for (int c : {1, 10, 100}) {
        for (double p = 0.05; p <= 0.95 + 1e-9; p += 0.05) {
            EXPECT_GE(kalshi_taker_fee(c, p), kalshi_maker_fee(c, p))
                << "taker < maker at (c=" << c << ", p=" << p << ")";
        }
    }
}

// ===== Round-trip scenarios used by the sizer =====

TEST(KalshiFees, RoundTripMakerFeeAtMid_1Contract_IsTwoCents) {
    // Per EdgeDetector::fee_floor_edge when assume_round_trip_fees=true:
    // total = 2 * maker_fee(1, 0.5) = 2 * 0.01 = 0.02.
    // This is the pre-buffer threshold the edge must clear.
    double rt = 2.0 * kalshi_maker_fee(1, 0.50);
    EXPECT_NEAR(rt, 0.02, 1e-12);
}

TEST(KalshiFees, NetEVAfterRoundTripMatchesManualCalc) {
    // Pure hand calc for the docs: model=0.70, price=0.50, 1 contract.
    // gross = 0.70 - 0.50 = 0.20
    // round-trip fee = 2 * ceil(0.0175 * 1 * 0.25 * 100)/100 = 2 * 0.01 = 0.02
    // net = 0.18
    const double gross = 0.70 - 0.50;
    const double fee = 2.0 * kalshi_maker_fee(1, 0.50);
    EXPECT_NEAR(gross - fee, 0.18, 1e-12);
}
