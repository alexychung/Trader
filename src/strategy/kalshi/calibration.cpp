#include "strategy/kalshi/calibration.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace trader::kalshi {

void CalibrationLogger::log_trade(const CalibrationRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.push_back(record);
    spdlog::info("Calibration: {} {} {}x @ ${:.4f} (model: {:.1f}%, market: {:.1f}%, edge: {:.1f}%)",
                 record.market_ticker, record.side, record.quantity,
                 record.entry_price, record.model_probability * 100,
                 record.market_price * 100, record.edge * 100);
}

void CalibrationLogger::resolve(const std::string& ticker, bool outcome, double pnl) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    for (auto& rec : records_) {
        if (rec.market_ticker == ticker && !rec.outcome.has_value()) {
            rec.resolution_time = now;
            rec.outcome = outcome;
            rec.pnl = pnl;
        }
    }
}

std::vector<CalibrationRecord> CalibrationLogger::records() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

int CalibrationLogger::total_trades() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(records_.size());
}

int CalibrationLogger::resolved_trades() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (const auto& rec : records_) {
        if (rec.outcome.has_value()) ++count;
    }
    return count;
}

double CalibrationLogger::total_pnl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double total = 0.0;
    for (const auto& rec : records_) {
        if (rec.pnl.has_value()) total += *rec.pnl;
    }
    return total;
}

BrierResult CalibrationLogger::brier_score(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    BrierResult result;
    result.category = category;

    double sum_sq = 0.0;
    int n = 0;

    for (const auto& rec : records_) {
        if (!rec.outcome.has_value()) continue;
        if (!category.empty() && rec.category != category) continue;

        // Transform to the side we actually traded:
        // If we traded YES: predicted = P(YES), actual = 1 if YES won
        // If we traded NO:  predicted = P(NO) = 1-P(YES), actual = 1 if NO won
        double predicted = (rec.side == "no") ? (1.0 - rec.model_probability) : rec.model_probability;
        bool our_side_won = (rec.side == "yes") ? (*rec.outcome) : !(*rec.outcome);
        double actual = our_side_won ? 1.0 : 0.0;

        double error = predicted - actual;
        sum_sq += error * error;
        ++n;
    }

    result.num_predictions = n;
    result.score = (n > 0) ? sum_sq / n : 0.0;
    return result;
}

std::vector<CalibrationBucket> CalibrationLogger::calibration_curve(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    constexpr int NUM_BUCKETS = 10;
    std::vector<int> counts(NUM_BUCKETS, 0);
    std::vector<double> sum_pred(NUM_BUCKETS, 0.0);
    std::vector<int> sum_actual(NUM_BUCKETS, 0);

    for (const auto& rec : records_) {
        if (!rec.outcome.has_value()) continue;
        if (!category.empty() && rec.category != category) continue;

        double predicted = (rec.side == "no") ? (1.0 - rec.model_probability) : rec.model_probability;
        bool our_side_won = (rec.side == "yes") ? (*rec.outcome) : !(*rec.outcome);

        int bucket = std::min(static_cast<int>(predicted * NUM_BUCKETS), NUM_BUCKETS - 1);
        counts[bucket]++;
        sum_pred[bucket] += predicted;
        if (our_side_won) sum_actual[bucket]++;
    }

    std::vector<CalibrationBucket> result;
    for (int i = 0; i < NUM_BUCKETS; ++i) {
        CalibrationBucket b;
        b.bucket_center = (i + 0.5) / NUM_BUCKETS;
        b.sample_count = counts[i];
        b.avg_prediction = (counts[i] > 0) ? sum_pred[i] / counts[i] : b.bucket_center;
        b.actual_frequency = (counts[i] > 0) ? static_cast<double>(sum_actual[i]) / counts[i] : 0.0;
        result.push_back(b);
    }

    return result;
}

} // namespace trader::kalshi
