#include "core/config.hpp"
#include "core/logging.hpp"
#include "core/event_bus.hpp"
#include "core/types.hpp"
#include "exchange/kalshi/kalshi_exchange.hpp"
#include "strategy/kalshi/weather_feed.hpp"
#include "strategy/kalshi/econ_feed.hpp"
#include "strategy/kalshi/data_feed_manager.hpp"
#include "strategy/kalshi/probability_engine.hpp"
#include "strategy/kalshi/econ_models.hpp"
#include "strategy/kalshi/edge_detector.hpp"
#include "strategy/kalshi/market_filter.hpp"
#include "strategy/kalshi/event_strategy.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "risk/risk_manager.hpp"
#include "risk/kill_switch.hpp"

#include <spdlog/spdlog.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>

using namespace trader;
using namespace trader::kalshi;

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    spdlog::warn("Signal {} received, shutting down...", sig);
    g_running = false;
}

int main(int argc, char* argv[]) {
    // 1. Load config
    std::string config_path = (argc > 1) ? argv[1] : "config/config.yaml";
    Config config;
    try {
        config = Config::load(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        std::cerr << "Usage: trader [config.yaml]" << std::endl;
        std::cerr << "Copy config/config.example.yaml to config/config.yaml and configure." << std::endl;
        return 1;
    }

    // 2. Initialize logging
    init_logging(config.logging.level, config.logging.file, config.logging.console);
    spdlog::info("=== Kalshi Event Trading Bot v0.1.0 ===");
    spdlog::info("Mode: {}", config.mode);
    spdlog::info("Venue: {}", config.venue);

    // 3. Signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 4. Initialize exchange
    KalshiAuth auth;
    if (!config.kalshi.api_key_file.empty()) {
        if (!auth.load_key(config.kalshi.api_key_file)) {
            spdlog::warn("Could not load API key from {}. Running in read-only mode.",
                         config.kalshi.api_key_file);
        }
    }
    auth.set_api_key_id(config.kalshi.api_key_id);

    KalshiExchange exchange(config.kalshi.api_base, config.kalshi.ws_url, auth);

    // 5. Initialize risk manager
    RiskManager risk_manager(config.risk);
    risk_manager.set_balance(config.risk.max_total_exposure + config.risk.max_daily_loss);

    KillSwitch kill_switch(risk_manager);

    // 6. Initialize data feeds
    WeatherFeed weather_feed;
    EconFeed econ_feed;
    DataFeedManager feed_manager;
    feed_manager.set_weather_feed(&weather_feed);
    feed_manager.set_econ_feed(&econ_feed);

    // 7. Initialize probability engine
    ProbabilityEngine prob_engine;
    auto weather_model = std::make_shared<WeatherEnsembleModel>();
    auto cpi_model = std::make_shared<CpiModel>();
    auto fed_model = std::make_shared<FedRateModel>();
    prob_engine.register_model("weather", weather_model);
    prob_engine.register_model("economics", cpi_model);
    prob_engine.register_model("fed", fed_model);

    // 8. Initialize strategy
    EdgeDetector::Config edge_cfg;
    edge_cfg.min_edge = config.strategy.min_edge_threshold;
    edge_cfg.kelly_fraction = config.strategy.kelly_fraction;
    edge_cfg.bankroll = risk_manager.balance();
    EdgeDetector edge_detector(edge_cfg);

    MarketFilter market_filter;
    CalibrationLogger calibration;

    KalshiEventStrategy strategy(prob_engine, edge_detector, market_filter,
                                  risk_manager, calibration);

    // Wire feed events to strategy
    feed_manager.set_callback([&](const FeedEvent& event) {
        if (event.type == FeedEvent::Type::WeatherEnsemble) {
            weather_model->set_ensemble(event.ensemble);
        } else if (event.type == FeedEvent::Type::EconSignal) {
            strategy.on_data_signal(event.signal);
        }
    });

    // Wire WS updates to strategy
    exchange.ws().set_on_market_update([&](const WsMarketUpdate& update) {
        MarketUpdate mu;
        mu.ticker = update.ticker;
        mu.yes_bid = update.yes_bid;
        mu.yes_ask = update.yes_ask;
        mu.last_price = update.last_price;
        mu.volume = update.volume;
        strategy.on_market_update(mu);
        kill_switch.on_heartbeat();
    });

    // 9. Connect exchange
    spdlog::info("Connecting to Kalshi {}...", config.mode == "live" ? "PRODUCTION" : "DEMO");
    if (!exchange.connect()) {
        spdlog::warn("Exchange connection failed. Running in offline mode.");
    }

    // 10. Main event loop
    spdlog::info("Starting main loop (tick interval: {}s)", config.strategy.tick_interval_seconds);
    auto tick_interval = std::chrono::seconds(config.strategy.tick_interval_seconds);
    auto last_tick = std::chrono::system_clock::now();

    while (g_running) {
        auto now = std::chrono::system_clock::now();

        // Check kill switch
        if (kill_switch.check()) {
            spdlog::critical("Kill switch active: {}", kill_switch.reason());
            kill_switch.execute(exchange);
            // Wait for manual reset
            while (g_running && kill_switch.is_active()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        // Refresh data feeds (on schedule)
        feed_manager.tick();

        // Strategy tick (on interval)
        if (now - last_tick >= tick_interval) {
            // Refresh market catalog
            exchange.rest().refresh_market_cache("weather");
            auto& cached = exchange.rest().cached_markets();
            std::vector<KalshiMarket> market_list;
            for (const auto& [t, m] : cached) {
                market_list.push_back(m);
            }
            strategy.set_markets(market_list);

            // Generate signals
            auto signals = strategy.generate_signals();

            // Execute signals
            for (const auto& signal : signals) {
                if (kill_switch.is_active()) break;

                Order order;
                order.ticker = signal.ticker;
                order.side = signal.side;
                order.contract_side = signal.contract_side;
                order.price = signal.market_price;
                order.quantity = signal.quantity;
                order.post_only = true;

                auto order_id = exchange.place_order(order);
                if (!order_id.empty()) {
                    kill_switch.on_order_success();
                    risk_manager.on_fill(signal.ticker, signal.contract_side,
                                         signal.quantity, signal.market_price);
                    spdlog::info("Order placed: {} {} {}x ${:.4f} ({})",
                                 signal.ticker, signal.contract_side, signal.quantity,
                                 signal.market_price, order_id);
                } else {
                    kill_switch.on_order_error();
                    spdlog::warn("Order failed: {}", signal.ticker);
                }
            }

            // Check settlements
            auto settlements = exchange.check_settlements();
            for (const auto& s : settlements) {
                Settlement settl;
                settl.ticker = s.ticker;
                settl.outcome = s.outcome;
                settl.pnl = s.pnl;
                strategy.on_settlement(settl);
                risk_manager.on_settlement(s.ticker, s.pnl);
            }

            // Log status
            auto brier = calibration.brier_score();
            spdlog::info("Status: Balance ${:.2f} | Exposure ${:.2f} | PnL ${:.2f} | "
                         "Signals {} | Trades {} | Brier {:.3f} ({})",
                         risk_manager.balance(), risk_manager.total_exposure(),
                         risk_manager.daily_pnl(), strategy.signals_generated(),
                         strategy.trades_executed(), brier.score, brier.num_predictions);

            last_tick = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 11. Clean shutdown
    spdlog::info("Shutting down...");
    exchange.disconnect();
    spdlog::info("Disconnected. Final stats:");
    spdlog::info("  Balance: ${:.2f}", risk_manager.balance());
    spdlog::info("  Daily PnL: ${:.2f}", risk_manager.daily_pnl());
    spdlog::info("  Total trades: {}", strategy.trades_executed());
    auto final_brier = calibration.brier_score();
    spdlog::info("  Brier score: {:.3f} ({} predictions)", final_brier.score, final_brier.num_predictions);
    spdlog::info("=== Shutdown complete ===");

    return 0;
}
