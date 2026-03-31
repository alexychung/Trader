#pragma once

#include "core/types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace trader::kalshi {

struct CalibrationRecord {
    std::string market_ticker;
    std::string category;
    Timestamp trade_time;
    Probability model_probability = 0.0;
    double market_price = 0.0;
    double edge = 0.0;
    std::string side;        // "yes" or "no"
    int quantity = 0;
    double entry_price = 0.0;
    double maker_fee = 0.0;
    // Filled after resolution:
    std::optional<Timestamp> resolution_time;
    std::optional<bool> outcome;
    std::optional<double> pnl;
};

struct CalibrationBucket {
    double bucket_center = 0.0;     // e.g., 0.35 for 30-40% bucket
    double avg_prediction = 0.0;
    double actual_frequency = 0.0;
    int sample_count = 0;
};

struct BrierResult {
    double score = 0.0;     // 0 = perfect, 0.25 = random, 1 = always wrong
    int num_predictions = 0;
    std::string category;   // "" for overall
};

class CalibrationLogger {
public:
    // Log a new trade prediction
    void log_trade(const CalibrationRecord& record);

    // Update with resolution outcome
    void resolve(const std::string& ticker, bool outcome, double pnl);

    // Compute Brier score (overall or per category)
    BrierResult brier_score(const std::string& category = "") const;

    // Compute calibration curve (10 buckets)
    std::vector<CalibrationBucket> calibration_curve(const std::string& category = "") const;

    // Access records
    std::vector<CalibrationRecord> records() const;
    int total_trades() const;
    int resolved_trades() const;
    double total_pnl() const;

private:
    mutable std::mutex mutex_;
    std::vector<CalibrationRecord> records_;
};

} // namespace trader::kalshi
