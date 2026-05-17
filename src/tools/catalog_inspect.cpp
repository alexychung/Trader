// List the first unique ticker prefixes in the demo catalog. Useful for
// discovering which market series are actually live on demo (Kalshi demo is a
// rolling subset — e.g. weather markets are commonly absent off-season).
//
// Usage: trader_catalog_inspect [config.yaml]

#include "core/config.hpp"
#include "core/logging.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <map>

int main(int argc, char* argv[]) {
    using namespace trader;
    using namespace trader::kalshi;

    std::string config_path = argc > 1 ? argv[1] : "config/config.yaml";
    Config config;
    try { config = Config::load(config_path); }
    catch (const std::exception& e) {
        std::cerr << "load config: " << e.what() << "\n"; return 1;
    }
    init_logging("warn", "", true);

    KalshiAuth auth;
    if (!auth.load_key(config.kalshi.api_key_file)) {
        std::cerr << "failed to load key\n"; return 1;
    }
    auth.set_api_key_id(config.kalshi.api_key_id);
    KalshiRestClient rest(config.kalshi.api_base, auth);

    // Paginate through the full catalog — first page on prod is dominated by
    // multivariate-event markets and hides everything else.
    auto markets = rest.get_markets_paginated("open", 1000, 50);
    std::cout << "Pulled " << markets.size() << " open markets from "
              << config.kalshi.api_base << " (paginated)\n\n";

    // Bucket by first token before the first dash. KXHIGHNY-26APR20-T75 →
    // "KXHIGHNY"; KXCPI-26MAR → "KXCPI"; etc.
    std::map<std::string, int> by_prefix;
    for (const auto& m : markets) {
        std::string prefix = m.ticker;
        auto dash = prefix.find('-');
        if (dash != std::string::npos) prefix.resize(dash);
        ++by_prefix[prefix];
    }

    std::cout << "Unique series prefixes (count):\n";
    for (const auto& [p, n] : by_prefix) {
        std::cout << "  " << n << "x  " << p << "\n";
    }

    // Sample tickers for weather/macro families — we need the full format
    // (not just the prefix) to know how to parse them.
    std::cout << "\nSample tickers for weather/macro families:\n";
    auto is_interesting = [](const std::string& p) {
        return p.find("TEMP") != std::string::npos
            || p.find("HIGH") != std::string::npos
            || p.find("LOW")  != std::string::npos
            || p.find("CPI")  != std::string::npos
            || p.find("PCE")  != std::string::npos
            || p.find("NFP")  != std::string::npos
            || p.find("FED")  != std::string::npos
            || p.find("WEATHER") != std::string::npos;
    };
    std::map<std::string, int> shown_by_prefix;
    for (const auto& m : markets) {
        std::string prefix = m.ticker;
        auto dash = prefix.find('-');
        if (dash != std::string::npos) prefix.resize(dash);
        if (!is_interesting(prefix)) continue;
        if (shown_by_prefix[prefix]++ >= 3) continue;
        std::cout << "  " << m.ticker
                  << "  status=" << m.status
                  << "  bid=" << m.yes_bid << " ask=" << m.yes_ask << "\n";
    }
    return 0;
}
