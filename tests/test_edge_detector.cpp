#include <gtest/gtest.h>
#include "strategy/kalshi/edge_detector.hpp"

using namespace trader;
using namespace trader::kalshi;

// ===== Kelly Criterion =====

TEST(EdgeDetector, KellyBasicCase) {
    // Model: 75%, market: $0.60, b = 0.4/0.6 = 0.667
    // f* = (0.667*0.75 - 0.25) / 0.667 = 0.375
    double f = EdgeDetector::kelly_fraction(0.75, 0.60);
    EXPECT_NEAR(f, 0.375, 0.01);
}

TEST(EdgeDetector, KellyNoEdge) {
    // Model matches market — no edge
    double f = EdgeDetector::kelly_fraction(0.60, 0.60);
    EXPECT_NEAR(f, 0.0, 0.01);
}

TEST(EdgeDetector, KellyNegativeEdge) {
    // Model says less likely than market price — should be 0 (don't bet)
    double f = EdgeDetector::kelly_fraction(0.40, 0.60);
    EXPECT_DOUBLE_EQ(f, 0.0);
}

TEST(EdgeDetector, KellyHighProbability) {
    // Model: 95%, market: $0.80
    double f = EdgeDetector::kelly_fraction(0.95, 0.80);
    EXPECT_GT(f, 0.5);  // Strong edge = large Kelly fraction
}

// ===== Net EV =====

TEST(EdgeDetector, NetEVPositive) {
    // Model: 75%, price: $0.60
    // EV = 0.75 * 1.0 - 0.60 - maker_fee(1, 0.60)
    double ev = EdgeDetector::net_ev_per_contract(0.75, 0.60);
    EXPECT_GT(ev, 0.10);  // Should be ~$0.14 after tiny fee
}

TEST(EdgeDetector, NetEVNegative) {
    // Model: 55%, price: $0.60
    double ev = EdgeDetector::net_ev_per_contract(0.55, 0.60);
    EXPECT_LT(ev, 0.0);
}

TEST(EdgeDetector, NetEVAccountsForFees) {
    double ev_no_fee = 0.75 * 1.0 - 0.60;  // $0.15 without fees
    double ev_with_fee = EdgeDetector::net_ev_per_contract(0.75, 0.60);
    EXPECT_LT(ev_with_fee, ev_no_fee);  // Fees reduce EV
}

// ===== Position Sizing =====

TEST(EdgeDetector, PositionSizeBasic) {
    EdgeDetector::Config cfg;
    cfg.bankroll = 100.0;
    cfg.kelly_fraction = 0.25;
    cfg.max_position = 10;
    EdgeDetector detector(cfg);

    // Model: 75%, price: $0.50
    // Kelly f* ≈ 0.50, fractional = 0.50*0.25 = 0.125
    // Bet: 0.125 * $100 = $12.50
    // Contracts: $12.50 / $0.50 = 25 → clamped to max 10
    int qty = detector.position_size(0.75, 0.50);
    EXPECT_EQ(qty, 10);  // Clamped to max
}

TEST(EdgeDetector, PositionSizeSmallEdge) {
    EdgeDetector::Config cfg;
    cfg.bankroll = 100.0;
    cfg.kelly_fraction = 0.25;
    cfg.max_position = 10;
    EdgeDetector detector(cfg);

    // Model: 60%, price: $0.55
    // Smaller edge → fewer contracts
    int qty = detector.position_size(0.60, 0.55);
    EXPECT_GE(qty, 0);
    EXPECT_LE(qty, 10);
}

TEST(EdgeDetector, PositionSizeNoEdge) {
    EdgeDetector detector;
    int qty = detector.position_size(0.50, 0.50);
    EXPECT_EQ(qty, 0);
}

// ===== Edge Detection =====

TEST(EdgeDetector, DetectStrongEdge) {
    EdgeDetector::Config cfg;
    cfg.min_edge = 0.05;
    cfg.min_confidence = 0.5;
    cfg.bankroll = 100.0;
    cfg.kelly_fraction = 0.25;
    cfg.max_position = 10;
    EdgeDetector detector(cfg);

    // Model says 85% YES, market has YES ask at 0.65
    auto result = detector.detect("KXHIGHNY-26APR-T75", 0.85, 0.60, 0.65, 0.8);

    EXPECT_EQ(result.contract_side, "yes");
    EXPECT_NEAR(result.edge, 0.20, 0.01);  // 0.85 - 0.65
    EXPECT_GT(result.kelly_quantity, 0);
    EXPECT_GT(result.net_ev, 0.0);
}

TEST(EdgeDetector, DetectNoEdge) {
    EdgeDetector detector;

    // Model matches market — no edge
    auto result = detector.detect("KXHIGHNY-26APR-T75", 0.65, 0.60, 0.65, 0.8);

    EXPECT_EQ(result.kelly_quantity, 0);
    EXPECT_TRUE(result.rationale.find("No trade") != std::string::npos);
}

TEST(EdgeDetector, DetectRejectsLowConfidence) {
    EdgeDetector::Config cfg;
    cfg.min_confidence = 0.5;
    EdgeDetector detector(cfg);

    // Good edge but low confidence
    auto result = detector.detect("KXHIGHNY-26APR-T75", 0.85, 0.60, 0.65, 0.3);
    EXPECT_EQ(result.kelly_quantity, 0);
}

TEST(EdgeDetector, DetectRejectsCheapContracts) {
    EdgeDetector::Config cfg;
    cfg.min_price = 0.15;
    EdgeDetector detector(cfg);

    // Cheap contract — favorite-longshot bias territory
    auto result = detector.detect("TICKER", 0.20, 0.05, 0.10, 0.8);
    EXPECT_EQ(result.kelly_quantity, 0);
}

TEST(EdgeDetector, DetectChoosesBestSide) {
    EdgeDetector detector;

    // Model says 30% YES → 70% NO
    // YES ask at 0.40 → edge_yes = 0.30 - 0.40 = -0.10
    // NO price = 1 - YES bid = 1 - 0.35 = 0.65 → edge_no = 0.70 - 0.65 = 0.05
    auto result = detector.detect("TICKER", 0.30, 0.35, 0.40, 0.8);

    EXPECT_EQ(result.contract_side, "no");
    EXPECT_NEAR(result.edge, 0.05, 0.01);
}
