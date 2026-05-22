#include <gtest/gtest.h>
#include "core/config.hpp"
#include <fstream>
#include <filesystem>

using namespace trader;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Write a test config file
        std::ofstream out(test_config_path_);
        out << R"(
venue: "kalshi"
mode: "paper"

kalshi:
  api_base: "https://demo-api.kalshi.co/trade-api/v2"
  ws_url: "wss://demo-api.kalshi.co/trade-api/ws/v2"
  api_key_file: "secrets/test_key.pem"
  api_key_id: "test-key-123"

risk:
  max_position_per_market: 15
  max_total_exposure: 90.0
  max_daily_loss: 20.0
  kill_switch_loss: 40.0
  cash_reserve_pct: 0.25
  maker_only: true

strategy:
  min_edge_threshold: 0.08
  kelly_fraction: 0.30
  tick_interval_seconds: 45

logging:
  level: "debug"
  file: "logs/test.log"
  console: false
)";
        out.close();
    }

    void TearDown() override {
        std::filesystem::remove(test_config_path_);
    }

    std::string test_config_path_ = "test_config_temp.yaml";
};

TEST_F(ConfigTest, LoadsVenueAndMode) {
    auto config = Config::load(test_config_path_);
    EXPECT_EQ(config.venue, "kalshi");
    EXPECT_EQ(config.mode, "paper");
}

TEST_F(ConfigTest, LoadsKalshiSection) {
    auto config = Config::load(test_config_path_);
    EXPECT_EQ(config.kalshi.api_base, "https://demo-api.kalshi.co/trade-api/v2");
    EXPECT_EQ(config.kalshi.ws_url, "wss://demo-api.kalshi.co/trade-api/ws/v2");
    EXPECT_EQ(config.kalshi.api_key_file, "secrets/test_key.pem");
    EXPECT_EQ(config.kalshi.api_key_id, "test-key-123");
}

TEST_F(ConfigTest, LoadsRiskSection) {
    auto config = Config::load(test_config_path_);
    EXPECT_EQ(config.risk.max_position_per_market, 15);
    EXPECT_DOUBLE_EQ(config.risk.max_total_exposure, 90.0);
    EXPECT_DOUBLE_EQ(config.risk.max_daily_loss, 20.0);
    EXPECT_DOUBLE_EQ(config.risk.kill_switch_loss, 40.0);
    EXPECT_DOUBLE_EQ(config.risk.cash_reserve_pct, 0.25);
    EXPECT_TRUE(config.risk.maker_only);
}

TEST_F(ConfigTest, LoadsStrategySection) {
    auto config = Config::load(test_config_path_);
    EXPECT_DOUBLE_EQ(config.strategy.min_edge_threshold, 0.08);
    EXPECT_DOUBLE_EQ(config.strategy.kelly_fraction, 0.30);
    EXPECT_EQ(config.strategy.tick_interval_seconds, 45);
}

TEST_F(ConfigTest, LoadsLoggingSection) {
    auto config = Config::load(test_config_path_);
    EXPECT_EQ(config.logging.level, "debug");
    EXPECT_EQ(config.logging.file, "logs/test.log");
    EXPECT_FALSE(config.logging.console);
}

TEST_F(ConfigTest, DefaultsWhenSectionMissing) {
    // Write a minimal config
    std::ofstream out("test_minimal_config.yaml");
    out << "venue: kalshi\n";
    out.close();

    auto config = Config::load("test_minimal_config.yaml");
    EXPECT_EQ(config.venue, "kalshi");
    // Defaults should apply
    EXPECT_EQ(config.risk.max_position_per_market, 10);
    EXPECT_DOUBLE_EQ(config.strategy.kelly_fraction, 0.25);
    EXPECT_EQ(config.logging.level, "info");

    std::filesystem::remove("test_minimal_config.yaml");
}

TEST_F(ConfigTest, ThrowsOnMissingFile) {
    EXPECT_THROW(Config::load("nonexistent_file.yaml"), std::runtime_error);
}
