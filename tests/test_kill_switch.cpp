#include <gtest/gtest.h>
#include "risk/kill_switch.hpp"

using namespace trader;

class KillSwitchTest : public ::testing::Test {
protected:
    void SetUp() override {
        RiskConfig rcfg;
        rcfg.max_position_per_market = 10;
        rcfg.max_total_exposure = 80.0;
        rcfg.max_daily_loss = 15.0;
        rcfg.kill_switch_loss = 30.0;
        rcfg.cash_reserve_pct = 0.20;
        risk_ = std::make_unique<RiskManager>(rcfg);
        risk_->set_balance(100.0);

        KillSwitch::Config kcfg;
        kcfg.loss_threshold = 30.0;
        kcfg.max_consecutive_errors = 3;
        kcfg.connectivity_timeout = std::chrono::seconds(60);
        ks_ = std::make_unique<KillSwitch>(*risk_, kcfg);
    }

    std::unique_ptr<RiskManager> risk_;
    std::unique_ptr<KillSwitch> ks_;
};

TEST_F(KillSwitchTest, NotActiveInitially) {
    EXPECT_FALSE(ks_->is_active());
    EXPECT_FALSE(ks_->check());
}

TEST_F(KillSwitchTest, ManualTrigger) {
    ks_->trigger("Manual test");
    EXPECT_TRUE(ks_->is_active());
    EXPECT_EQ(ks_->reason(), "Manual test");
}

TEST_F(KillSwitchTest, Reset) {
    ks_->trigger("Test");
    EXPECT_TRUE(ks_->is_active());
    ks_->reset();
    EXPECT_FALSE(ks_->is_active());
}

TEST_F(KillSwitchTest, TriggersOnDailyLoss) {
    // Accumulate losses through risk manager
    for (int i = 0; i < 10; ++i) {
        std::string t = "MKT" + std::to_string(i);
        risk_->on_fill(t, "yes", 5, 0.60);
        risk_->on_settlement(t, -3.0);
    }
    // Daily PnL should be -30

    bool triggered = ks_->check();
    EXPECT_TRUE(triggered);
    EXPECT_TRUE(ks_->is_active());
}

TEST_F(KillSwitchTest, TriggersOnConsecutiveErrors) {
    ks_->on_order_error();
    EXPECT_FALSE(ks_->check());
    ks_->on_order_error();
    EXPECT_FALSE(ks_->check());
    ks_->on_order_error();  // 3rd error
    EXPECT_TRUE(ks_->check());
}

TEST_F(KillSwitchTest, ErrorCountResetsOnSuccess) {
    ks_->on_order_error();
    ks_->on_order_error();
    ks_->on_order_success();  // Reset counter
    ks_->on_order_error();    // Only 1 error now
    EXPECT_FALSE(ks_->check());
}

TEST_F(KillSwitchTest, HeartbeatPreventsTimeout) {
    ks_->on_heartbeat();
    EXPECT_FALSE(ks_->check());
}

TEST_F(KillSwitchTest, SnapshotCapturesState) {
    risk_->on_fill("MKT1", "yes", 5, 0.50);
    risk_->on_settlement("MKT1", 2.0);

    auto snap = ks_->snapshot();
    EXPECT_GT(snap.balance, 0.0);
    EXPECT_DOUBLE_EQ(snap.daily_pnl, 2.0);
}
