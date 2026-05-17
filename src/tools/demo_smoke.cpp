// Smoke test against Kalshi demo: authenticates, picks a live market, places a
// lowball post-only buy, confirms the round-trip, and cancels. Intended to be
// run manually against demo — exits non-zero on any failure.

#include "core/config.hpp"
#include "core/logging.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include <spdlog/spdlog.h>
#include <iostream>

int main(int argc, char* argv[]) {
    using namespace trader;
    using namespace trader::kalshi;

    std::string config_path = argc > 1 ? argv[1] : "config/config.yaml";
    Config config;
    try { config = Config::load(config_path); }
    catch (const std::exception& e) {
        std::cerr << "load config: " << e.what() << "\n"; return 1;
    }
    init_logging("info", "", true);

    if (config.mode == "live") {
        std::cerr << "refusing to run smoke test in live mode\n";
        return 1;
    }

    KalshiAuth auth;
    if (!auth.load_key(config.kalshi.api_key_file)) {
        std::cerr << "failed to load key: " << config.kalshi.api_key_file << "\n";
        return 1;
    }
    auth.set_api_key_id(config.kalshi.api_key_id);

    KalshiRestClient rest(config.kalshi.api_base, auth);

    double balance = rest.get_balance();
    spdlog::info("demo balance: ${:.2f}", balance);
    if (balance <= 0.0) { spdlog::error("no balance"); return 2; }

    auto markets = rest.get_markets("", "open", 50);
    spdlog::info("fetched {} markets", markets.size());

    // Pick a market with a valid yes_ask so we can post a far-from-market buy.
    const KalshiMarket* target = nullptr;
    for (const auto& m : markets) {
        if (m.yes_ask > 0.02 && m.yes_ask < 0.99) { target = &m; break; }
    }
    if (!target) { spdlog::error("no suitable market"); return 3; }
    spdlog::info("target: {} yes_bid={:.2f} yes_ask={:.2f}",
                 target->ticker, target->yes_bid, target->yes_ask);

    // Post-only buy at $0.01 — well below ask, should rest on book, not fill.
    OrderId id = rest.place_order(target->ticker, "yes", "buy", 0.01, 1, true);
    if (id.empty()) { spdlog::error("place_order failed"); return 4; }
    spdlog::info("placed order id={}", id);

    bool cancelled = rest.cancel_order(id);
    if (!cancelled) { spdlog::error("cancel failed for {}", id); return 5; }
    spdlog::info("cancelled order {} — smoke test PASS", id);
    return 0;
}
