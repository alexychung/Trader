// Smoke test for external data feeds (FRED, BLS, Open-Meteo, NWS).
//
// Hits one representative endpoint on each source and prints the result.
// Exit code 0 iff every source returned usable data. Run this after pasting
// API keys into config/config.yaml to verify the credentials work before
// launching the main bot.
//
// Usage: trader_feeds_smoke [config/config.yaml]

#include "core/config.hpp"
#include "core/logging.hpp"
#include "strategy/kalshi/econ_feed.hpp"
#include "strategy/kalshi/weather_feed.hpp"
#include <spdlog/spdlog.h>
#include <iostream>

namespace {

// Print a pass/fail line with the count of items returned. Returns 1 on fail.
int report(const std::string& label, bool ok, std::size_t count, const std::string& note = "") {
    if (ok) {
        std::cout << "  PASS  " << label << " (" << count << " records"
                  << (note.empty() ? "" : ", ") << note << ")\n";
        return 0;
    }
    std::cout << "  FAIL  " << label
              << (note.empty() ? "" : ": ") << note << "\n";
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace trader;
    using namespace trader::kalshi;

    std::string config_path = argc > 1 ? argv[1] : "config/config.yaml";
    Config config;
    try { config = Config::load(config_path); }
    catch (const std::exception& e) {
        std::cerr << "load config: " << e.what() << "\n";
        std::cerr << "Usage: trader_feeds_smoke [config.yaml]\n";
        return 1;
    }

    // Quiet default logger — the smoke test uses stdout directly. Pass --debug
    // as second arg to surface HTTP-level errors.
    bool verbose = (argc > 2 && std::string(argv[2]) == "--debug");
    init_logging(verbose ? "debug" : "warn", "", true);

    int failures = 0;
    std::cout << "Feed smoke test (config: " << config_path << ")\n";
    std::cout << "----------------------------------------\n";

    // ----- FRED -----
    // UNRATE is the monthly unemployment rate; it's small (1 number/month),
    // always has data, and requires a valid API key — a clean yes/no probe.
    if (config.feeds.fred_api_key.empty()) {
        failures += report("FRED", false, 0,
            "no api_key in config.feeds.fred.api_key — get one at "
            "https://fred.stlouisfed.org/docs/api/api_key.html");
    } else {
        FredClient fred(config.feeds.fred_api_key);
        auto obs = fred.fetch_series("UNRATE", 3);
        bool ok = !obs.empty();
        std::string note;
        if (ok) {
            note = "latest: " + obs.back().date + " = " +
                   std::to_string(obs.back().value);
        }
        failures += report("FRED   (UNRATE)", ok, obs.size(), note);
    }

    // ----- BLS -----
    // CUUR0000SA0 is headline CPI. Works with or without a key; unauthenticated
    // is capped at ~25 queries/day but a one-shot smoke is fine.
    {
        BlsClient bls(config.feeds.bls_api_key);
        auto pts = bls.fetch_series("CUUR0000SA0", "2025", "2026");
        bool ok = !pts.empty();
        std::string note;
        if (ok) {
            const auto& latest = pts.front();  // BLS returns newest-first
            note = "latest: " + latest.year + " " + latest.period +
                   " = " + std::to_string(latest.value);
        } else if (config.feeds.bls_api_key.empty()) {
            note = "no key set — verify you're not rate-limited or try a key";
        }
        failures += report("BLS    (CPI)   ", ok, pts.size(), note);
    }

    // ----- Open-Meteo (no key needed) -----
    {
        OpenMeteoClient meteo;
        WeatherStation nyc{"KNYC", "NYC", 40.7128, -74.0060, "KXHIGHNY"};
        auto forecasts = meteo.fetch_ensemble(nyc, 3);
        bool ok = !forecasts.empty();
        std::string note;
        if (ok) {
            note = "models: ";
            bool first = true;
            for (const auto& f : forecasts) {
                if (!first) note += ",";
                note += f.model + "/" + std::to_string(f.num_members) + "mem";
                first = false;
            }
        }
        failures += report("OpenMeteo (KNYC)", ok, forecasts.size(), note);
    }

    // ----- NWS (no key needed) -----
    {
        NwsClient nws;
        WeatherStation nyc{"KNYC", "NYC", 40.7128, -74.0060, "KXHIGHNY"};
        auto fcs = nws.fetch_forecast(nyc);
        bool ok = !fcs.empty();
        std::string note;
        if (ok) {
            note = "first day: " + fcs.front().date + " high=" +
                   std::to_string(fcs.front().high_f) + "F";
        }
        failures += report("NWS    (KNYC)  ", ok, fcs.size(), note);
    }

    std::cout << "----------------------------------------\n";
    if (failures == 0) {
        std::cout << "All feed sources OK.\n";
        return 0;
    }
    std::cout << failures << " feed(s) failed. Fix before running the main bot.\n";
    return failures;
}
