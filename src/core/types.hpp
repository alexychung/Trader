#pragma once

#include <chrono>
#include <cmath>
#include <string>
#include <algorithm>
#include <vector>

namespace trader {

// Core type aliases
using Price = double;
using Quantity = double;
using Probability = double;       // [0.0, 1.0]
using ContractPrice = double;     // [0.0, 1.0] — maps to probability on Kalshi
using Timestamp = std::chrono::system_clock::time_point;
using OrderId = std::string;

// Enums

enum class Side { Buy, Sell };

inline std::string to_string(Side s) {
    switch (s) {
        case Side::Buy: return "buy";
        case Side::Sell: return "sell";
    }
    return "unknown";
}

inline Side side_from_string(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "buy") return Side::Buy;
    return Side::Sell;
}

enum class OrderStatus { Pending, Open, Filled, Cancelled, Rejected };

inline std::string to_string(OrderStatus s) {
    switch (s) {
        case OrderStatus::Pending:   return "pending";
        case OrderStatus::Open:      return "open";
        case OrderStatus::Filled:    return "filled";
        case OrderStatus::Cancelled: return "cancelled";
        case OrderStatus::Rejected:  return "rejected";
    }
    return "unknown";
}

// Core structs

struct MarketEvent {
    std::string ticker;
    std::string category;       // "economics", "weather", "fed"
    Timestamp close_time;
    Timestamp resolution_time;
};

struct Order {
    std::string ticker;
    Side side = Side::Buy;
    std::string contract_side;  // "yes" or "no"
    double price = 0.0;
    int quantity = 0;
    bool post_only = true;      // Always use maker orders
    std::string client_order_id;
};

struct MarketUpdate {
    std::string ticker;
    double yes_bid = 0.0;
    double yes_ask = 0.0;
    double last_price = 0.0;
    int volume = 0;
    int open_interest = 0;
    Timestamp timestamp;
};

struct DataSignal {
    std::string source;         // "open_meteo", "nws", "bls", "fred"
    std::string series_id;
    std::string label;
    double value = 0.0;
    double previous_value = 0.0;
    Timestamp observation_date;
    Timestamp fetch_time;
};

struct Fill {
    OrderId order_id;
    std::string ticker;
    Side side = Side::Buy;
    std::string contract_side;
    double price = 0.0;
    int quantity = 0;
    Timestamp timestamp;
};

struct Settlement {
    std::string ticker;
    bool outcome = false;       // true = YES resolved, false = NO resolved
    double pnl = 0.0;
    Timestamp resolution_time;
};

struct TradeSignal {
    std::string ticker;
    Side side = Side::Buy;
    std::string contract_side;  // "yes" or "no"
    Probability model_probability = 0.0;
    // Reference price used by the edge/fee math — yes_ask for a YES buy,
    // (1 - yes_bid) for a NO buy. This is the price we'd PAY as a taker
    // and also what the calibration log stores as entry_price. Order
    // placement may price differently (see yes_bid_snapshot/yes_ask_snapshot
    // and kalshi_maker_post_price) when maker-only is on.
    ContractPrice market_price = 0.0;
    // Top-of-book snapshot at signal time, in native YES units. main.cpp
    // uses these to compute a post-only price that rests on the book
    // rather than crossing. Leaving them zero means "fall back to
    // market_price as the taker price."
    double yes_bid_snapshot = 0.0;
    double yes_ask_snapshot = 0.0;
    double edge = 0.0;
    double confidence = 0.0;
    int quantity = 0;
    std::string rationale;
};

// Kalshi fee calculations
// maker_fee = ceil(0.0175 * contracts * price * (1 - price))
// taker_fee = ceil(0.07  * contracts * price * (1 - price))
// Returns fee in dollars (ceil to nearest cent)

// Kalshi's server computes fees in integer cents. We mirror ceil-to-cent but
// must defend against floating-point drift on exact boundaries: e.g.
// 0.07 * 100 * 0.5 * 0.5 * 100 evaluates to 175.000…003 in IEEE-754, ceiling
// to 176 when the correct Kalshi fee is 175 cents. The 1e-9 tolerance is
// smaller than any real sub-cent remainder (the minimum non-zero raw fee is
// 0.0175 * 1 * 0.01 * 0.99 * 100 ≈ 0.017 cents) but larger than float noise
// across realistic inputs.
inline double kalshi_fee_cents_from_raw_dollars(double raw_dollars) {
    return std::ceil(raw_dollars * 100.0 - 1e-9) / 100.0;
}

// Globals match Kalshi's default schedule. Use the overloads below when
// a per-series override is in play (GET /series/fee_changes).
constexpr double kKalshiDefaultMakerRate = 0.0175;
constexpr double kKalshiDefaultTakerRate = 0.07;

inline double kalshi_maker_fee(int contracts, double price, double rate) {
    double raw = rate * contracts * price * (1.0 - price);
    return kalshi_fee_cents_from_raw_dollars(raw);
}

inline double kalshi_maker_fee(int contracts, double price) {
    return kalshi_maker_fee(contracts, price, kKalshiDefaultMakerRate);
}

inline double kalshi_taker_fee(int contracts, double price, double rate) {
    double raw = rate * contracts * price * (1.0 - price);
    return kalshi_fee_cents_from_raw_dollars(raw);
}

inline double kalshi_taker_fee(int contracts, double price) {
    return kalshi_taker_fee(contracts, price, kKalshiDefaultTakerRate);
}

} // namespace trader
