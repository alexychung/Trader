#include "core/config.hpp"
#include "core/logging.hpp"
#include "core/event_bus.hpp"
#include "core/types.hpp"
#include "core/state_store.hpp"
#include "core/staleness_gate.hpp"
#include "core/alert_manager.hpp"
#include "exchange/kalshi/kalshi_exchange.hpp"
#include "exchange/kalshi/order_manager.hpp"
#include "exchange/kalshi/encoding.hpp"  // kalshi_maker_post_price
#include "strategy/kalshi/weather_feed.hpp"
#include "strategy/kalshi/econ_feed.hpp"
#include "strategy/kalshi/data_feed_manager.hpp"
#include "strategy/kalshi/observations.hpp"
#include "strategy/kalshi/probability_engine.hpp"
#include "strategy/kalshi/econ_models.hpp"
#include "strategy/kalshi/edge_detector.hpp"
#include "strategy/kalshi/market_filter.hpp"
#include "strategy/kalshi/event_strategy.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/kalshi/adaptive_sizer.hpp"
#include "strategy/kalshi/probability_calibrator.hpp"
#include "strategy/nba/nba_score_feed.hpp"
#include "strategy/nba/nba_strategy.hpp"
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

using namespace trader;
using namespace trader::kalshi;

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    spdlog::warn("Signal {} received, shutting down...", sig);
    g_running = false;
}

// "Trading day" key based on UTC date — used to detect day rollover for daily reset.
// Kalshi runs 24/7 but settlements cluster around US business hours; midnight UTC is a
// reasonable bucket boundary that avoids resetting in the middle of a busy ET morning.
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
    // CLI parsing: <config_path> [--confirm-live] [--shadow]
    //
    // --shadow: read-only session. Honors the configured mode (demo/prod)
    // for market data, subscriptions, positions, and settlements — but hard-
    // gates every write (place_order / cancel_order) at the exchange layer.
    // Intended for validating the strategy against real prod book dynamics
    // before committing capital. --confirm-live is not required in shadow
    // because no real-money action is possible.
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
    spdlog::info("=== Kalshi Event Trading Bot v0.2.0 ===");
    spdlog::info("Mode: {}", config.mode);
    spdlog::info("Venue: {}", config.venue);

    // Live-mode hard gate: never go live without an explicit per-session CLI
    // flag — unless --shadow is set, in which case no writes can reach the
    // exchange regardless of mode.
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
        spdlog::warn("Use this session to validate strategy behavior against real "
                     "prod books before committing capital.");
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
        // First run — initialize from config.
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
    // Shadow mode must be armed BEFORE exchange.connect(), because connect()
    // may trigger reconciliation paths that surface intended cancels. It's
    // a pure local flag — does not touch the wire.
    exchange.set_shadow_mode(shadow_mode);

    // --- Risk + kill switch + clusters + alerts ---
    RiskManager risk_manager(config.risk);
    risk_manager.set_balance(state.balance);
    KillSwitch kill_switch(risk_manager);
    ClusterLimiter cluster_limiter;

    // Wire exchange fill/settlement events into the risk + cluster views.
    // Without this the pre-trade gate and cluster cap both operate on stale
    // state after the first fill.
    exchange.set_risk_manager(&risk_manager);
    exchange.set_cluster_limiter(&cluster_limiter);

    // Apply the maker-only policy from config. When true (default), every
    // order is submitted post-only regardless of per-order flags — the
    // force-exit path in OrderManager will still queue at the bid but will
    // not cross. When false, Order.post_only is honored; force-exits take
    // liquidity and are booked with taker fees.
    exchange.set_enforce_maker_only(config.risk.maker_only);

    // Notify the kill switch immediately if RiskManager trips its own kill
    // (e.g., daily-loss check in on_settlement). Otherwise the switch only
    // notices on the next main-loop tick, which could be up to
    // tick_interval_seconds later with orders still resting.
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

    // --- Data feeds ---
    WeatherFeed weather_feed;
    EconFeed econ_feed(config.feeds.bls_api_key, config.feeds.fred_api_key);
    if (config.feeds.fred_api_key.empty()) {
        spdlog::warn("FRED api_key not set — FRED endpoints will 401. "
                     "Get a free key at https://fred.stlouisfed.org/docs/api/api_key.html");
    }
    if (config.feeds.bls_api_key.empty()) {
        spdlog::info("BLS api_key not set — using unauthenticated rate limit (~25 queries/day)");
    }
    DataFeedManager feed_manager;
    feed_manager.set_weather_feed(&weather_feed);
    feed_manager.set_econ_feed(&econ_feed);

    // --- Probability models ---
    ProbabilityEngine prob_engine;
    auto weather_model = std::make_shared<WeatherEnsembleModel>();
    auto cpi_model = std::make_shared<CpiModel>();
    auto fed_model = std::make_shared<FedRateModel>();
    prob_engine.register_model("weather", weather_model);
    prob_engine.register_model("economics", cpi_model);
    prob_engine.register_model("fed", fed_model);

    // Station observations: enables the "settled-but-unpriced" edge. The
    // tracker is refreshed every ~5 minutes via the main tick loop below
    // (see `Observations refresh` block). When a station's daily max is
    // locked AND a contract threshold is clearly on one side, the weather
    // model returns near-certain probability instead of the forward
    // ensemble forecast.
    StationObservationsTracker obs_tracker;
    weather_model->set_observations_tracker(&obs_tracker);

    // --- Strategy stack ---
    EdgeDetector::Config edge_cfg;
    edge_cfg.min_edge = config.strategy.min_edge_threshold;
    edge_cfg.min_confidence = config.strategy.min_confidence;
    edge_cfg.kelly_fraction = config.strategy.kelly_fraction;
    edge_cfg.bankroll = state.balance;  // initial bankroll; updated each tick via set_bankroll
    EdgeDetector edge_detector(edge_cfg);

    // Translate YAML filter thresholds into the kalshi::FilterConfig the
    // strategy layer expects. The `allowed_categories` / `blocked_categories`
    // sets stay hard-coded because our routing relies on them — the YAML
    // struct only exposes numeric thresholds that are realistically tuned
    // between demo (thin, relax) and prod (deeper, tighten).
    FilterConfig filter_cfg;
    filter_cfg.min_volume              = config.filter.min_volume;
    filter_cfg.min_spread              = config.filter.min_spread;
    filter_cfg.max_spread              = config.filter.max_spread;
    filter_cfg.min_price               = config.filter.min_price;
    filter_cfg.max_price               = config.filter.max_price;
    filter_cfg.skip_fractional_markets = config.filter.skip_fractional_markets;
    MarketFilter market_filter(filter_cfg);
    CalibrationLogger calibration(config.paths.calibration_file);
    AdaptiveSizer sizer;
    ProbabilityCalibrator prob_calibrator;
    StalenessGate staleness;

    KalshiEventStrategy strategy(prob_engine, edge_detector, market_filter,
                                  risk_manager, calibration, sizer,
                                  prob_calibrator, staleness, cluster_limiter);
    strategy.set_settled_max_only(config.strategy.settled_max_only);
    if (config.strategy.settled_max_only) {
        spdlog::info("Strategy: settled_max_only=ON — ensemble predictions "
                     "will shadow-log only; only post-peak locked observations "
                     "will fire real trades.");
    }

    OrderManager order_manager(exchange);

    // --- NBA strategy (optional, gated on config.nba.enabled) ---
    // Lives alongside the weather strategy; shares the same exchange, risk
    // manager, calibration log, and order placement path. Markets are
    // separated by ticker prefix (KXNBAGAME-* for NBA, KXTEMP*/KXCPI*/etc
    // for weather/econ).
    std::unique_ptr<trader::nba::NbaScoreFeed> nba_score_feed;
    std::unique_ptr<trader::nba::NbaStrategy> nba_strategy;
    if (config.nba.enabled) {
        nba_score_feed = std::make_unique<trader::nba::NbaScoreFeed>();
        trader::nba::NbaStrategy::Config ncfg;
        ncfg.min_edge_threshold           = config.nba.min_edge_threshold;
        ncfg.max_spread                   = config.nba.max_spread;
        ncfg.min_seconds_remaining        = config.nba.min_seconds_remaining;
        ncfg.max_seconds_remaining        = config.nba.max_seconds_remaining;
        ncfg.max_position_per_game_dollars = config.nba.max_position_per_game_dollars;
        ncfg.kelly_fraction               = config.nba.kelly_fraction;
        ncfg.scoreboard_poll_interval     = std::chrono::seconds(config.nba.scoreboard_poll_seconds);
        nba_strategy = std::make_unique<trader::nba::NbaStrategy>(
            risk_manager, calibration, *nba_score_feed, ncfg);
        spdlog::info("NBA strategy: ENABLED (edge≥{:.2f}, ${}/game cap, poll {}s)",
                     ncfg.min_edge_threshold,
                     ncfg.max_position_per_game_dollars,
                     config.nba.scoreboard_poll_seconds);
    } else {
        spdlog::info("NBA strategy: disabled (set nba.enabled=true to activate)");
    }

    // --- Wire feed events to strategy ---
    feed_manager.set_callback([&](const FeedEvent& event) {
        if (event.type == FeedEvent::Type::WeatherEnsemble) {
            weather_model->set_ensemble(event.ensemble);
            staleness.on_feed_update("weather");
        } else if (event.type == FeedEvent::Type::EconSignal) {
            strategy.on_data_signal(event.signal);
        }
    });

    // --- Wire WS market updates to strategy + kill switch heartbeat ---
    exchange.ws().set_on_market_update([&](const WsMarketUpdate& update) {
        MarketUpdate mu;
        mu.ticker = update.ticker;
        mu.yes_bid = update.yes_bid;
        mu.yes_ask = update.yes_ask;
        mu.last_price = update.last_price;
        mu.volume = update.volume;
        strategy.on_market_update(mu);
        if (nba_strategy) nba_strategy->on_market_update(mu);
        kill_switch.on_heartbeat();
    });

    // Surface sustained WS reconnect failures to the alert channel. The
    // client itself keeps retrying (Kalshi outages do recover), but silent
    // retries over a revoked key can hide an account issue for hours.
    exchange.ws().set_on_error([&](const std::string& msg) {
        spdlog::error("WS error: {}", msg);
        alerts.send("WebSocket connection failure", msg);
    });

    spdlog::info("Connecting to Kalshi {}...", config.mode == "live" ? "PRODUCTION" : "DEMO");
    bool connected = exchange.connect();
    if (!connected) {
        spdlog::warn("Exchange connection failed. Will continue and retry; bot runs in offline-safe mode.");
    }

    // Fills channels are account-wide — subscribe once. Ticker subs are
    // added per-market below. Both `fill` (legacy) and `user_orders` (the
    // Feb 3, 2026 replacement) are subscribed during the migration window;
    // on_fill dedups by trade_id so duplicate delivery is harmless.
    exchange.ws().subscribe_fills();
    exchange.ws().subscribe_user_orders();

    // Log account tier + RPS caps. The strategy tick cadence and the
    // cold-start batched order-book fetch both assume ≥5 RPS headroom;
    // Basic tier can be tighter and is worth surfacing so an operator can
    // spot rate-limit-driven stalls without guesswork.
    if (connected) {
        auto limits = exchange.rest().get_account_limits();
        if (!limits.tier.empty() || limits.read_rps > 0) {
            spdlog::info("Kalshi account: tier='{}' read_rps={} write_rps={} max_open_orders={}",
                         limits.tier, limits.read_rps, limits.write_rps, limits.max_open_orders);
        } else {
            spdlog::info("Kalshi /account/limits returned no recognizable fields (fresh account?)");
        }

        // Per-series fee overrides. Most series still use the global default
        // (0.07 / 0.0175); any entries here override the hardcoded formula
        // for EdgeDetector's EV and fee-floor math.
        auto raw_schedule = exchange.rest().get_fee_schedule();
        if (!raw_schedule.empty()) {
            std::unordered_map<std::string, SeriesFeeRates> schedule;
            for (const auto& [series, rates] : raw_schedule) {
                schedule[series] = {rates.maker_rate, rates.taker_rate};
                spdlog::info("Fee override: series='{}' maker={:.4f} taker={:.4f}",
                             series, rates.maker_rate, rates.taker_rate);
            }
            edge_detector.set_fee_schedule(std::move(schedule));
        } else {
            spdlog::info("No per-series fee overrides — using global default rates");
        }
    }

    // Startup state reconciliation: Kalshi's server is the source of truth.
    //
    //   1. seed_positions_from_rest() — pulls the current position for every
    //      open market. Positions from before `last_fill_ts_ms` would
    //      otherwise be invisible and the risk gate would let us stack
    //      orders on top of existing fills.
    //   2. reconcile_fills_since() — replays individual fills so the
    //      calibration layer sees them and the watermark advances. This
    //      would double-count positions that seed(1) already accounts for,
    //      so we only replay fills newer than the seed timestamp.
    //
    // The seed timestamp is captured right before (1) so any fills that
    // arrive between (1) and (2) are still caught by reconcile. This is a
    // narrow window (a few ms in practice) but not zero.
    if (connected) {
        const int64_t seed_ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int seeded = exchange.seed_positions_from_rest();

        // Advance the watermark past fills that the seed already folded in,
        // unless state.json's watermark is newer (meaning we had a tight
        // clean-exit and the seeded positions match what we expect).
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

    // Balance reconciliation: Kalshi's /portfolio/balance is the ground truth.
    // In live mode, a mismatch > $1 is fatal — bail out so an operator can
    // investigate (possibly a settlement we didn't record, an orphaned order,
    // or a state.json corruption). In paper mode, we quietly snap the bot's
    // balance to Kalshi's — demo accounts can drift arbitrarily (leftover
    // fills from prior runs, reserved funds on resting orders, etc.) and
    // refusing to start would block every development session.
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
            // Shadow sessions can't place orders, so a balance mismatch is
            // purely informational. We also don't snap state.balance to the
            // real account — the strategy should size against the configured
            // bankroll ($100), not the full prod balance, so sizing reflects
            // what a real committed-capital session would do.
            if (diff > 1.0) {
                spdlog::warn("SHADOW: Kalshi balance ${:.2f} differs from configured "
                             "bankroll ${:.2f}. Strategy will size against the configured "
                             "bankroll to mirror a real $100 capital session.",
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
    // Start first tick immediately so market catalog + ticker subs warm up before
    // the connectivity kill switch's 60s timer expires.
    auto last_tick = std::chrono::system_clock::now() - tick_interval;
    std::string current_day = utc_day_key(std::chrono::system_clock::now());

    try {
    while (g_running) {
        auto now = std::chrono::system_clock::now();

        // Daily rollover.
        std::string today = utc_day_key(now);
        if (today != current_day) {
            spdlog::info("Day rollover: {} -> {}", current_day, today);
            // Send daily summary BEFORE resetting counters.
            auto brier_real = calibration.brier_score_real();
            alerts.daily_summary(risk_manager.balance(), risk_manager.daily_pnl(),
                                  state.trades_today, brier_real.score);
            risk_manager.reset_daily();
            strategy.reset_daily();
            state.daily_pnl = 0.0;
            state.day_start = now;
            state.trades_today = 0;
            state_store.save(state);
            current_day = today;
        }

        if (kill_switch.check()) {
            spdlog::critical("Kill switch active: {}", kill_switch.reason());
            kill_switch.execute(exchange);
            // Persist current state before pausing for manual reset.
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

        feed_manager.tick();

        // Observations refresh — every 5 minutes, pull recent NWS obs for
        // each station and update the tracker. Enables the settled-max
        // override in WeatherEnsembleModel when the day's peak has passed.
        //
        // Each per-station HTTP call succeeding proves connectivity, so we
        // feed the kill switch's heartbeat inside the loop. Without this,
        // a 25-35s obs-refresh (18 stations × ~1.5s) could starve the kill
        // switch's 60s connectivity timer and false-fire the moment the
        // refresh completes (observed in production 2026-04-21T11:20:36).
        // Also run kill_switch.check() between fetches so a genuine kill
        // trigger (e.g. a daily-loss breach from an in-flight fill) doesn't
        // wait ~30s for the obs loop to exit.
        static Timestamp last_obs_refresh{};  // zero-init → triggers first pass
        if (now - last_obs_refresh >= std::chrono::minutes(5)) {
            int locked = 0;
            for (const auto& st : kalshi_weather_stations()) {
                if (!g_running || kill_switch.is_active()) break;
                if (auto obs = StationObservationsClient::fetch_and_compute(st)) {
                    obs_tracker.set(*obs);
                    if (obs->locked) ++locked;
                    kill_switch.on_heartbeat();  // successful HTTP = connectivity OK
                }
            }
            last_obs_refresh = now;
            spdlog::info("Observations refresh: {} stations tracked, {} locked",
                         obs_tracker.size(), locked);
        }

        if (now - last_tick >= tick_interval) {
            // Market-catalog refresh cadence. The paginated scan walks up to
            // 100 pages × 1000/page ≈ 80s of network I/O per call and 2026-04
            // Kalshi returns ~83k markets globally. Doing it every 60s tick
            // wastes ~80%/60s on network and leaves no gap for obs refresh,
            // kill-switch checks, etc.
            //
            // We don't need fresh catalog every minute. Pricing comes from
            // the WebSocket `ticker` / `orderbook_delta` channels, which are
            // already subscribed per market. The REST catalog is just the
            // "which markets exist" list — new tickers appear at most a few
            // times per day (next-day weather contracts rolling in, CPI
            // release brackets listing). 10 minutes is fast enough to pick
            // those up without hammering the server.
            //
            // We still want ONE refresh on the very first tick so shadows
            // can start generating immediately. The initial last_tick value
            // is (now - tick_interval), so on loop entry `now - last_market_refresh`
            // starts at infinity and falls through on first iteration.
            static Timestamp last_market_refresh{};  // epoch → triggers first call
            constexpr auto kMarketCatalogTtl = std::chrono::minutes(10);
            if (now - last_market_refresh >= kMarketCatalogTtl) {
                //   Weather:    KXTEMP*   (current 2026 format, e.g. KXTEMPNYCH = NYC high)
                //               KXHIGHT*  (legacy IATA-suffixed stations)
                //               KXHIGH*   (older legacy)
                //   Inflation:  KXCPI*    (CPI brackets, core, YoY)
                //               KXPCE*    (PCE if live)
                //   Fed:        KXFEDDISSENT, KXFEDCOMBO  (rate-decision contracts)
                //   Macro:      KXUSNFP*  (Non-Farm Payrolls brackets)
                //               KXJOBLESSCLAIMS*  (weekly jobless claims)
                // Excludes:
                //   INFLATION  → KXHIGHINFLATION (macro, not weather)
                //   MENTION    → KXFEDMENTION (keyword contracts, not rate policy)
                //
                // Prefix audit 2026-04-23: prod catalog_inspect showed
                // KXTEMPNYCH (24 open) and KXUSNFP (15 open) live; legacy
                // KXHIGH* absent. Re-run trader_catalog_inspect.exe when
                // 0/N markets kept persists — format drift is the usual cause.
                std::vector<std::string> prefixes = {
                    "KXTEMP", "KXHIGHT", "KXHIGH", "KXCPI", "KXPCE",
                    "KXFEDDISSENT", "KXFEDCOMBO", "KXUSNFP", "KXJOBLESSCLAIMS"};
                if (nba_strategy) prefixes.push_back("KXNBAGAME");
                exchange.rest().refresh_markets_by_ticker_prefix(
                    prefixes,
                    {"INFLATION", "MENTION"});
                last_market_refresh = now;
            }
            auto& cached = exchange.rest().cached_markets();
            // Any successful REST round-trip proves connectivity — the
            // post-filter cache may legitimately be empty (e.g. weather is
            // off-season on demo) without meaning we've lost the network.
            // Heartbeat unconditionally on a completed tick.
            kill_switch.on_heartbeat();
            std::vector<KalshiMarket> market_list;
            market_list.reserve(cached.size());
            for (const auto& [t, m] : cached) market_list.push_back(m);
            strategy.set_markets(market_list);

            // Hand the NBA subset to the NBA strategy. Cheap (only KXNBAGAME-*)
            // and avoids walking the full cache inside the strategy.
            if (nba_strategy) {
                std::vector<KalshiMarket> nba_markets;
                for (const auto& m : market_list) {
                    if (m.ticker.rfind("KXNBAGAME-", 0) == 0) {
                        nba_markets.push_back(m);
                    }
                }
                nba_strategy->set_markets(nba_markets);
            }

            // Subscribe to tickers for cached markets. WS client dedups by (channel, ticker),
            // so repeated calls across ticks are safe. Cap to bound outbound WS subscribe traffic.
            constexpr size_t kMaxTickerSubs = 50;
            size_t sub_count = 0;
            for (const auto& m : market_list) {
                if (sub_count >= kMaxTickerSubs) break;
                exchange.ws().subscribe_ticker(m.ticker);
                ++sub_count;
            }

            // Update bankroll seen by EdgeDetector (capped to prevent runaway Kelly).
            double effective_bk = sizer.effective_bankroll(risk_manager.balance(), state.starting_bankroll);
            edge_detector.set_bankroll(effective_bk);

            auto signals = strategy.generate_signals();

            // NBA signals are folded into the same emission loop below — same
            // exchange, same risk manager, same order placement.
            if (nba_strategy) {
                auto nba_signals = nba_strategy->generate_signals();
                signals.insert(signals.end(),
                                std::make_move_iterator(nba_signals.begin()),
                                std::make_move_iterator(nba_signals.end()));
            }

            for (const auto& signal : signals) {
                if (kill_switch.is_active()) break;

                Order order;
                order.ticker = signal.ticker;
                order.side = signal.side;
                order.contract_side = signal.contract_side;
                order.quantity = signal.quantity;
                order.post_only = config.risk.maker_only;

                // Price selection. signal.market_price is the taker-reference
                // (yes_ask for YES, 1-yes_bid for NO) used by edge/fee math.
                // If we're post-only, submitting at the taker price would be
                // rejected as "would cross" — so compute a rest-on-book
                // price from the top-of-book snapshot. Falls back to the
                // taker price if the snapshot wasn't populated (older signal
                // paths / tests) since that matches historic behavior.
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
                    // IMPORTANT: do NOT call risk_manager.on_fill or
                    // cluster_limiter.on_fill here. Those callbacks fire from
                    // kalshi_exchange.on_fill when the WS confirms the fill.
                    // Calling them here optimistically double-counts every
                    // fill — the fix is to treat WS + REST reconcile as the
                    // sole source of truth. Pre-trade risk checks will see a
                    // brief "no position yet" window until the fill arrives,
                    // but on 60s tick intervals that's always resolved before
                    // the next iteration.
                    ++state.trades_today;
                    spdlog::info("Order placed: {} {} {}x ${:.4f} ({})",
                                 signal.ticker, signal.contract_side, signal.quantity,
                                 signal.market_price, order_id);
                } else {
                    kill_switch.on_order_error();
                    spdlog::warn("Order failed: {}", signal.ticker);
                }
            }

            // Order lifecycle: cancel stale, reprice on move, force-exit on model flip.
            order_manager.tick(strategy.latest_market_updates(), strategy.current_model_probs());

            // Settlements
            auto settlements = exchange.check_settlements();
            for (const auto& s : settlements) {
                Settlement settl;
                settl.ticker = s.ticker;
                settl.outcome = s.outcome;
                settl.pnl = s.pnl;
                strategy.on_settlement(settl);
                if (nba_strategy) nba_strategy->on_settlement(settl);
                risk_manager.on_settlement(s.ticker, s.pnl);
                cluster_limiter.on_settlement(s.ticker);
                state.cumulative_pnl += s.pnl;
            }

            // Persist updated state every tick.
            double prev_balance = state.balance;
            state.balance = risk_manager.balance();
            state.daily_pnl = risk_manager.daily_pnl();
            state.last_fill_ts_ms = exchange.last_fill_ts_ms();
            state_store.save(state);
            alerts.on_balance_change(prev_balance, state.balance);

            // Status line.
            auto brier = calibration.brier_score();
            auto brier_real = calibration.brier_score_real();
            spdlog::info("Status: Balance ${:.2f} (cap ${:.2f}) | Exposure ${:.2f} | Daily ${:.2f} | "
                         "Real {} (Brier {:.3f}) | Shadows {} (Brier {:.3f}) | Probes/Expl {}",
                         risk_manager.balance(), effective_bk, risk_manager.total_exposure(),
                         risk_manager.daily_pnl(),
                         calibration.real_trades(), brier_real.score,
                         calibration.shadow_trades(), brier.score,
                         strategy.exploration_taken());

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

    // Clean shutdown
    spdlog::info("Shutting down...");
    exchange.disconnect();

    // Final state snapshot.
    state.balance = risk_manager.balance();
    state.daily_pnl = risk_manager.daily_pnl();
    state_store.save(state);

    // Calibration log writes are debounced to 5s during steady-state; flush
    // the tail so any shadows accumulated since the last rewrite land on disk.
    calibration.flush();

    spdlog::info("Disconnected. Final stats:");
    spdlog::info("  Balance: ${:.2f}", risk_manager.balance());
    spdlog::info("  Daily PnL: ${:.2f}", risk_manager.daily_pnl());
    spdlog::info("  Cumulative PnL: ${:.2f}", state.cumulative_pnl);
    spdlog::info("  Real trades: {} | Shadow predictions: {}",
                 calibration.real_trades(), calibration.shadow_trades());
    auto final_brier = calibration.brier_score();
    auto final_brier_real = calibration.brier_score_real();
    spdlog::info("  Brier (all): {:.3f} ({} samples) | (real only): {:.3f} ({} samples)",
                 final_brier.score, final_brier.num_predictions,
                 final_brier_real.score, final_brier_real.num_predictions);
    // Per-source Brier — separates forward-forecast accuracy from the
    // near-certain settled-max predictions. The whole point of tracking
    // them apart is to know after a week whether 0.95-confidence settled
    // overrides are empirically as accurate as claimed (Brier should be
    // ~0.01-0.05 for settled_max vs ~0.18-0.22 for ensemble).
    auto brier_ens  = calibration.brier_score_by_source("ensemble", "weather");
    auto brier_sm   = calibration.brier_score_by_source("settled_max", "weather");
    if (brier_ens.num_predictions > 0) {
        spdlog::info("  Weather ensemble    Brier: {:.3f} ({} samples)",
                     brier_ens.score, brier_ens.num_predictions);
    }
    if (brier_sm.num_predictions > 0) {
        spdlog::info("  Weather settled-max Brier: {:.3f} ({} samples)",
                     brier_sm.score, brier_sm.num_predictions);
    }
    spdlog::info("  Order mgmt: cancelled-stale={} cancelled-reprice={} force-exits={}",
                 order_manager.total_cancelled_stale(),
                 order_manager.total_cancelled_repriced(),
                 order_manager.total_force_exits());
    spdlog::info("=== Shutdown complete ===");
    return 0;
}
