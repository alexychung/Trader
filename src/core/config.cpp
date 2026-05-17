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
        // Prefer nested demo/prod blocks keyed by mode. Fall back to the
        // flat layout (all fields directly under `kalshi:`) for older configs.
        const char* block = (config.mode == "live") ? "prod" : "demo";
        auto nested = k[block];
        auto src = nested ? nested : k;
        if (src["api_base"])     config.kalshi.api_base = src["api_base"].as<std::string>();
        if (src["ws_url"])       config.kalshi.ws_url = src["ws_url"].as<std::string>();
        if (src["api_key_file"]) config.kalshi.api_key_file = src["api_key_file"].as<std::string>();
        if (src["api_key_id"])   config.kalshi.api_key_id = src["api_key_id"].as<std::string>();
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
        if (s["min_confidence"])        config.strategy.min_confidence = s["min_confidence"].as<double>();
        if (s["min_spread_to_mm"])      config.strategy.min_spread_to_mm = s["min_spread_to_mm"].as<double>();
        if (s["kelly_fraction"])        config.strategy.kelly_fraction = s["kelly_fraction"].as<double>();
        if (s["tick_interval_seconds"]) config.strategy.tick_interval_seconds = s["tick_interval_seconds"].as<int>();
        if (s["settled_max_only"])      config.strategy.settled_max_only = s["settled_max_only"].as<bool>();
    }

    if (auto l = root["logging"]) {
        if (l["level"])   config.logging.level = l["level"].as<std::string>();
        if (l["file"])    config.logging.file = l["file"].as<std::string>();
        if (l["console"]) config.logging.console = l["console"].as<bool>();
    }

    if (auto p = root["paths"]) {
        if (p["data_dir"])         config.paths.data_dir = p["data_dir"].as<std::string>();
        if (p["state_file"])       config.paths.state_file = p["state_file"].as<std::string>();
        if (p["calibration_file"]) config.paths.calibration_file = p["calibration_file"].as<std::string>();
    }

    if (auto f = root["feeds"]) {
        // Nested shape: feeds.bls.api_key / feeds.fred.api_key (matches config.example.yaml).
        if (auto bls = f["bls"]) {
            if (bls["api_key"]) config.feeds.bls_api_key = bls["api_key"].as<std::string>();
        }
        if (auto fred = f["fred"]) {
            if (fred["api_key"]) config.feeds.fred_api_key = fred["api_key"].as<std::string>();
        }
        if (auto om = f["open_meteo"]) {
            if (om["base_url"])      config.feeds.open_meteo_base_url = om["base_url"].as<std::string>();
            if (om["refresh_hours"]) config.feeds.refresh_hours = om["refresh_hours"].as<int>();
        }
    }

    if (auto flt = root["filter"]) {
        if (flt["min_volume"])              config.filter.min_volume = flt["min_volume"].as<int>();
        if (flt["min_spread"])              config.filter.min_spread = flt["min_spread"].as<double>();
        if (flt["max_spread"])              config.filter.max_spread = flt["max_spread"].as<double>();
        if (flt["min_price"])               config.filter.min_price = flt["min_price"].as<double>();
        if (flt["max_price"])               config.filter.max_price = flt["max_price"].as<double>();
        if (flt["skip_fractional_markets"]) config.filter.skip_fractional_markets = flt["skip_fractional_markets"].as<bool>();
    }

    if (auto a = root["alerts"]) {
        if (a["webhook_url"])           config.alerts.webhook_url = a["webhook_url"].as<std::string>();
        if (a["format"])                config.alerts.format = a["format"].as<std::string>();
        if (a["send_kill_switch"])      config.alerts.send_kill_switch = a["send_kill_switch"].as<bool>();
        if (a["send_daily_summary"])    config.alerts.send_daily_summary = a["send_daily_summary"].as<bool>();
        if (a["send_balance_changes"])  config.alerts.send_balance_changes = a["send_balance_changes"].as<bool>();
        if (a["balance_change_pct"])    config.alerts.balance_change_pct = a["balance_change_pct"].as<double>();
    }

    return config;
}

} // namespace trader
