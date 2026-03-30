#include "core/config.hpp"
#include <fstream>
#include <stdexcept>

namespace trader {

Config Config::load(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load config: " + std::string(e.what()));
    }

    Config config;

    if (root["venue"])  config.venue = root["venue"].as<std::string>();
    if (root["mode"])   config.mode = root["mode"].as<std::string>();

    if (auto k = root["kalshi"]) {
        if (k["api_base"])     config.kalshi.api_base = k["api_base"].as<std::string>();
        if (k["ws_url"])       config.kalshi.ws_url = k["ws_url"].as<std::string>();
        if (k["api_key_file"]) config.kalshi.api_key_file = k["api_key_file"].as<std::string>();
        if (k["api_key_id"])   config.kalshi.api_key_id = k["api_key_id"].as<std::string>();
    }

    if (auto r = root["risk"]) {
        if (r["max_position_per_market"]) config.risk.max_position_per_market = r["max_position_per_market"].as<int>();
        if (r["max_total_exposure"])      config.risk.max_total_exposure = r["max_total_exposure"].as<double>();
        if (r["max_daily_loss"])          config.risk.max_daily_loss = r["max_daily_loss"].as<double>();
        if (r["kill_switch_loss"])        config.risk.kill_switch_loss = r["kill_switch_loss"].as<double>();
        if (r["cash_reserve_pct"])        config.risk.cash_reserve_pct = r["cash_reserve_pct"].as<double>();
        if (r["maker_only"])             config.risk.maker_only = r["maker_only"].as<bool>();
    }

    if (auto s = root["strategy"]) {
        if (s["min_edge_threshold"])    config.strategy.min_edge_threshold = s["min_edge_threshold"].as<double>();
        if (s["min_spread_to_mm"])      config.strategy.min_spread_to_mm = s["min_spread_to_mm"].as<double>();
        if (s["kelly_fraction"])        config.strategy.kelly_fraction = s["kelly_fraction"].as<double>();
        if (s["tick_interval_seconds"]) config.strategy.tick_interval_seconds = s["tick_interval_seconds"].as<int>();
    }

    if (auto l = root["logging"]) {
        if (l["level"])   config.logging.level = l["level"].as<std::string>();
        if (l["file"])    config.logging.file = l["file"].as<std::string>();
        if (l["console"]) config.logging.console = l["console"].as<bool>();
    }

    return config;
}

} // namespace trader
