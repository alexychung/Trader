#include "core/config.hpp"
#include "core/logging.hpp"
#include "core/event_bus.hpp"
#include "core/types.hpp"
#include "core/state_store.hpp"
#include "core/alert_manager.hpp"
#include "exchange/kalshi/kalshi_exchange.hpp"
#include "exchange/kalshi/order_manager.hpp"
#include "exchange/kalshi/encoding.hpp"  // kalshi_maker_post_price
#include "feeds/injury_news_feed.hpp"
#include "feeds/sharp_book_provider.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/nba/nba_score_feed.hpp"
#include "strategy/nba/nba_strategy.hpp"
#include "strategy/nba/resolution_lag_strategy.hpp"
#include "risk/risk_manager.hpp"
#include "risk/kill_switch.hpp"
#include "risk/cluster_limiter.hpp"

#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>
#include <ctime>
#include <string>
#include <cmath>
#include <unordered_set>
#include <vector>

using namespace trader;
using namespace trader::kalshi;

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    spdlog::warn("Signal {} received, shutting down...", sig);
    g_running = false;
}

// "Trading day" key based on UTC date — used to detect day rollover for daily reset.
static std::string utc_day_key(Timestamp ts) {
    auto t = std::chrono::system_clock::to_time_t(ts);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_utc);
    return buf;
}

int main(int argc, char* argv[]) {
    // CLI: <config_path> [--confirm-live] [--shadow]
    std::string config_path = "config/config.yaml";
    bool confirm_live = false;
    bool shadow_mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--confirm-live") confirm_live = true;
        else if (arg == "--shadow") shadow_mode = true;
        else if (arg.rfind("--", 0) == 0) {
            std::cerr << "Unknown flag: " << arg << std::endl;
            return 1;
        } else {
            config_path = arg;
        }
    }

    Config config;
    try {
        config = Config::load(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        std::cerr << "Usage: trader [config.yaml] [--confirm-live]" << std::endl;
        std::cerr << "Copy config/config.example.yaml to config/config.yaml and configure." << std::endl;
        return 1;
    }

    init_logging(config.logging.level, config.logging.file, config.logging.console);
    spdlog::info("=== Kalshi NBA Trading Bot v0.3.0 ===");
    spdlog::info("Mode: {}", config.mode);
    spdlog::info("Venue: {}", config.venue);

    if (config.mode == "live" && !confirm_live && !shadow_mode) {
        spdlog::critical("Mode=live requires --confirm-live CLI flag for this session. "
                         "(Pass --shadow to run prod read-only without confirming.) Refusing to start.");
        std::cerr << "ERROR: live mode requires --confirm-live flag, "
                     "or --shadow for read-only prod sessions. Refusing to start." << std::endl;
        return 2;
    }
    if (confirm_live && config.mode != "live") {
        spdlog::warn("--confirm-live ignored: config mode is '{}'", config.mode);
    }
    if (shadow_mode) {
        spdlog::warn("=== SHADOW MODE ENABLED ===");
        spdlog::warn("All write calls to the exchange (place_order / cancel_order) "
                     "will be suppressed at the exchange layer and only logged.");
        spdlog::warn("Market data, positions, fills, and settlements still flow through normally.");
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // --- Persistent state ---
    StateStore state_store(config.paths.state_file);
    PersistedState state;
    if (auto loaded = state_store.load()) {
        state = *loaded;
        spdlog::info("State loaded: balance ${:.2f} (start ${:.2f}), daily PnL ${:.2f}, cumulative PnL ${:.2f}",
                     state.balance, state.starting_bankroll, state.daily_pnl, state.cumulative_pnl);
    } else {
        state.balance = 100.0;
        state.starting_bankroll = 100.0;
        state.day_start = std::chrono::system_clock::now();
        spdlog::info("No prior state. Starting fresh: balance ${:.2f}", state.balance);
        state_store.save(state);
    }

    // --- Exchange ---
    KalshiAuth auth;
    if (!config.kalshi.api_key_file.empty()) {
        if (!auth.load_key(config.kalshi.api_key_file)) {
            spdlog::warn("Could not load API key from {}. Running in read-only mode.",
                         config.kalshi.api_key_file);
        }
    }
    auth.set_api_key_id(config.kalshi.api_key_id);
    WsConfig ws_cfg;
    ws_cfg.enable_live = true;
    KalshiExchange exchange(config.kalshi.api_base, config.kalshi.ws_url, auth, ws_cfg);
    exchange.set_shadow_mode(shadow_mode);

    // --- Risk + kill switch + clusters + alerts ---
    RiskManager risk_manager(config.risk);
    risk_manager.set_balance(state.balance);
    KillSwitch kill_switch(risk_manager);
    ClusterLimiter cluster_limiter;

    exchange.set_risk_manager(&risk_manager);
    exchange.set_cluster_limiter(&cluster_limiter);
    exchange.set_enforce_maker_only(config.risk.maker_only);

    risk_manager.set_on_kill([&kill_switch](const std::string& reason) {
        kill_switch.trigger(reason);
    });

    AlertManager::Config alert_cfg;
    alert_cfg.webhook_url = config.alerts.webhook_url;
    alert_cfg.format = (config.alerts.format == "slack") ? AlertManager::Format::Slack
                       : (config.alerts.format == "generic") ? AlertManager::Format::Generic
                       : AlertManager::Format::Discord;
    alert_cfg.send_kill_switch = config.alerts.send_kill_switch;
    alert_cfg.send_daily_summary = config.alerts.send_daily_summary;
    alert_cfg.send_balance_changes = config.alerts.send_balance_changes;
    alert_cfg.balance_change_pct = config.alerts.balance_change_pct;
    AlertManager alerts(alert_cfg);

    CalibrationLogger calibration(config.paths.calibration_file);
    OrderManager order_manager(exchange);

    // --- NBA strategy ---
    if (!config.nba.enabled) {
        spdlog::critical("NBA strategy is disabled in config (nba.enabled=false). "
                         "This build has no other strategies. Refusing to start.");
        std::cerr << "ERROR: nba.enabled is false. Set it to true in config.yaml." << std::endl;
        return 5;
    }

    trader::nba::NbaScoreFeed nba_score_feed;
    trader::nba::NbaStrategy::Config ncfg;
    ncfg.min_edge_threshold            = config.nba.min_edge_threshold;
    ncfg.max_spread                    = config.nba.max_spread;
    ncfg.min_seconds_remaining         = config.nba.min_seconds_remaining;
    ncfg.max_seconds_remaining         = config.nba.max_seconds_remaining;
    ncfg.max_position_per_game_dollars = config.nba.max_position_per_game_dollars;
    ncfg.kelly_fraction                = config.nba.kelly_fraction;
    ncfg.scoreboard_poll_interval      = std::chrono::seconds(config.nba.scoreboard_poll_seconds);
    ncfg.min_abs_score_diff            = config.nba.min_abs_score_diff;
    ncfg.max_uncertain_wp              = config.nba.max_uncertain_wp;
    ncfg.min_strong_wp                 = config.nba.min_strong_wp;
    ncfg.min_lot_size                  = config.nba.min_lot_size;
    ncfg.quote_jitter_pct              = config.nba.quote_jitter_pct;
    ncfg.min_clv_edge                  = config.nba.min_clv_edge;
    ncfg.min_market_volume             = config.nba.min_market_volume;
    ncfg.default_pregame_spread        = config.nba.default_pregame_spread;
    trader::nba::NbaStrategy nba_strategy(risk_manager, calibration, nba_score_feed, ncfg);

    // External-data feed stubs (Null implementations by default). Replace
    // with real providers when ready (see src/feeds/*.hpp for instructions).
    trader::feeds::NullSharpBookProvider sharp_book;
    trader::feeds::NullInjuryNewsFeed injury_feed;
    nba_strategy.set_sharp_book_provider(&sharp_book);
    nba_strategy.set_injury_feed(&injury_feed);

    // Resolution-lag arb strategy (2026-05-20). Runs alongside NbaStrategy
    // on the same KXNBAGAME-* universe; the two strategies target
    // non-overlapping game phases (NbaStrategy = mid-Q3 to mid-Q4 high-conf;
    // ResolutionLag = end-of-Q4 blowout or settled-but-untraded). Both
    // share RiskManager so per-ticker dedup is global.
    trader::nba::ResolutionLagStrategy::Config rcfg;
    rcfg.cutoff_clock_seconds         = config.resolution_lag.cutoff_clock_seconds;
    rcfg.cutoff_lead_points           = config.resolution_lag.cutoff_lead_points;
    rcfg.include_status_final         = config.resolution_lag.include_status_final;
    rcfg.min_entry_price              = config.resolution_lag.min_entry_price;
    rcfg.max_entry_price              = config.resolution_lag.max_entry_price;
    rcfg.max_position_per_game_dollars = config.resolution_lag.max_position_per_game_dollars;
    rcfg.min_lot_size                 = config.resolution_lag.min_lot_size;
    rcfg.min_market_volume            = config.resolution_lag.min_market_volume;
    trader::nba::ResolutionLagStrategy resolution_lag(risk_manager, calibration, rcfg);

    spdlog::info("NBA strategy: edge≥{:.2f}, ${}/game cap, Kelly fraction {:.2f}, poll {}s, "
                 "high-conf gate: |lead|≥{} AND wp∉[{:.2f},{:.2f}], "
                 "min_lot={}, jitter±{:.0f}%, CLV gate via {}",
                 ncfg.min_edge_threshold,
                 ncfg.max_position_per_game_dollars,
                 ncfg.kelly_fraction,
                 config.nba.scoreboard_poll_seconds,
                 ncfg.min_abs_score_diff,
                 ncfg.max_uncertain_wp,
                 ncfg.min_strong_wp,
                 ncfg.min_lot_size,
                 ncfg.quote_jitter_pct * 100.0,
                 sharp_book.name());
    if (config.resolution_lag.enabled) {
        spdlog::info("ResolutionLag strategy: ENABLED, entry window [{:.2f},{:.2f}], "
                     "trigger: Q4 ≤{}s & |lead|≥{} OR final, ${}/game cap, min_lot={}",
                     rcfg.min_entry_price, rcfg.max_entry_price,
                     rcfg.cutoff_clock_seconds, rcfg.cutoff_lead_points,
                     rcfg.max_position_per_game_dollars, rcfg.min_lot_size);
    }

    // --- Wire WS market updates to NBA strategy + kill switch heartbeat ---
    exchange.ws().set_on_market_update([&](const WsMarketUpdate& update) {
        MarketUpdate mu;
        mu.ticker = update.ticker;
        mu.yes_bid = update.yes_bid;
        mu.yes_ask = update.yes_ask;
        mu.last_price = update.last_price;
        mu.volume = update.volume;
        nba_strategy.on_market_update(mu);
        if (config.resolution_lag.enabled) {
            resolution_lag.on_market_update(mu);
        }
        kill_switch.on_heartbeat();
    });

    exchange.ws().set_on_error([&](const std::string& msg) {
        spdlog::error("WS error: {}", msg);
        alerts.send("WebSocket connection failure", msg);
    });

    spdlog::info("Connecting to Kalshi {}...", config.mode == "live" ? "PRODUCTION" : "DEMO");
    bool connected = exchange.connect();
    if (!connected) {
        spdlog::warn("Exchange connection failed. Will continue and retry; bot runs in offline-safe mode.");
    }

    exchange.ws().subscribe_fills();
    exchange.ws().subscribe_user_orders();

    if (connected) {
        auto limits = exchange.rest().get_account_limits();
        if (!limits.tier.empty() || limits.read_rps > 0) {
            spdlog::info("Kalshi account: tier='{}' read_rps={} write_rps={} max_open_orders={}",
                         limits.tier, limits.read_rps, limits.write_rps, limits.max_open_orders);
        } else {
            spdlog::info("Kalshi /account/limits returned no recognizable fields (fresh account?)");
        }
    }

    // Startup state reconciliation.
    if (connected) {
        const int64_t seed_ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int seeded = exchange.seed_positions_from_rest();

        int64_t reconcile_from = std::max(state.last_fill_ts_ms, seed_ts_ms);
        if (seeded > 0 && reconcile_from > state.last_fill_ts_ms) {
            spdlog::info("Advancing fill watermark after position seed: {} -> {}",
                         state.last_fill_ts_ms, reconcile_from);
        }

        int64_t new_watermark = exchange.reconcile_fills_since(reconcile_from);
        if (new_watermark != state.last_fill_ts_ms) {
            state.last_fill_ts_ms = new_watermark;
            state_store.save(state);
        }
    }

    // Balance reconciliation. In live, mismatch > $1 is fatal. Shadow & paper
    // tolerate the gap (shadow logs it; paper snaps to Kalshi).
    if (connected) {
        double kalshi_balance = exchange.get_balance();
        double expected = state.balance;
        double diff = std::abs(kalshi_balance - expected);
        if (config.mode == "live" && !shadow_mode) {
            if (diff > 1.0) {
                spdlog::critical("Live balance reconciliation FAILED. Kalshi=${:.2f}, expected=${:.2f}. Refusing to start.",
                                 kalshi_balance, expected);
                std::cerr << "ERROR: Kalshi balance ($" << kalshi_balance
                          << ") differs from expected ($" << expected
                          << ") by more than $1. Adjust state.json or investigate." << std::endl;
                exchange.disconnect();
                return 3;
            }
            spdlog::info("Live balance reconciliation OK: Kalshi=${:.2f}", kalshi_balance);
        } else if (config.mode == "live" && shadow_mode) {
            if (diff > 1.0) {
                spdlog::warn("SHADOW: Kalshi balance ${:.2f} differs from configured "
                             "bankroll ${:.2f}. Strategy will size against the configured "
                             "bankroll to mirror a real capital session.",
                             kalshi_balance, expected);
            }
        } else {
            if (diff > 0.01) {
                spdlog::warn("Paper balance drift: state.json=${:.2f}, Kalshi=${:.2f}. Snapping to Kalshi.",
                             expected, kalshi_balance);
            }
            state.balance = kalshi_balance;
            risk_manager.set_balance(kalshi_balance);
        }
    }

    spdlog::info("Starting main loop (tick interval: {}s)", config.strategy.tick_interval_seconds);
    auto tick_interval = std::chrono::seconds(config.strategy.tick_interval_seconds);
    auto last_tick = std::chrono::system_clock::now() - tick_interval;
    std::string current_day = utc_day_key(std::chrono::system_clock::now());

    try {
    while (g_running) {
        auto now = std::chrono::system_clock::now();

        std::string today = utc_day_key(now);
        if (today != current_day) {
            spdlog::info("Day rollover: {} -> {}", current_day, today);
            auto brier_real = calibration.brier_score_real();
            alerts.daily_summary(risk_manager.balance(), risk_manager.daily_pnl(),
                                  state.trades_today, brier_real.score);
            risk_manager.reset_daily();
            state.daily_pnl = 0.0;
            state.day_start = now;
            state.trades_today = 0;
            state_store.save(state);
            current_day = today;
        }

        if (kill_switch.check()) {
            spdlog::critical("Kill switch active: {}", kill_switch.reason());
            kill_switch.execute(exchange);
            state.balance = risk_manager.balance();
            state.daily_pnl = risk_manager.daily_pnl();
            state.last_fill_ts_ms = exchange.last_fill_ts_ms();
            state_store.save(state);
            alerts.on_kill_switch(kill_switch.reason(), state.balance, state.daily_pnl);
            while (g_running && kill_switch.is_active()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        if (now - last_tick >= tick_interval) {
            // Market-catalog refresh. NBA-only — KXNBAGAME-* prefix. Cached
            // markets are filtered to that prefix before being handed to the
            // NBA strategy.
            static Timestamp last_market_refresh{};  // epoch → triggers first call
            constexpr auto kMarketCatalogTtl = std::chrono::minutes(10);
            if (now - last_market_refresh >= kMarketCatalogTtl) {
                // Server-side series filter — far cheaper than walking the
                // full catalog and reliable on prod where the open-markets
                // list exceeds the page cap.
                exchange.rest().refresh_markets_by_series("KXNBAGAME");
                last_market_refresh = now;
            }
            auto& cached = exchange.rest().cached_markets();
            kill_switch.on_heartbeat();

            std::vector<KalshiMarket> nba_markets;
            nba_markets.reserve(cached.size());
            for (const auto& [t, m] : cached) {
                if (m.ticker.rfind("KXNBAGAME-", 0) == 0) {
                    nba_markets.push_back(m);
                }
            }
            nba_strategy.set_markets(nba_markets);
            if (config.resolution_lag.enabled) {
                resolution_lag.set_markets(nba_markets);
            }

            // Subscribe to NBA tickers we haven't already subscribed to. WS
            // client dedups by (channel, ticker) so repeated calls are safe.
            constexpr size_t kMaxTickerSubs = 50;
            size_t sub_count = 0;
            for (const auto& m : nba_markets) {
                if (sub_count >= kMaxTickerSubs) break;
                exchange.ws().subscribe_ticker(m.ticker);
                ++sub_count;
            }

            auto signals = nba_strategy.generate_signals();

            // ResolutionLag uses the same scoreboard snapshots that NbaStrategy
            // just refreshed. We expose them via getter for cross-strategy reuse.
            if (config.resolution_lag.enabled) {
                resolution_lag.set_snapshots(nba_strategy.last_snapshots());
                auto rl_signals = resolution_lag.generate_signals();
                signals.insert(signals.end(),
                                std::make_move_iterator(rl_signals.begin()),
                                std::make_move_iterator(rl_signals.end()));
            }

            // Cross-strategy dedup (fix #4, 2026-05-20): risk_manager's
            // position_quantity() only counts FILLED contracts. If NbaStrategy
            // and ResolutionLagStrategy fire on the same ticker the same tick,
            // both can pass per-strategy dedup before the first order's fill
            // arrives via WS. Track tickers we've placed THIS TICK and skip
            // duplicates here at the placement boundary. Cleared every tick.
            std::unordered_set<std::string> placed_this_tick;
            for (const auto& signal : signals) {
                if (kill_switch.is_active()) break;
                if (placed_this_tick.count(signal.ticker) > 0) {
                    spdlog::debug("Dedup: skip {} — already placed this tick by sibling strategy",
                                  signal.ticker);
                    continue;
                }

                Order order;
                order.ticker = signal.ticker;
                order.side = signal.side;
                order.contract_side = signal.contract_side;
                order.quantity = signal.quantity;
                order.post_only = config.risk.maker_only;

                if (order.post_only
                    && (signal.yes_bid_snapshot > 0.0
                        || signal.yes_ask_snapshot > 0.0)) {
                    order.price = kalshi_maker_post_price(
                        signal.contract_side,
                        signal.yes_bid_snapshot,
                        signal.yes_ask_snapshot);
                } else {
                    order.price = signal.market_price;
                }

                auto order_id = exchange.place_order(order);
                if (!order_id.empty()) {
                    kill_switch.on_order_success();
                    ++state.trades_today;
                    placed_this_tick.insert(signal.ticker);
                    spdlog::info("Order placed: {} {} {}x ${:.4f} ({})",
                                 signal.ticker, signal.contract_side, signal.quantity,
                                 signal.market_price, order_id);
                } else {
                    kill_switch.on_order_error();
                    spdlog::warn("Order failed: {}", signal.ticker);
                }
            }

            // Settlements
            auto settlements = exchange.check_settlements();
            for (const auto& s : settlements) {
                Settlement settl;
                settl.ticker = s.ticker;
                settl.outcome = s.outcome;
                settl.pnl = s.pnl;
                nba_strategy.on_settlement(settl);
                if (config.resolution_lag.enabled) {
                    resolution_lag.on_settlement(settl);
                }
                risk_manager.on_settlement(s.ticker, s.pnl);
                cluster_limiter.on_settlement(s.ticker);
                state.cumulative_pnl += s.pnl;
            }

            double prev_balance = state.balance;
            state.balance = risk_manager.balance();
            state.daily_pnl = risk_manager.daily_pnl();
            state.last_fill_ts_ms = exchange.last_fill_ts_ms();
            state_store.save(state);
            alerts.on_balance_change(prev_balance, state.balance);

            auto brier_nba = calibration.brier_score_by_source("arcsine", "nba");
            spdlog::info("Status: Balance ${:.2f} | Exposure ${:.2f} | Daily ${:.2f} | "
                         "Real {} (NBA Brier {:.3f} / {} samples) | Live games {}",
                         risk_manager.balance(),
                         risk_manager.total_exposure(),
                         risk_manager.daily_pnl(),
                         calibration.real_trades(),
                         brier_nba.score, brier_nba.num_predictions,
                         nba_strategy.live_games());

            last_tick = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    } catch (const std::exception& e) {
        spdlog::critical("Unhandled exception in main loop: {}", e.what());
        try {
            state.balance = risk_manager.balance();
            state.daily_pnl = risk_manager.daily_pnl();
            state.last_fill_ts_ms = exchange.last_fill_ts_ms();
            state_store.save(state);
            exchange.disconnect();
        } catch (...) {}
        return 4;
    }

    spdlog::info("Shutting down...");
    exchange.disconnect();

    state.balance = risk_manager.balance();
    state.daily_pnl = risk_manager.daily_pnl();
    state_store.save(state);

    calibration.flush();

    spdlog::info("Disconnected. Final stats:");
    spdlog::info("  Balance: ${:.2f}", risk_manager.balance());
    spdlog::info("  Daily PnL: ${:.2f}", risk_manager.daily_pnl());
    spdlog::info("  Cumulative PnL: ${:.2f}", state.cumulative_pnl);
    spdlog::info("  Real trades: {} | Shadow predictions: {}",
                 calibration.real_trades(), calibration.shadow_trades());
    auto final_brier_real = calibration.brier_score_real();
    auto final_brier_nba = calibration.brier_score_by_source("arcsine", "nba");
    spdlog::info("  Brier (real all): {:.3f} ({} samples)",
                 final_brier_real.score, final_brier_real.num_predictions);
    if (final_brier_nba.num_predictions > 0) {
        spdlog::info("  NBA arcsine Brier: {:.3f} ({} samples)",
                     final_brier_nba.score, final_brier_nba.num_predictions);
    }
    spdlog::info("=== Shutdown complete ===");
    return 0;
}
