#pragma once

#include <string>
#include <yaml-cpp/yaml.h>

namespace trader {

struct KalshiConfig {
    std::string api_base = "https://demo-api.kalshi.co/trade-api/v2";
    std::string ws_url = "wss://demo-api.kalshi.co/trade-api/ws/v2";
    std::string api_key_file;
    std::string api_key_id;
};

struct RiskConfig {
    int max_position_per_market = 10;
    double max_total_exposure = 80.0;
    double max_daily_loss = 15.0;
    double kill_switch_loss = 30.0;
    double cash_reserve_pct = 0.20;
    bool maker_only = true;
};

struct StrategyConfig {
    double min_edge_threshold = 0.05;
    double min_spread_to_mm = 0.08;
    double kelly_fraction = 0.25;
    int tick_interval_seconds = 60;
};

struct LoggingConfig {
    std::string level = "info";
    std::string file = "logs/trader.log";
    bool console = true;
};

struct Config {
    std::string venue = "kalshi";
    std::string mode = "paper";
    KalshiConfig kalshi;
    RiskConfig risk;
    StrategyConfig strategy;
    LoggingConfig logging;

    static Config load(const std::string& path);
};

} // namespace trader
