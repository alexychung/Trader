#pragma once

// Kalshi API 2026 wire format helpers.
//
// Kalshi completed its fixed-point migration in April 2026:
//   - Prices are 4-decimal dollar strings: "0.6500" (was integer cents: 65)
//   - Contract counts are 2-decimal strings in `count_fp`, `yes_bid_size_fp`,
//     etc. (was integer `count`). Fractional trading is enabled per-market via
//     a `fractional_trading_enabled` flag.
//
// These helpers centralize the wire encoding/decoding so there is exactly
// one place to tune format (decimal width, rounding, legacy fallback), and
// one place to add tests. Parsers accept both string (new) and numeric
// (legacy / defensive) representations — numbers are always treated as
// dollars, never cents, matching the post-April-2 server contract.

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdint>

namespace trader::kalshi {

// Format a price in [0, 1] as the 4-decimal string Kalshi expects in
// order submissions and returns in market data.
inline std::string price_to_kalshi_string(double price) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4) << price;
    return ss.str();
}

// Format an integer contract count as the 2-decimal string the `count_fp`
// family of fields uses. Integer inputs serialize as "N.00".
inline std::string count_to_kalshi_fp_string(int count) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << static_cast<double>(count);
    return ss.str();
}

// Same but for a double — used when/if the strategy layer is upgraded to
// hold true fractional contract quantities. Kalshi's wire format supports
// 2-decimal precision; we round half-away-from-zero to that cent grid.
inline std::string count_to_kalshi_fp_string(double count) {
    double snapped = std::round(count * 100.0) / 100.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << snapped;
    return ss.str();
}

// Read a Kalshi price field. Accepts:
//   - a 4-decimal string ("0.6500") — current format
//   - a floating-point number (0.65)    — defensive fallback if server regresses
// Returns default_value if the field is missing or of an unexpected type.
// Numbers are always interpreted as DOLLARS, never legacy integer cents —
// post-April-2-2026 the integer-cents fields are gone, and misinterpreting
// a number-typed 0.65 as "65 cents" would 100x the apparent price.
inline double parse_kalshi_price(const nlohmann::json& j,
                                  const std::string& key,
                                  double default_value = 0.0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return default_value;
    if (it->is_string()) {
        const std::string& s = it->get_ref<const std::string&>();
        try { return std::stod(s); }
        catch (...) {
            // Malformed string (e.g. "N/A" or empty). Distinguish from
            // missing-field by logging — silent 0.0 can look like a
            // legitimate zero bid and skew edge/filter decisions.
            spdlog::warn("parse_kalshi_price: unparseable '{}' value {:?}; "
                         "returning default", key, s);
            return default_value;
        }
    }
    if (it->is_number()) return it->get<double>();
    return default_value;
}

// Read a Kalshi contract count from `count_fp` (or similar). Accepts:
//   - a 2-decimal string ("10.00") — current format
//   - an integer/number (10)        — defensive fallback
// Returns default_value if missing or unparseable. The result is the whole-
// number part (rounded toward zero) because the surrounding strategy code
// still represents quantity as `int`. Sub-contract fractions are dropped;
// this is a conservative choice while fractional trading is rolled out
// per-market and not universally enabled.
inline int parse_kalshi_count_fp(const nlohmann::json& j,
                                 const std::string& key,
                                 int default_value = 0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return default_value;
    if (it->is_string()) {
        try {
            double v = std::stod(it->get<std::string>());
            return static_cast<int>(v);  // truncate fractional part
        } catch (...) { return default_value; }
    }
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number()) return static_cast<int>(it->get<double>());
    return default_value;
}

// Raw-double version for callers that want the exact fractional quantity
// (e.g. a future fractional-aware position tracker).
inline double parse_kalshi_count_fp_double(const nlohmann::json& j,
                                           const std::string& key,
                                           double default_value = 0.0) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return default_value;
    if (it->is_string()) {
        try { return std::stod(it->get<std::string>()); }
        catch (...) { return default_value; }
    }
    if (it->is_number()) return it->get<double>();
    return default_value;
}

// Compute a post-only-safe limit price for a Kalshi buy order. Given the
// current top-of-book in native YES units and which side we're buying,
// returns a price that rests on the book rather than crossing (so Kalshi
// accepts a post_only order instead of rejecting with "would cross").
//
// Semantics:
//   - Queue priority: we post one tick above the best bid on our side
//     rather than joining the existing best-bid queue, trading 1¢ of edge
//     for much better fill probability. On mid-priced contracts that's
//     ~2% of the notional and usually worth it.
//   - Never cross: the result is clamped to at most (ask - tick) so a
//     narrow spread can't accidentally produce a crossing limit. When the
//     spread is exactly one tick wide, this floors at the existing best
//     bid (join the queue, don't cross).
//   - Tick grid: Kalshi prices are 1¢ increments on open markets, so we
//     snap to cents after computation.
//
// Inputs are in YES-native coordinates: for a NO buy, we reflect to
// yes-space (best NO bid = 1 - yes_ask, NO ask = 1 - yes_bid), compute,
// then reflect back.
inline double kalshi_maker_post_price(const std::string& contract_side,
                                       double yes_bid,
                                       double yes_ask,
                                       double tick = 0.01) {
    auto snap_cent = [](double p) {
        return std::round(p * 100.0) / 100.0;
    };
    // Degenerate or stale book — fall back to the far side, which the
    // caller can treat as a taker price (or reject upstream).
    if (!(yes_bid >= 0.0 && yes_ask > yes_bid && yes_ask <= 1.0)) {
        return (contract_side == "yes") ? yes_ask : (1.0 - yes_bid);
    }

    if (contract_side == "yes") {
        // Best YES bid is yes_bid; ask is yes_ask. Post one tick up but
        // never at/above the ask.
        double want = yes_bid + tick;
        double ceiling = yes_ask - tick;
        double price = std::min(want, ceiling);
        if (price < yes_bid) price = yes_bid;  // 1-tick spread: join the queue
        return snap_cent(price);
    }
    // NO buy: work in NO space. Best NO bid = 1 - yes_ask; NO ask = 1 - yes_bid.
    double no_best_bid = 1.0 - yes_ask;
    double no_ask = 1.0 - yes_bid;
    double want = no_best_bid + tick;
    double ceiling = no_ask - tick;
    double price = std::min(want, ceiling);
    if (price < no_best_bid) price = no_best_bid;
    return snap_cent(price);
}

} // namespace trader::kalshi
