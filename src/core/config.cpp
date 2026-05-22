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
        if (s["kelly_fraction"])        config.strategy.kelly_fraction = s["kelly_fraction"].as<double>();
        if (s["tick_interval_seconds"]) config.strategy.tick_interval_seconds = s["tick_interval_seconds"].as<int>();
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

    if (auto n = root["nba"]) {
        if (n["enabled"])                       config.nba.enabled = n["enabled"].as<bool>();
        if (n["min_edge_threshold"])            config.nba.min_edge_threshold = n["min_edge_threshold"].as<double>();
        if (n["max_edge_threshold"])            config.nba.max_edge_threshold = n["max_edge_threshold"].as<double>();
        if (n["max_spread"])                    config.nba.max_spread = n["max_spread"].as<double>();
        if (n["min_seconds_remaining"])         config.nba.min_seconds_remaining = n["min_seconds_remaining"].as<int>();
        if (n["max_seconds_remaining"])         config.nba.max_seconds_remaining = n["max_seconds_remaining"].as<int>();
        if (n["max_position_per_game_dollars"]) config.nba.max_position_per_game_dollars = n["max_position_per_game_dollars"].as<double>();
        if (n["kelly_fraction"])                config.nba.kelly_fraction = n["kelly_fraction"].as<double>();
        if (n["scoreboard_poll_seconds"])       config.nba.scoreboard_poll_seconds = n["scoreboard_poll_seconds"].as<int>();
        if (n["min_abs_score_diff"])            config.nba.min_abs_score_diff = n["min_abs_score_diff"].as<int>();
        if (n["max_uncertain_wp"])              config.nba.max_uncertain_wp = n["max_uncertain_wp"].as<double>();
        if (n["min_strong_wp"])                 config.nba.min_strong_wp = n["min_strong_wp"].as<double>();
        if (n["min_lot_size"])                  config.nba.min_lot_size = n["min_lot_size"].as<int>();
        if (n["quote_jitter_pct"])              config.nba.quote_jitter_pct = n["quote_jitter_pct"].as<double>();
        if (n["min_clv_edge"])                  config.nba.min_clv_edge = n["min_clv_edge"].as<double>();
        if (n["min_market_volume"])             config.nba.min_market_volume = n["min_market_volume"].as<int>();
        if (n["default_pregame_spread"])        config.nba.default_pregame_spread = n["default_pregame_spread"].as<double>();
    }

    if (auto r = root["resolution_lag"]) {
        if (r["enabled"])                       config.resolution_lag.enabled = r["enabled"].as<bool>();
        if (r["cutoff_clock_seconds"])          config.resolution_lag.cutoff_clock_seconds = r["cutoff_clock_seconds"].as<int>();
        if (r["cutoff_lead_points"])            config.resolution_lag.cutoff_lead_points = r["cutoff_lead_points"].as<int>();
        if (r["include_status_final"])          config.resolution_lag.include_status_final = r["include_status_final"].as<bool>();
        if (r["min_entry_price"])               config.resolution_lag.min_entry_price = r["min_entry_price"].as<double>();
        if (r["max_entry_price"])               config.resolution_lag.max_entry_price = r["max_entry_price"].as<double>();
        if (r["max_position_per_game_dollars"]) config.resolution_lag.max_position_per_game_dollars = r["max_position_per_game_dollars"].as<double>();
        if (r["min_lot_size"])                  config.resolution_lag.min_lot_size = r["min_lot_size"].as<int>();
        if (r["min_market_volume"])             config.resolution_lag.min_market_volume = r["min_market_volume"].as<int>();
    }

    if (auto f = root["feeds"]) {
        if (f["oddsapi_key"])                   config.feeds.oddsapi_key = f["oddsapi_key"].as<std::string>();
        if (f["espn_injury_url"])               config.feeds.espn_injury_url = f["espn_injury_url"].as<std::string>();
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
