#include "strategy/kalshi/calibration.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace trader::kalshi {

void CalibrationLogger::log_trade(const CalibrationRecord& record) {
    records_.push_back(record);
    spdlog::info("Calibration: {} {} {}x @ ${:.4f} (model: {:.1f}%, market: {:.1f}%, edge: {:.1f}%)",
                 record.market_ticker, record.side, record.quantity,
                 record.entry_price, record.model_probability * 100,
                 record.market_price * 100, record.edge * 100);
}

void CalibrationLogger::resolve(const std::string& ticker, bool outcome, double pnl) {
    auto now = std::chrono::system_clock::now();
    for (auto& rec : records_) {
        if (rec.market_ticker == ticker && !rec.outcome.has_value()) {
            rec.resolution_time = now;
            rec.outcome = outcome;
            rec.pnl = pnl;
        }
    }
}

int CalibrationLogger::resolved_trades() const {
    int count = 0;
    for (const auto& rec : records_) {
        if (rec.outcome.has_value()) ++count;
    }
    return count;
}

double CalibrationLogger::total_pnl() const {
    double total = 0.0;
    for (const auto& rec : records_) {
        if (rec.pnl.has_value()) total += *rec.pnl;
    }
    return total;
}

BrierResult CalibrationLogger::brier_score(const std::string& category) const {
    BrierResult result;
    result.category = category;

    double sum_sq = 0.0;
    int n = 0;

    for (const auto& rec : records_) {
        if (!rec.outcome.has_value()) continue;
        if (!category.empty() && rec.category != category) continue;

        // For YES side: predicted = model_probability, actual = 1.0 if YES won
        // For NO side: predicted = 1.0 - model_probability, actual = 1.0 if NO won
        double predicted = rec.model_probability;
        double actual = (*rec.outcome) ? 1.0 : 0.0;

        if (rec.side == "no") {
            predicted = 1.0 - predicted;
            actual = 1.0 - actual;
        }

        double error = predicted - actual;
        sum_sq += error * error;
        ++n;
    }

    result.num_predictions = n;
    result.score = (n > 0) ? sum_sq / n : 0.0;
    return result;
}

std::vector<CalibrationBucket> CalibrationLogger::calibration_curve(const std::string& category) const {
    // 10 buckets: [0,0.1), [0.1,0.2), ..., [0.9,1.0]
    constexpr int NUM_BUCKETS = 10;
    std::vector<int> counts(NUM_BUCKETS, 0);
    std::vector<double> sum_pred(NUM_BUCKETS, 0.0);
    std::vector<int> sum_actual(NUM_BUCKETS, 0);

    for (const auto& rec : records_) {
        if (!rec.outcome.has_value()) continue;
        if (!category.empty() && rec.category != category) continue;

        double predicted = rec.model_probability;
        bool actual_yes = *rec.outcome;

        if (rec.side == "no") {
            predicted = 1.0 - predicted;
            actual_yes = !actual_yes;
        }

        int bucket = std::min(static_cast<int>(predicted * NUM_BUCKETS), NUM_BUCKETS - 1);
        counts[bucket]++;
        sum_pred[bucket] += predicted;
        if (actual_yes) sum_actual[bucket]++;
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
