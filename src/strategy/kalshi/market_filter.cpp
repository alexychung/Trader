#include "strategy/kalshi/market_filter.hpp"
#include "core/types.hpp"

namespace trader::kalshi {

FilteredMarket MarketFilter::check(const KalshiMarket& market) const {
    FilteredMarket result;
    result.market = market;
    result.spread = market.yes_ask - market.yes_bid;

    // Category check
    if (config_.blocked_categories.count(market.category)) {
        result.reject_reason = "Blocked category: " + market.category;
        return result;
    }
    if (!config_.allowed_categories.empty() &&
        !config_.allowed_categories.count(market.category)) {
        result.reject_reason = "Category not in allowed list: " + market.category;
        return result;
    }

    // Status check
    if (market.status != "open") {
        result.reject_reason = "Market not open: " + market.status;
        return result;
    }

    // Volume check
    if (market.volume < config_.min_volume) {
        result.reject_reason = "Volume too low: " + std::to_string(market.volume);
        return result;
    }

    // Spread check
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
    for (const auto& market : markets) {
        auto checked = check(market);
        if (checked.passes) {
            results.push_back(checked);
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
