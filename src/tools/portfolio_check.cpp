// Diagnostic: query Kalshi demo for current balance + recent fills.
// Used to verify whether orders placed by the bot actually filled on the
// venue, independent of WS delivery. Read-only — places no orders.

#include "core/config.hpp"
#include "core/logging.hpp"
#include "exchange/kalshi/auth.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <chrono>

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

    KalshiAuth auth;
    if (!auth.load_key(config.kalshi.api_key_file)) {
        std::cerr << "failed to load key: " << config.kalshi.api_key_file << "\n";
        return 1;
    }
    auth.set_api_key_id(config.kalshi.api_key_id);

    KalshiRestClient rest(config.kalshi.api_base, auth);

    spdlog::info("=== Portfolio check against {} ===", config.kalshi.api_base);

    double balance = rest.get_balance();
    spdlog::info("Balance: ${:.2f}", balance);

    // Fetch all fills in the last 24h (min_ts_ms = now - 86400000).
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t since = now_ms - 86400000LL;

    auto fills = rest.get_fills_since(since);
    spdlog::info("Fills in last 24h: {}", fills.size());

    double total_fill_cost = 0.0;
    for (const auto& f : fills) {
        spdlog::info("  {} {} {} {}x @ ${:.4f} (order={}, trade={}, ts_ms={})",
                     f.action, f.side, f.ticker, f.count, f.price,
                     f.order_id, f.trade_id, f.created_ts_ms);
        total_fill_cost += f.count * f.price;
    }
    spdlog::info("Total fill cost: ${:.2f}", total_fill_cost);

    // Parsed market positions via the typed helper.
    auto positions = rest.get_market_positions();
    spdlog::info("Parsed market positions: {}", positions.size());
    for (const auto& p : positions) {
        spdlog::info("  {}: {} shares, cost ${:.4f}, fees ${:.4f}, realized ${:.4f}",
                     p.ticker, p.position, p.cost, p.fees_paid, p.realized_pnl);
    }

    return 0;
}
