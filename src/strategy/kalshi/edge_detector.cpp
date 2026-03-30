#include "strategy/kalshi/edge_detector.hpp"
#include "core/types.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace trader::kalshi {

double EdgeDetector::kelly_fraction(double model_prob, double market_price) {
    if (market_price <= 0.0 || market_price >= 1.0) return 0.0;

    double b = (1.0 - market_price) / market_price;  // Net odds
    double p = model_prob;
    double q = 1.0 - p;

    double f = (b * p - q) / b;
    return std::max(0.0, f);  // Never negative
}

double EdgeDetector::net_ev_per_contract(double model_prob, double market_price) {
    // EV = P(win) * $1.00 - price - maker_fee
    double maker_fee = kalshi_maker_fee(1, market_price);
    return model_prob * 1.0 - market_price - maker_fee;
}

int EdgeDetector::position_size(double model_prob, double market_price) const {
    double f = kelly_fraction(model_prob, market_price);
    double bet_fraction = f * config_.kelly_fraction;

    // Dollar amount to bet
    double bet_dollars = bet_fraction * config_.bankroll;

    // Contracts = dollars / price
    int contracts = static_cast<int>(std::floor(bet_dollars / market_price));

    // Clamp to limits
    contracts = std::max(0, std::min(contracts, config_.max_position));
    return contracts;
}

EdgeResult EdgeDetector::detect(const std::string& ticker, Probability model_prob,
                                 double yes_bid, double yes_ask, double confidence) {
    EdgeResult result;
    result.ticker = ticker;
    result.model_prob = model_prob;
    result.confidence = confidence;

    // Compute edge on both sides
    double edge_yes = model_prob - yes_ask;      // Buy YES at ask
    double edge_no = (1.0 - model_prob) - (1.0 - yes_bid);  // Buy NO at (1 - bid)

    // Choose the side with more edge
    if (edge_yes >= edge_no) {
        result.side = Side::Buy;
        result.contract_side = "yes";
        result.market_price = yes_ask;
        result.edge = edge_yes;
    } else {
        result.side = Side::Buy;
        result.contract_side = "no";
        result.market_price = 1.0 - yes_bid;  // NO price = 1 - YES bid
        result.edge = edge_no;
    }

    result.net_ev = net_ev_per_contract(
        (result.contract_side == "yes") ? model_prob : (1.0 - model_prob),
        result.market_price
    );

    // Check filters
    bool price_ok = result.market_price >= config_.min_price &&
                    result.market_price <= config_.max_price;
    bool edge_ok = result.edge >= config_.min_edge;
    bool confidence_ok = confidence >= config_.min_confidence;
    bool ev_ok = result.net_ev > 0.0;

    if (price_ok && edge_ok && confidence_ok && ev_ok) {
        result.kelly_quantity = position_size(
            (result.contract_side == "yes") ? model_prob : (1.0 - model_prob),
            result.market_price
        );

        std::ostringstream oss;
        oss << "Edge: " << (result.edge * 100.0) << "% on "
            << result.contract_side << " @ $" << result.market_price
            << " (model: " << (model_prob * 100.0) << "%, net EV: $"
            << result.net_ev << "/contract)";
        result.rationale = oss.str();
    } else {
        result.kelly_quantity = 0;
        result.rationale = "No trade: ";
        if (!price_ok) result.rationale += "price out of range; ";
        if (!edge_ok) result.rationale += "edge too small; ";
        if (!confidence_ok) result.rationale += "low confidence; ";
        if (!ev_ok) result.rationale += "negative EV after fees; ";
    }

    return result;
}

} // namespace trader::kalshi
