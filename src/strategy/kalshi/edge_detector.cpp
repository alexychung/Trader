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

double EdgeDetector::net_ev_per_contract(double model_prob, double market_price,
                                         bool round_trip, double maker_rate) {
    // EV = P(win) * $1.00 - price - entry_fee [- exit_fee if round-trip]
    // Settlement fee = 0; exit fee applies only if we sell before settlement
    // (e.g. force-exit on model flip, reprice on market move).
    double maker_fee = kalshi_maker_fee(1, market_price, maker_rate);
    double total_fee = round_trip ? 2.0 * maker_fee : maker_fee;
    return model_prob * 1.0 - market_price - total_fee;
}

double EdgeDetector::fee_floor_edge(double market_price, bool round_trip,
                                    double safety_buffer, double maker_rate) {
    double fee = kalshi_maker_fee(1, market_price, maker_rate);
    double total_fee = round_trip ? 2.0 * fee : fee;
    return total_fee + safety_buffer;
}

SeriesFeeRates EdgeDetector::rates_for(const std::string& ticker) const {
    auto it = config_.fee_schedule.find(kalshi_series_from_ticker(ticker));
    if (it != config_.fee_schedule.end()) return it->second;
    return {};  // defaults
}

int EdgeDetector::position_size(double model_prob, double market_price, double kelly_scale) const {
    double f = kelly_fraction(model_prob, market_price);
    double bet_fraction = f * config_.kelly_fraction * kelly_scale;

    // Dollar amount to bet
    double bet_dollars = bet_fraction * config_.bankroll;

    // Contracts = dollars / price
    int contracts = (market_price > 0.0)
        ? static_cast<int>(std::floor(bet_dollars / market_price))
        : 0;

    // Clamp to limits
    contracts = std::max(0, std::min(contracts, config_.max_position));
    return contracts;
}

EdgeResult EdgeDetector::detect(const std::string& ticker, Probability model_prob,
                                 double yes_bid, double yes_ask, double confidence,
                                 double kelly_scale) {
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

    // Pick up per-series fee overrides (Sep 21, 2025). Unknown series fall
    // through to the global 0.07/0.0175 defaults.
    const SeriesFeeRates rates = rates_for(ticker);
    result.net_ev = net_ev_per_contract(
        (result.contract_side == "yes") ? model_prob : (1.0 - model_prob),
        result.market_price,
        config_.assume_round_trip_fees,
        rates.maker_rate
    );

    // Dynamic edge floor — max(config min_edge, actual round-trip fees + buffer).
    // Prevents trading edges that look positive but would be eaten by fees on
    // early exits. Cheap contracts have smaller fees, so the floor relaxes;
    // mid-priced contracts (~$0.50) have the highest fees and tighten it.
    const double fee_floor = fee_floor_edge(result.market_price,
                                            config_.assume_round_trip_fees,
                                            config_.fee_safety_buffer,
                                            rates.maker_rate);
    const double effective_min_edge = std::max(config_.min_edge, fee_floor);

    // Check filters
    bool price_ok = result.market_price >= config_.min_price &&
                    result.market_price <= config_.max_price;
    bool edge_ok = result.edge >= effective_min_edge;
    bool confidence_ok = confidence >= config_.min_confidence;
    bool ev_ok = result.net_ev > 0.0;

    if (price_ok && edge_ok && confidence_ok && ev_ok && kelly_scale > 0.0) {
        result.kelly_quantity = position_size(
            (result.contract_side == "yes") ? model_prob : (1.0 - model_prob),
            result.market_price,
            kelly_scale
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
