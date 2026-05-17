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
    double min_edge_threshold = 0.08;
    double min_confidence = 0.30;
    double min_spread_to_mm = 0.08;
    double kelly_fraction = 0.25;
    int tick_interval_seconds = 60;
    // When true, only settled-max weather signals (post-peak observations
    // locked, near-certain outcomes) fire real trades. Pre-peak ensemble
    // predictions still shadow-log for Brier calibration but never size
    // positions. This is the conservative live posture — ensemble edge
    // remains unproven at $100 bankroll, settled-max is defensible.
    bool settled_max_only = true;
};

struct LoggingConfig {
    std::string level = "info";
    std::string file = "logs/trader.log";
    bool console = true;
};

struct PathsConfig {
    std::string data_dir = "data";                         // directory for persistent state
    std::string state_file = "data/state.json";            // PersistedState location
    std::string calibration_file = "data/calibration.json"; // CalibrationLogger location
};

struct AlertsConfig {
    std::string webhook_url;          // empty = disabled
    std::string format = "discord";   // discord | slack | generic
    bool send_kill_switch = true;
    bool send_daily_summary = true;
    bool send_balance_changes = true;
    double balance_change_pct = 0.05;
};

// External data-feed credentials. All optional — BLS works without a key at a
// lower rate limit; Open-Meteo and NWS require no key at all. FRED requires
// a key for any meaningful use and will 401 without one.
struct FeedsConfig {
    std::string bls_api_key;   // https://data.bls.gov/registrationEngine/
    std::string fred_api_key;  // https://fred.stlouisfed.org/docs/api/api_key.html
    std::string open_meteo_base_url = "https://ensemble-api.open-meteo.com/v1/ensemble";
    int refresh_hours = 6;     // how stale before we re-pull the weather ensemble
};

// Market-filter thresholds. Mirrors kalshi::FilterConfig but as a plain
// struct Config can load from YAML without pulling in the strategy header.
// `main.cpp` translates this to a kalshi::FilterConfig and hands it to
// MarketFilter. Settings here let you tune the strategy for demo (thin
// books → relax min_volume) vs prod (deeper books → tighten everything)
// without a rebuild.
struct FilterThresholds {
    int min_volume = 50;
    double min_spread = 0.03;    // narrower = illiquid / no edge
    double max_spread = 0.30;    // wider = placeholder book (bid=0/ask=1)
    double min_price = 0.15;     // skip cheap (favorite-longshot bias)
    double max_price = 0.85;     // skip expensive (same reason mirrored)
    bool skip_fractional_markets = false;
};

struct Config {
    std::string venue = "kalshi";
    std::string mode = "paper";
    KalshiConfig kalshi;
    RiskConfig risk;
    StrategyConfig strategy;
    LoggingConfig logging;
    PathsConfig paths;
    AlertsConfig alerts;
    FeedsConfig feeds;
    FilterThresholds filter;

    static Config load(const std::string& path);
};

} // namespace trader
