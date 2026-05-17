#include "backtest/driver.hpp"
#include "backtest/report.hpp"
#include "backtest/settled_markets_fetcher.hpp"
#include "core/config.hpp"
#include "core/logging.hpp"
#include "core/staleness_gate.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include "exchange/mock_exchange.hpp"
#include "risk/cluster_limiter.hpp"
#include "risk/risk_manager.hpp"
#include "strategy/kalshi/adaptive_sizer.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/kalshi/econ_models.hpp"
#include "strategy/kalshi/edge_detector.hpp"
#include "strategy/kalshi/event_strategy.hpp"
#include "strategy/kalshi/market_filter.hpp"
#include "strategy/kalshi/probability_calibrator.hpp"
#include "strategy/kalshi/probability_engine.hpp"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <iostream>
#include <string>

using namespace trader;
using namespace trader::kalshi;

namespace {

struct CliArgs {
    std::string start_date;
    std::string end_date;
    std::string category = "weather";
    std::string config_path = "config/config.yaml";
    std::string run_id;
    int max_markets = 200;
    std::string api_base = "https://api.elections.kalshi.com/trade-api/v2";
    std::string cache_dir = "data/cache/backtest";
    std::string out_dir = "data/backtest";
};

void print_usage() {
    std::cerr <<
        "trader_backtest --start YYYY-MM-DD --end YYYY-MM-DD [options]\n"
        "  --category <cat>       default: weather\n"
        "  --config <path>        default: config/config.yaml\n"
        "  --run-id <id>          default: timestamp\n"
        "  --max-markets <n>      default: 200\n"
        "  --api-base <url>       default: https://api.elections.kalshi.com/trade-api/v2\n"
        "  --cache-dir <path>     default: data/cache/backtest\n"
        "  --out-dir <path>       default: data/backtest\n";
}

bool parse_args(int argc, char** argv, CliArgs& out) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << flag << " requires a value\n";
                return "";
            }
            return argv[++i];
        };
        if (a == "--start") out.start_date = next("--start");
        else if (a == "--end") out.end_date = next("--end");
        else if (a == "--category") out.category = next("--category");
        else if (a == "--config") out.config_path = next("--config");
        else if (a == "--run-id") out.run_id = next("--run-id");
        else if (a == "--max-markets") out.max_markets = std::stoi(next("--max-markets"));
        else if (a == "--api-base") out.api_base = next("--api-base");
        else if (a == "--cache-dir") out.cache_dir = next("--cache-dir");
        else if (a == "--out-dir") out.out_dir = next("--out-dir");
        else if (a == "-h" || a == "--help") { print_usage(); return false; }
        else { std::cerr << "unknown flag: " << a << "\n"; print_usage(); return false; }
    }
    if (out.start_date.empty() || out.end_date.empty()) {
        std::cerr << "--start and --end are required\n";
        print_usage();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    CliArgs args;
    if (!parse_args(argc, argv, args)) return 1;

    // Config is optional — if present, use the logging section. Everything else
    // for backtest is CLI-driven.
    Config cfg;
    try {
        if (std::filesystem::exists(args.config_path)) {
            cfg = Config::load(args.config_path);
        }
    } catch (const std::exception& e) {
        std::cerr << "failed to load config (" << e.what() << "), using defaults\n";
    }
    init_logging(cfg.logging.level.empty() ? "info" : cfg.logging.level,
                 "logs/backtest.log", true);

    spdlog::info("=== Kalshi Backtest Harness ===");
    spdlog::info("Window: {} .. {}  category='{}'  max_markets={}",
                 args.start_date, args.end_date, args.category, args.max_markets);

    // --- Exchange for historical market fetches (unauthenticated is fine) ---
    KalshiAuth auth;
    KalshiRestClient rest(args.api_base, auth);

    backtest::SettledMarketsFetcher fetcher(rest, args.cache_dir + "/markets");
    auto markets = fetcher.fetch_all(args.start_date, args.end_date,
                                       args.category, args.max_markets);
    if (markets.empty()) {
        spdlog::error("No settled markets found in window. Check --start/--end and --api-base.");
        return 2;
    }
    spdlog::info("Loaded {} settled markets (cache hits: {}, misses: {})",
                 markets.size(), fetcher.cache_hits(), fetcher.cache_misses());

    // --- Build strategy with isolated calibration file ---
    std::string run_id = args.run_id;
    if (run_id.empty()) {
        run_id = "run-" + args.start_date + "-" + args.end_date + "-" + args.category;
    }
    auto run_dir = std::filesystem::path(args.out_dir) / run_id;
    std::error_code ec;
    std::filesystem::create_directories(run_dir, ec);

    MockExchange exchange(100.0);
    exchange.connect();

    ProbabilityEngine prob_engine;
    auto weather_model = std::make_shared<WeatherEnsembleModel>();
    auto cpi_model = std::make_shared<CpiModel>();
    auto fed_model = std::make_shared<FedRateModel>();
    prob_engine.register_model("weather", weather_model);
    prob_engine.register_model("economics", cpi_model);
    prob_engine.register_model("fed", fed_model);

    RiskConfig risk_cfg;
    RiskManager risk_manager(risk_cfg);
    risk_manager.set_balance(100.0);

    EdgeDetector::Config edge_cfg;
    edge_cfg.min_edge = 0.05;
    edge_cfg.kelly_fraction = 0.25;
    edge_cfg.bankroll = 100.0;
    EdgeDetector edge_detector(edge_cfg);

    MarketFilter market_filter;

    // Isolated calibration file per run so the live bot's calibration stays clean.
    std::string calib_path = (run_dir / "calibration.json").string();
    CalibrationLogger calibration(calib_path);

    AdaptiveSizer sizer;
    ProbabilityCalibrator prob_calibrator;
    StalenessGate staleness;
    ClusterLimiter cluster_limiter;

    KalshiEventStrategy strategy(prob_engine, edge_detector, market_filter,
                                  risk_manager, calibration, sizer,
                                  prob_calibrator, staleness, cluster_limiter);

    backtest::BacktestDriver::Config driver_cfg;
    driver_cfg.weather_cache_dir = args.cache_dir + "/weather";
    backtest::BacktestDriver driver(strategy, prob_engine, weather_model,
                                     staleness, exchange, driver_cfg);

    auto run_result = driver.run(std::move(markets));
    spdlog::info("Driver: decisions={}, settlements={}, orders={}",
                 run_result.decisions_made, run_result.settlements_applied,
                 run_result.orders_placed);

    backtest::BacktestReport::Config rep_cfg;
    rep_cfg.run_id = run_id;
    rep_cfg.out_dir_base = args.out_dir;
    rep_cfg.adverse_fill_slippage = driver_cfg.adverse_fill_slippage;
    rep_cfg.assume_taker_fills = driver_cfg.assume_taker_fills;
    backtest::BacktestReport report(calibration, exchange, run_result, rep_cfg);
    auto out_dir = report.write();

    auto summary = report.compute_summary();
    spdlog::info("---");
    spdlog::info("Samples: {} (real={}, shadow={}, resolved={})",
                 summary.samples, summary.real_trades, summary.shadow_trades, summary.resolved);
    spdlog::info("Brier model (all):       {:.4f}", summary.brier_all);
    spdlog::info("Brier model (real only): {:.4f}", summary.brier_real);
    spdlog::info("Brier market baseline:   {:.4f}", summary.brier_market_baseline);
    spdlog::info("Model edge vs market:    {:+.4f}", summary.brier_market_baseline - summary.brier_all);
    spdlog::info("Hit rate (strong, p>=0.55): {:.1f}%", summary.hit_rate * 100);
    spdlog::info("Hypothetical PnL: ${:.2f} (fee drag ${:.2f}, slippage ${:.2f})",
                 summary.hypothetical_pnl, summary.fee_drag, summary.slippage_drag);
    spdlog::info("Report written to: {}", out_dir);

    // Exit code semantics for scripts: 0 = normal completion; 3 = no resolved samples.
    return summary.resolved > 0 ? 0 : 3;
}
