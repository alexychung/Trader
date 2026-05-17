#include "strategy/kalshi/market_filter.hpp"
#include "core/types.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <unordered_map>

namespace trader::kalshi {

namespace {
// Derive a logical category from the ticker when the server-provided `category`
// field is empty. Kalshi deprecated `category` in Mar 2026; demo now returns
// `""` for every market. Without a fallback, the allowed-categories filter
// blocks every market. Mapping matches the models we've registered in
// ProbabilityEngine (weather, economics, fed).
std::string category_from_ticker(const std::string& ticker) {
    auto starts_with = [&](const char* p) {
        std::size_t n = std::strlen(p);
        return ticker.compare(0, n, p) == 0;
    };
    // Weather: high/low temperature, rainfall. KXTEMP* is the 2026 format
    // (KXTEMPNYCH, KXTEMPORDL, etc.); KXHIGH/KXLOW are legacy.
    if (starts_with("KXTEMP")  ||
        starts_with("KXHIGHT") || starts_with("KXLOWT") ||
        starts_with("KXHIGH")  || starts_with("KXLOW")  ||
        starts_with("KXRAIN")  || starts_with("KXSNOW")) {
        return "weather";
    }
    // Inflation / CPI / other macro releases. KXUSNFP = Non-Farm Payrolls,
    // KXJOBLESSCLAIMS = weekly unemployment claims — no model yet but we
    // still want them routed to "economics" so the filter doesn't drop
    // them and the bot surfaces shadow data when a model is added later.
    if (starts_with("KXCPI") || starts_with("KXPCE") || starts_with("KXINFL") ||
        starts_with("KXUSNFP") || starts_with("KXJOBLESSCLAIMS")) {
        return "economics";
    }
    // Fed policy / rates.
    if (starts_with("KXFED") || starts_with("KXFFR") || starts_with("KXRATE")) {
        return "fed";
    }
    return "";
}
} // namespace

FilteredMarket MarketFilter::check(const KalshiMarket& market) const {
    FilteredMarket result;
    result.market = market;
    result.spread = market.yes_ask - market.yes_bid;

    // Category check — with prefix fallback. Kalshi's `category` field is
    // being deprecated and already returns empty on demo. Derive from the
    // ticker when the server didn't set one, so weather/CPI/Fed markets
    // still route through their registered models.
    std::string effective_category = market.category;
    if (effective_category.empty()) {
        effective_category = category_from_ticker(market.ticker);
    }
    if (config_.blocked_categories.count(effective_category)) {
        result.reject_reason = "Blocked category: " + effective_category;
        return result;
    }
    if (!config_.allowed_categories.empty() &&
        !config_.allowed_categories.count(effective_category)) {
        result.reject_reason = "Category not in allowed list: " +
            (effective_category.empty() ? std::string("<unknown>") : effective_category);
        return result;
    }
    // Backfill the category on the output so downstream code (strategy,
    // calibration) doesn't need to re-derive it per market.
    result.market.category = effective_category;

    // Status check. Kalshi has used both "open" (legacy) and "active" (current
    // demo + prod response) for tradeable markets — accept both. Any other
    // value ("closed", "settled", "determined", "finalized") is terminal.
    if (market.status != "open" && market.status != "active") {
        result.reject_reason = "Market not tradeable: " + market.status;
        return result;
    }

    // Fractional-trading guard — opt-in. See FilterConfig comment. Default
    // is to allow these markets because on demo every weather market is
    // fractional-enabled and skipping them empties the category.
    if (config_.skip_fractional_markets && market.fractional_trading_enabled) {
        result.reject_reason = "Fractional trading enabled — skipped by config";
        return result;
    }

    // Volume check
    if (market.volume < config_.min_volume) {
        result.reject_reason = "Volume too low: " + std::to_string(market.volume);
        return result;
    }

    // Spread check — both sides. Narrow means illiquid/no-edge; wide means
    // placeholder book (bid=0.00/ask=1.00 is common on Kalshi demo).
    if (result.spread > config_.max_spread) {
        result.reject_reason = "Spread too wide: " + std::to_string(result.spread);
        return result;
    }
    if (result.spread < config_.min_spread) {
        result.reject_reason = "Spread too narrow: " + std::to_string(result.spread);
        return result;
    }

    // Price range check (avoid favorite-longshot bias at extremes)
    double mid = (market.yes_bid + market.yes_ask) / 2.0;
    if (mid < config_.min_price || mid > config_.max_price) {
        result.reject_reason = "Price out of range: " + std::to_string(mid);
        return result;
    }

    result.passes = true;
    return result;
}

std::vector<FilteredMarket> MarketFilter::filter(const std::vector<KalshiMarket>& markets) const {
    std::vector<FilteredMarket> results;
    // Summarize rejection reasons so we can tell at a glance why N of M
    // markets were dropped. Per-market spdlog would flood at 240+ markets/tick;
    // a reason histogram is compact and actionable.
    std::unordered_map<std::string, int> reject_counts;
    std::unordered_map<std::string, std::string> reject_examples;
    for (const auto& market : markets) {
        auto checked = check(market);
        if (checked.passes) {
            results.push_back(checked);
        } else {
            // Truncate the reason to the stable prefix so e.g. "Volume too
            // low: 3" and "Volume too low: 7" aggregate together.
            std::string key = checked.reject_reason;
            auto colon = key.find(':');
            if (colon != std::string::npos) key.resize(colon);
            ++reject_counts[key];
            reject_examples.try_emplace(key, market.ticker + ": " + checked.reject_reason);
        }
    }
    if (!reject_counts.empty()) {
        for (const auto& [reason, count] : reject_counts) {
            spdlog::info("MarketFilter: {}x \"{}\" (e.g. {})",
                         count, reason, reject_examples[reason]);
        }
    }
    return results;
}

MmQuote MarketMaker::generate_quote(const std::string& ticker, Probability model_prob,
                                     double yes_bid, double yes_ask, double confidence) const {
    MmQuote quote;
    quote.ticker = ticker;

    double spread = yes_ask - yes_bid;

    // Must have wide enough spread
    if (spread < config_.min_spread_to_mm) {
        quote.reason = "Spread too narrow for MM: " + std::to_string(spread);
        return quote;
    }

    // Must have high enough confidence
    if (confidence < config_.min_confidence) {
        quote.reason = "Confidence too low for MM: " + std::to_string(confidence);
        return quote;
    }

    // Compute bid/ask around model probability
    quote.bid_price = model_prob - config_.half_spread_target;
    quote.ask_price = model_prob + config_.half_spread_target;

    // Improve if possible: don't quote worse than current market
    quote.bid_price = std::max(quote.bid_price, yes_bid + 0.01);
    quote.ask_price = std::min(quote.ask_price, yes_ask - 0.01);

    // Validate: bid must be below ask
    if (quote.bid_price >= quote.ask_price) {
        quote.reason = "Bid/ask would cross";
        return quote;
    }

    // Validate: spread must cover fees on both sides
    double mm_spread = quote.ask_price - quote.bid_price;
    double mid_price = (quote.bid_price + quote.ask_price) / 2.0;
    double total_fee = 2.0 * kalshi_maker_fee(1, mid_price);
    if (mm_spread < total_fee) {
        quote.reason = "Spread doesn't cover fees";
        return quote;
    }

    quote.quantity = config_.mm_quantity;
    quote.valid = true;
    return quote;
}

} // namespace trader::kalshi
