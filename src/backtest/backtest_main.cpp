// Backtest CLI.
//
// Usage:
//   trader_backtest --start YYYY-MM-DD --end YYYY-MM-DD
//                   [--strategy nba|reslag|both]
//                   [--cache-dir data/cache/pbp]
//                   [--csv-out data/backtest/results.csv]
//                   [--config config/config.yaml]
//
// Fetches NBA scoreboards + play-by-play in [start, end], runs the
// configured strategy against each game with a SyntheticKalshiPriceProvider,
// and prints aggregate metrics. Per-game results are written to a CSV the
// user can pivot in a spreadsheet.

#include "backtest/kalshi_candle_provider.hpp"
#include "backtest/nba_pbp_fetcher.hpp"
#include "backtest/replay_engine.hpp"
#include "core/config.hpp"
#include "core/logging.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/rest_client.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CliArgs {
    std::string start_date;
    std::string end_date;
    std::string strategy = "both";   // nba | reslag | both
    std::string cache_dir = "data/cache/pbp";
    std::string csv_out;             // empty = skip CSV
    std::string config_path = "config/config.yaml";
    // Synthetic-Kalshi noise standard deviation (in price units). Larger
    // values produce more Kalshi mispricing and therefore more strategy
    // signals — useful to verify the strategy fires correctly even on a
    // toy data source. 0.01 is the calibrated-Kalshi case (almost no
    // signals); 0.04-0.06 is closer to "Kalshi is reacting slowly /
    // anchored" and exercises the strategy.
    double noise_stdev = 0.04;
    // Overrides for sizing knobs that materially change the backtest result.
    // -1 = use whatever's in config (or built-in default).
    double max_position_dollars = -1.0;
    double simulated_bankroll = -1.0;
    // Use real historical Kalshi candlesticks instead of the synthetic
    // provider. Requires network on first run; subsequent runs hit the
    // candle_cache_dir cache. Credentials are read from the same config
    // file as the live bot (config.kalshi.api_key_file / api_key_id) —
    // Kalshi's candlestick endpoint requires authentication, even for
    // public market data.
    bool real_prices = false;
    std::string candle_cache_dir = "data/cache/candles";
    bool help = false;
};

void print_usage() {
    std::cerr <<
        "Usage: trader_backtest --start YYYY-MM-DD --end YYYY-MM-DD\n"
        "                       [--strategy nba|reslag|both]\n"
        "                       [--cache-dir data/cache/pbp]\n"
        "                       [--csv-out data/backtest/results.csv]\n"
        "                       [--config config/config.yaml]\n"
        "                       [--noise-stdev 0.04]\n"
        "                       [--max-position-dollars N]   # override per-game sizing cap\n"
        "                       [--bankroll N]               # override simulated bankroll\n"
        "                       [--real-prices]              # use real Kalshi candlesticks (auth via config.kalshi)\n"
        "                       [--candle-cache-dir data/cache/candles]\n";
}

bool parse_args(int argc, char* argv[], CliArgs& out) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " requires a value\n";
                return {};
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { out.help = true; }
        else if (a == "--start") out.start_date = next("--start");
        else if (a == "--end") out.end_date = next("--end");
        else if (a == "--strategy") out.strategy = next("--strategy");
        else if (a == "--cache-dir") out.cache_dir = next("--cache-dir");
        else if (a == "--csv-out") out.csv_out = next("--csv-out");
        else if (a == "--config") out.config_path = next("--config");
        else if (a == "--noise-stdev") {
            try { out.noise_stdev = std::stod(next("--noise-stdev")); }
            catch (...) { std::cerr << "bad --noise-stdev\n"; return false; }
        }
        else if (a == "--max-position-dollars") {
            try { out.max_position_dollars = std::stod(next("--max-position-dollars")); }
            catch (...) { std::cerr << "bad --max-position-dollars\n"; return false; }
        }
        else if (a == "--bankroll") {
            try { out.simulated_bankroll = std::stod(next("--bankroll")); }
            catch (...) { std::cerr << "bad --bankroll\n"; return false; }
        }
        else if (a == "--real-prices") out.real_prices = true;
        else if (a == "--candle-cache-dir") out.candle_cache_dir = next("--candle-cache-dir");
        else {
            std::cerr << "Unknown arg: " << a << "\n";
            return false;
        }
    }
    if (out.help) return true;
    if (out.start_date.empty() || out.end_date.empty()) {
        std::cerr << "ERROR: --start and --end are required.\n";
        return false;
    }
    if (out.strategy != "nba" && out.strategy != "reslag" &&
        out.strategy != "both") {
        std::cerr << "ERROR: --strategy must be one of nba, reslag, both\n";
        return false;
    }
    return true;
}

// Inclusive date iteration. Both args YYYY-MM-DD.
std::vector<std::string> iterate_dates(const std::string& start,
                                        const std::string& end) {
    std::vector<std::string> out;
    auto parse = [](const std::string& s) -> std::tm {
        std::tm tm{};
        tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(s.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(s.substr(8, 2));
        return tm;
    };
    std::tm s = parse(start);
    std::tm e = parse(end);
#ifdef _WIN32
    std::time_t st = _mkgmtime(&s);
    std::time_t et = _mkgmtime(&e);
#else
    std::time_t st = timegm(&s);
    std::time_t et = timegm(&e);
#endif
    for (std::time_t t = st; t <= et; t += 86400) {
        std::tm gm{};
#ifdef _WIN32
        gmtime_s(&gm, &t);
#else
        gmtime_r(&t, &gm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &gm);
        out.push_back(buf);
    }
    return out;
}

void write_csv(const std::string& path,
                const trader::backtest::BacktestSummary& summary) {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    std::ofstream f(path, std::ios::trunc);
    if (!f) {
        spdlog::error("Could not open CSV for writing: {}", path);
        return;
    }
    f << "game_id,date,away,home,away_final,home_final,home_won,"
         "signals_arcsine,signals_reslag,fills,total_fees,settled_pnl\n";
    for (const auto& g : summary.per_game) {
        f << g.game_id << "," << g.date_iso << ","
          << g.away_tricode << "," << g.home_tricode << ","
          << g.away_final_score << "," << g.home_final_score << ","
          << (g.home_won ? 1 : 0) << ","
          << g.signals_arcsine << "," << g.signals_resolution_lag << ","
          << g.fills.size() << ","
          << std::fixed << std::setprecision(4) << g.total_fees << ","
          << std::fixed << std::setprecision(4) << g.settled_pnl << "\n";
    }
}

} // namespace

int main(int argc, char* argv[]) {
    CliArgs args;
    if (!parse_args(argc, argv, args) || args.help) {
        print_usage();
        return args.help ? 0 : 1;
    }

    // Use the same logging init as the live bot so log levels follow config.
    // Config is optional — we fall back to defaults if it's missing.
    trader::Config cfg;
    try { cfg = trader::Config::load(args.config_path); }
    catch (...) {
        spdlog::warn("Could not load config {}, using defaults",
                     args.config_path);
    }
    trader::init_logging(cfg.logging.level, /*file=*/"", /*console=*/true);

    spdlog::info("Backtest range: {} → {}, strategy={}",
                 args.start_date, args.end_date, args.strategy);

    trader::backtest::NbaPbpFetcher pbp(args.cache_dir);

    // Build the strategy config from the live config's NBA + reslag blocks.
    trader::backtest::ReplayConfig rcfg;
    rcfg.nba.min_edge_threshold            = cfg.nba.min_edge_threshold;
    rcfg.nba.max_edge_threshold            = cfg.nba.max_edge_threshold;
    rcfg.nba.max_spread                    = cfg.nba.max_spread;
    rcfg.nba.min_seconds_remaining         = cfg.nba.min_seconds_remaining;
    rcfg.nba.max_seconds_remaining         = cfg.nba.max_seconds_remaining;
    rcfg.nba.max_position_per_game_dollars = cfg.nba.max_position_per_game_dollars;
    rcfg.nba.kelly_fraction                = cfg.nba.kelly_fraction;
    rcfg.nba.min_abs_score_diff            = cfg.nba.min_abs_score_diff;
    rcfg.nba.max_uncertain_wp              = cfg.nba.max_uncertain_wp;
    rcfg.nba.min_strong_wp                 = cfg.nba.min_strong_wp;
    rcfg.nba.min_lot_size                  = cfg.nba.min_lot_size;
    rcfg.nba.min_clv_edge                  = cfg.nba.min_clv_edge;
    rcfg.nba.min_market_volume             = cfg.nba.min_market_volume;
    rcfg.nba.default_pregame_spread        = cfg.nba.default_pregame_spread;
    rcfg.reslag.cutoff_clock_seconds       = cfg.resolution_lag.cutoff_clock_seconds;
    rcfg.reslag.cutoff_lead_points         = cfg.resolution_lag.cutoff_lead_points;
    rcfg.reslag.include_status_final       = cfg.resolution_lag.include_status_final;
    rcfg.reslag.min_entry_price            = cfg.resolution_lag.min_entry_price;
    rcfg.reslag.max_entry_price            = cfg.resolution_lag.max_entry_price;
    rcfg.reslag.max_position_per_game_dollars =
        cfg.resolution_lag.max_position_per_game_dollars;
    rcfg.reslag.min_lot_size               = cfg.resolution_lag.min_lot_size;
    rcfg.reslag.min_market_volume          = cfg.resolution_lag.min_market_volume;
    rcfg.run_resolution_lag = (args.strategy == "reslag" || args.strategy == "both");
    // For --strategy nba, we also need to *disable* arcsine signals from the
    // NBA strategy. Easiest: set an absurd min_edge_threshold so nothing fires.
    if (args.strategy == "reslag") {
        rcfg.nba.min_edge_threshold = 1.0;  // unreachable
    }

    if (args.max_position_dollars > 0.0) {
        rcfg.nba.max_position_per_game_dollars = args.max_position_dollars;
        rcfg.reslag.max_position_per_game_dollars = args.max_position_dollars;
        spdlog::info("Override: max_position_per_game_dollars = ${:.2f}",
                     args.max_position_dollars);
    }
    if (args.simulated_bankroll > 0.0) {
        rcfg.simulated_bankroll = args.simulated_bankroll;
        spdlog::info("Override: simulated_bankroll = ${:.2f}",
                     args.simulated_bankroll);
    }

    trader::backtest::BacktestReplay engine(rcfg);

    // Optional real-prices path. The REST client + candle provider are
    // built only if the user asked for them — keeps the default synthetic
    // path free of network deps.
    std::unique_ptr<trader::kalshi::KalshiAuth> auth;
    std::unique_ptr<trader::kalshi::KalshiRestClient> rest;
    std::unique_ptr<trader::backtest::KalshiCandlePriceProvider> candle_provider;
    if (args.real_prices) {
        auth = std::make_unique<trader::kalshi::KalshiAuth>();
        if (!cfg.kalshi.api_key_file.empty()) {
            if (!auth->load_key(cfg.kalshi.api_key_file)) {
                spdlog::error("Could not load Kalshi key from {} — candle "
                              "fetch will 404. Aborting.",
                              cfg.kalshi.api_key_file);
                return 2;
            }
        }
        auth->set_api_key_id(cfg.kalshi.api_key_id);
        rest = std::make_unique<trader::kalshi::KalshiRestClient>(
            cfg.kalshi.api_base, *auth);
        candle_provider = std::make_unique<
            trader::backtest::KalshiCandlePriceProvider>(*rest,
                                                          args.candle_cache_dir);
        spdlog::info("Real-price mode: candles via {} (cache: {})",
                     cfg.kalshi.api_base, args.candle_cache_dir);
    }

    std::vector<trader::backtest::GameReplayResult> per_game;
    int games_attempted = 0;
    int games_skipped_no_kalshi_data = 0;

    for (const auto& date : iterate_dates(args.start_date, args.end_date)) {
        auto games = pbp.fetch_games_on_date(date);
        spdlog::info("Date {}: {} games", date, games.size());
        for (const auto& g : games) {
            // Only replay games that finished — pre/in-progress have
            // no settle outcome we can score.
            if (g.game_status != 3) continue;
            ++games_attempted;
            auto events = pbp.fetch_pbp(g.game_id);
            if (events.empty()) {
                spdlog::warn("No PBP for {} {}@{} — skipping",
                             g.game_id, g.away_tricode, g.home_tricode);
                continue;
            }
            if (args.real_prices) {
                bool ok = candle_provider->prefetch_game(
                    g.game_date_iso, g.home_tricode, g.away_tricode);
                if (!ok) {
                    ++games_skipped_no_kalshi_data;
                    spdlog::info("No Kalshi candles for {} {}@{} — skipping",
                                 g.game_id, g.away_tricode, g.home_tricode);
                    continue;
                }
                auto result = engine.replay_game(g, events, *candle_provider);
                per_game.push_back(result);
            } else {
                trader::backtest::SyntheticKalshiPriceProvider provider(
                    g.home_tricode, g.away_tricode,
                    /*half_spread=*/0.02, /*noise_stdev=*/args.noise_stdev);
                auto result = engine.replay_game(g, events, provider);
                per_game.push_back(result);
            }
        }
    }
    if (args.real_prices) {
        spdlog::info("Candle cache: {} hits, {} misses, {} empty fetches; "
                     "{} games skipped (no Kalshi data)",
                     candle_provider->cache_hits(),
                     candle_provider->cache_misses(),
                     candle_provider->empty_fetches(),
                     games_skipped_no_kalshi_data);
    }

    auto summary = trader::backtest::aggregate(per_game);
    spdlog::info("PBP cache: {} hits, {} misses",
                 pbp.cache_hits(), pbp.cache_misses());

    std::cout << "\n=== Backtest Summary ===\n"
              << "Range:            " << args.start_date << " → "
                                       << args.end_date << "\n"
              << "Strategy:         " << args.strategy << "\n"
              << "Games attempted:  " << games_attempted << "\n"
              << "Games replayed:   " << summary.games_replayed << "\n"
              << "Total signals:    " << summary.total_signals << "\n"
              << "Total fills:      " << summary.total_fills << "\n"
              << "Wins:             " << summary.wins << "\n"
              << "Losses:           " << summary.losses;
    if (summary.wins + summary.losses > 0) {
        double wr = static_cast<double>(summary.wins) /
                    static_cast<double>(summary.wins + summary.losses);
        std::cout << "  (" << std::fixed << std::setprecision(1)
                  << wr * 100.0 << "% win rate)";
    }
    std::cout << "\n"
              << "Total PnL:        $" << std::fixed << std::setprecision(2)
              << summary.total_pnl << "\n"
              << "Total fees:       $" << std::fixed << std::setprecision(2)
              << summary.total_fees << "\n"
              << "Max drawdown:     $" << std::fixed << std::setprecision(2)
              << summary.max_drawdown << "\n"
              << "Brier score:      " << std::fixed << std::setprecision(4)
              << summary.brier_score << "  ("
              << summary.brier_samples << " samples)\n"
              << "Avg PnL per game: $";
    if (summary.games_replayed > 0) {
        std::cout << std::fixed << std::setprecision(2)
                  << summary.total_pnl / summary.games_replayed;
    } else {
        std::cout << "N/A";
    }
    std::cout << "\n========================\n\n";
    if (args.real_prices) {
        std::cout << "NOTE: Real Kalshi candlestick prices (1-minute bars). The\n"
                     "strategy saw `yes_bid_close` / `yes_ask_close` for each\n"
                     "bar — sub-minute moves are invisible. Wall-clock anchoring\n"
                     "for pbp events is approximate (see pbp_wall_clock_ts_sec)\n"
                     "so the candle bar may be off by a minute or two vs. real\n"
                     "live execution. Games with no Kalshi data are skipped.\n\n";
    } else {
        std::cout << "NOTE: This run used a SYNTHETIC Kalshi price model\n"
                     "(our_fair ± half_spread + small noise). It exercises\n"
                     "strategy logic and signal rate but is OPTIMISTIC about\n"
                     "edge — synthetic prices are correlated with our model\n"
                     "by construction. Re-run with --real-prices for an\n"
                     "honest edge test against historical Kalshi books.\n\n";
    }

    if (!args.csv_out.empty()) {
        write_csv(args.csv_out, summary);
        std::cout << "Per-game results written to " << args.csv_out << "\n";
    }

    return 0;
}
