#include <gtest/gtest.h>
#include "core/dashboard.hpp"

using namespace trader;
using namespace trader::kalshi;

class DashboardTest : public ::testing::Test {
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

        kill_ = std::make_unique<KillSwitch>(*risk_);

        auth_.set_api_key_id("test");
        exchange_ = std::make_unique<KalshiExchange>(
            "https://demo-api.kalshi.co/trade-api/v2",
            "wss://demo-api.kalshi.co/trade-api/ws/v2",
            auth_
        );

        dashboard_ = std::make_unique<Dashboard>(*risk_, *kill_, calibration_, *exchange_);
    }

    std::unique_ptr<RiskManager> risk_;
    std::unique_ptr<KillSwitch> kill_;
    KalshiAuth auth_;
    std::unique_ptr<KalshiExchange> exchange_;
    CalibrationLogger calibration_;
    std::unique_ptr<Dashboard> dashboard_;
};

TEST_F(DashboardTest, RenderContainsBalance) {
    std::string output = dashboard_->render();
    EXPECT_TRUE(output.find("Balance") != std::string::npos);
    EXPECT_TRUE(output.find("$100.00") != std::string::npos);
}

TEST_F(DashboardTest, RenderContainsRunningStatus) {
    std::string output = dashboard_->render();
    EXPECT_TRUE(output.find("RUNNING") != std::string::npos);
}

TEST_F(DashboardTest, RenderContainsKilledStatus) {
    kill_->trigger("Test kill");
    std::string output = dashboard_->render();
    EXPECT_TRUE(output.find("KILLED") != std::string::npos);
    EXPECT_TRUE(output.find("Test kill") != std::string::npos);
}

TEST_F(DashboardTest, RenderContainsBrierScore) {
    std::string output = dashboard_->render();
    EXPECT_TRUE(output.find("Brier") != std::string::npos);
}

TEST_F(DashboardTest, RenderContainsKeyboardHints) {
    std::string output = dashboard_->render();
    EXPECT_TRUE(output.find("[Q]uit") != std::string::npos);
    EXPECT_TRUE(output.find("[K]ill") != std::string::npos);
}

TEST_F(DashboardTest, RenderShowsPositions) {
    // Add a position via exchange
    WsFill fill{.order_id="o1", .ticker="KXHIGHNY-26APR-T75",
                .price=0.65, .count=5, .side="yes", .action="buy"};
    exchange_->on_fill(fill);

    std::string output = dashboard_->render();
    EXPECT_TRUE(output.find("KXHIGHNY") != std::string::npos);
    EXPECT_TRUE(output.find("1 active") != std::string::npos);
}

TEST_F(DashboardTest, RenderShowsSignalAndTradeCounts) {
    dashboard_->set_signal_count(15);
    dashboard_->set_trade_count(8);

    std::string output = dashboard_->render();
    EXPECT_TRUE(output.find("Signals: 15") != std::string::npos);
    EXPECT_TRUE(output.find("Trades: 8") != std::string::npos);
}

TEST_F(DashboardTest, RenderShowsUptime) {
    std::string output = dashboard_->render();
    EXPECT_TRUE(output.find("Uptime") != std::string::npos);
}
