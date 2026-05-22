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
    double kelly_fraction = 0.25;
    int tick_interval_seconds = 60;
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

// NBA in-game trading config. The bot's sole strategy after the weather/econ
// pivot. Default thresholds reflect the v1 arcsine "safe-lead" strategy: only
// trades during the back half of regulation, on near-certain garbage-time or
// comeback mispricings. Tune via paper trading.
struct NbaConfig {
    bool enabled = false;
    // Post-2026-05-20 semantic: edge AFTER costs above target price (yes_ask
    // worst case), not edge above mid. See NbaStrategy::Config docstring.
    double min_edge_threshold = 0.02;
    double max_spread = 0.10;
    int min_seconds_remaining = 60;            // skip final minute (formula breaks)
    int max_seconds_remaining = 1440;          // skip first half (talent-gap dominates)
    double max_position_per_game_dollars = 50.0;
    double kelly_fraction = 0.25;
    int scoreboard_poll_seconds = 5;           // cdn.nba.com refresh cadence

    // High-confidence gate (improved-directional v2). Trades only fire when
    // |home_lead| >= min_abs_score_diff AND (home_wp outside [max_uncertain_wp,
    // min_strong_wp]). Arcsine is most accurate when one side dominates; tight
    // games produce noisy probabilities that get picked off.
    int min_abs_score_diff = 8;
    double max_uncertain_wp = 0.30;
    double min_strong_wp = 0.70;

    // 2026-05-20 additions (research-driven improvements).
    int min_lot_size = 5;            // skip orders smaller than this (fee ceil)
    double quote_jitter_pct = 0.20;  // ±jitter on poll cadence (anti-HFT)
    double min_clv_edge = 0.02;      // min Kalshi-vs-Pinnacle drift (mid below sharp fair)
    int min_market_volume = 100;     // skip markets with low total volume (stale-book guard)
    double default_pregame_spread = 0.0;  // points, positive = home favored
};

// Resolution-lag arb strategy config (new strategy 2026-05-20). See
// src/strategy/nba/resolution_lag_strategy.hpp for the thesis.
struct ResolutionLagConfig {
    bool enabled = true;
    int cutoff_clock_seconds = 120;
    int cutoff_lead_points = 15;
    bool include_status_final = true;
    double min_entry_price = 0.94;
    double max_entry_price = 0.99;
    double max_position_per_game_dollars = 25.0;
    int min_lot_size = 5;
    int min_market_volume = 50;
};

// Optional external-data-feed credentials. Used by:
//   - SharpBookProvider (Pinnacle CLV gate, #1)
//   - InjuryNewsFeed (#3)
// Both default to NullProvider stubs when no credentials are supplied,
// keeping the bot functional in standalone mode.
struct FeedsConfig {
    std::string oddsapi_key;       // https://the-odds-api.com — 500 req/mo free
    std::string espn_injury_url =
        "https://www.espn.com/nba/injuries";  // public HTML, no key
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
    NbaConfig nba;
    ResolutionLagConfig resolution_lag;
    FeedsConfig feeds;

    static Config load(const std::string& path);
};

} // namespace trader
