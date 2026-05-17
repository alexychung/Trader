#include <gtest/gtest.h>
#include "risk/risk_manager.hpp"

using namespace trader;

class RiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        RiskConfig config;
        config.max_position_per_market = 10;
        config.max_total_exposure = 80.0;
        config.max_daily_loss = 15.0;
        config.kill_switch_loss = 30.0;
        config.cash_reserve_pct = 0.20;
        config.maker_only = true;

        rm_ = std::make_unique<RiskManager>(config);
        rm_->set_balance(100.0);
    }

    std::unique_ptr<RiskManager> rm_;
};

// ===== Pre-trade gate =====

TEST_F(RiskManagerTest, AllowsValidTrade) {
    auto check = rm_->check_trade("MKT1", 5, 0.50);
    EXPECT_TRUE(check.allowed);
    EXPECT_EQ(check.reason, "OK");
}

TEST_F(RiskManagerTest, RejectsOverPositionLimit) {
    // Fill 8 contracts first
    rm_->on_fill("MKT1", "yes", 8, 0.50);

    // Try to add 5 more (8+5=13 > max 10)
    auto check = rm_->check_trade("MKT1", 5, 0.50);
    EXPECT_FALSE(check.allowed);
    EXPECT_TRUE(check.reason.find("Per-market") != std::string::npos);
}

TEST_F(RiskManagerTest, AllowsExactlyAtPositionLimit) {
    rm_->on_fill("MKT1", "yes", 8, 0.50);
    auto check = rm_->check_trade("MKT1", 2, 0.50);  // 8+2 = 10 = max
    EXPECT_TRUE(check.allowed);
}

TEST_F(RiskManagerTest, RejectsOverExposureLimit) {
    // Fill $75 of exposure
    rm_->on_fill("MKT1", "yes", 10, 0.50);  // $5
    rm_->on_fill("MKT2", "yes", 10, 0.50);  // $5 more = $10 total
    // But balance is reduced by fills: 100 - 10 = 90

    // Actually let's do a larger trade to hit the limit
    rm_->set_balance(100.0);
    rm_->on_fill("BIG", "yes", 10, 7.0);  // $70 exposure

    auto check = rm_->check_trade("MKT3", 5, 3.0);  // +$15 → $85 > $80
    EXPECT_FALSE(check.allowed);
    EXPECT_TRUE(check.reason.find("exposure") != std::string::npos);
}

TEST_F(RiskManagerTest, RejectsAfterDailyLoss) {
    // Simulate a losing day
    rm_->on_fill("MKT1", "yes", 10, 0.50);
    rm_->on_settlement("MKT1", -5.0);  // Lose $5

    rm_->on_fill("MKT2", "yes", 10, 0.50);
    rm_->on_settlement("MKT2", -5.0);  // Lose $5 more

    rm_->on_fill("MKT3", "yes", 10, 0.50);
    rm_->on_settlement("MKT3", -6.0);  // Total daily loss: $16 > $15 limit

    auto check = rm_->check_trade("MKT4", 1, 0.50);
    EXPECT_FALSE(check.allowed);
    EXPECT_TRUE(check.reason.find("Daily loss") != std::string::npos);
}

TEST_F(RiskManagerTest, RejectsWhenKilled) {
    rm_->trigger_kill("Manual test");
    auto check = rm_->check_trade("MKT1", 1, 0.50);
    EXPECT_FALSE(check.allowed);
    EXPECT_TRUE(check.reason.find("Kill switch") != std::string::npos);
}

TEST_F(RiskManagerTest, RejectsInsufficientCapital) {
    rm_->set_balance(10.0);  // Only $10

    // Reserve is 20% of $10 = $2, available = $8
    // Try to buy 10 contracts at $0.90 = $9 > $8 available after reserve
    auto check = rm_->check_trade("MKT1", 10, 0.90);
    EXPECT_FALSE(check.allowed);
    EXPECT_TRUE(check.reason.find("capital") != std::string::npos ||
                check.reason.find("reserve") != std::string::npos);
}

// ===== Position Tracking =====

TEST_F(RiskManagerTest, FillUpdatesExposure) {
    EXPECT_DOUBLE_EQ(rm_->total_exposure(), 0.0);

    rm_->on_fill("MKT1", "yes", 5, 0.50);
    EXPECT_NEAR(rm_->total_exposure(), 2.50, 0.01);

    rm_->on_fill("MKT2", "no", 3, 0.40);
    EXPECT_NEAR(rm_->total_exposure(), 3.70, 0.01);
}

TEST_F(RiskManagerTest, ActiveMarketCount) {
    EXPECT_EQ(rm_->active_market_count(), 0);

    rm_->on_fill("MKT1", "yes", 1, 0.50);
    EXPECT_EQ(rm_->active_market_count(), 1);

    rm_->on_fill("MKT2", "no", 1, 0.50);
    EXPECT_EQ(rm_->active_market_count(), 2);

    // Same market doesn't increase count
    rm_->on_fill("MKT1", "yes", 1, 0.50);
    EXPECT_EQ(rm_->active_market_count(), 2);
}

// ===== Settlement =====

TEST_F(RiskManagerTest, SettlementWinUpdatesPnL) {
    rm_->on_fill("MKT1", "yes", 5, 0.40);  // Cost $2.00
    rm_->on_settlement("MKT1", 3.0);  // Win $3.00

    EXPECT_DOUBLE_EQ(rm_->daily_pnl(), 3.0);
    EXPECT_EQ(rm_->active_market_count(), 0);  // Settled, removed
}

TEST_F(RiskManagerTest, SettlementLossUpdatesPnL) {
    rm_->on_fill("MKT1", "yes", 5, 0.60);  // Cost $3.00
    rm_->on_settlement("MKT1", -3.0);  // Lose all

    EXPECT_DOUBLE_EQ(rm_->daily_pnl(), -3.0);
}

TEST_F(RiskManagerTest, SettlementWinReturnsCostPlusPnL) {
    // Balance accounting is the load-bearing invariant here: on_fill debits
    // cost up front, on_settlement must return cost + pnl so a winning
    // ticker ends the cycle with the expected net change in balance.
    // Miswiring this (e.g. returning only pnl) silently underreports
    // balance by the ticker's cost on every settlement and the bot
    // accumulates a phantom loss equal to the week's gross traded volume.
    rm_->set_balance(100.0);
    rm_->on_fill("WIN", "yes", 5, 0.40);          // cost $2.00 debited
    EXPECT_DOUBLE_EQ(rm_->balance(), 98.0);

    rm_->on_settlement("WIN", +3.0);              // +$3 net over cost
    EXPECT_DOUBLE_EQ(rm_->balance(), 103.0);      // 98 + 2 + 3
    EXPECT_DOUBLE_EQ(rm_->daily_pnl(), 3.0);
}

TEST_F(RiskManagerTest, SettlementLossReturnsCostPlusNegativePnL) {
    // Losing the full cost nets to balance unchanged before-and-after the
    // cycle (cost returned but pnl == -cost). Invariant: balance after a
    // total loss == balance before the entry.
    rm_->set_balance(100.0);
    rm_->on_fill("LOSE", "yes", 5, 0.60);         // cost $3.00 debited
    EXPECT_DOUBLE_EQ(rm_->balance(), 97.0);

    rm_->on_settlement("LOSE", -3.0);             // full loss: pnl == -cost
    EXPECT_DOUBLE_EQ(rm_->balance(), 97.0);       // unchanged
    EXPECT_DOUBLE_EQ(rm_->daily_pnl(), -3.0);
}

TEST_F(RiskManagerTest, SettlementRemovesFromExposure) {
    rm_->on_fill("A", "yes", 5, 0.40);   // $2 exposure
    rm_->on_fill("B", "yes", 5, 0.30);   // $1.50 exposure
    EXPECT_DOUBLE_EQ(rm_->total_exposure(), 3.5);

    rm_->on_settlement("A", +1.0);
    // A is settled; only B's cost remains in exposure.
    EXPECT_DOUBLE_EQ(rm_->total_exposure(), 1.5);
}

TEST_F(RiskManagerTest, SettlementWithoutPriorFillIsNoOp) {
    // Defensive: a settlement event arriving for a ticker the risk manager
    // never tracked (e.g., position was fully seeded via the REST path and
    // somehow lost, or the ticker resolved before we knew about it) should
    // not crash or corrupt daily_pnl.
    rm_->set_balance(100.0);
    rm_->on_settlement("UNKNOWN", +5.0);

    EXPECT_DOUBLE_EQ(rm_->balance(), 100.0);
    EXPECT_DOUBLE_EQ(rm_->daily_pnl(), 0.0);
    EXPECT_FALSE(rm_->is_killed());
}

// ===== Kill Switch =====

TEST_F(RiskManagerTest, KillSwitchTriggersOnLoss) {
    EXPECT_FALSE(rm_->is_killed());

    // Accumulate big losses
    for (int i = 0; i < 10; ++i) {
        std::string ticker = "MKT" + std::to_string(i);
        rm_->on_fill(ticker, "yes", 5, 0.60);
        rm_->on_settlement(ticker, -3.0);  // Lose $3 each = $30 total
    }

    EXPECT_TRUE(rm_->is_killed());
}

TEST_F(RiskManagerTest, KillSwitchReset) {
    rm_->trigger_kill("Test");
    EXPECT_TRUE(rm_->is_killed());

    rm_->reset_kill();
    EXPECT_FALSE(rm_->is_killed());
}

// ===== Daily Reset =====

TEST_F(RiskManagerTest, DailyResetClearsPnL) {
    rm_->on_fill("MKT1", "yes", 5, 0.40);
    rm_->on_settlement("MKT1", 3.0);
    EXPECT_DOUBLE_EQ(rm_->daily_pnl(), 3.0);

    rm_->reset_daily();
    EXPECT_DOUBLE_EQ(rm_->daily_pnl(), 0.0);
}
