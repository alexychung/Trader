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
    int quantity = 0;        // 0 for shadow predictions
    double entry_price = 0.0;
    double maker_fee = 0.0;
    bool is_shadow = false;  // true: model predicted but no real trade was placed (selection-bias-free data)
    bool is_exploration = false;  // true: small probe trade, not from main edge logic
    // Which model actually produced this probability. Empty string for
    // historical records and records from models that don't report a source.
    // Known values today:
    //   "ensemble"     — WeatherEnsembleModel's forward GFS+ECMWF estimate
    //   "settled_max"  — post-peak observation override (near-certain)
    //   "cpi"          — CpiModel
    //   "fed"          — FedRateModel
    // Used to partition Brier/calibration by model so we can compare
    // forward-forecast vs settled-max accuracy independently.
    std::string model_source;
    // Filled after resolution:
    std::optional<Timestamp> resolution_time;
    std::optional<bool> outcome;
    std::optional<double> pnl;  // for shadow records, stays 0 (no actual fill)
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

// Per-category aggregate stats used by AdaptiveSizer.
struct CategoryAggregate {
    std::string category;
    int resolved_total = 0;     // resolved real + shadow
    int resolved_real = 0;      // real trades only
    double brier = 0.25;        // all-time Brier (real + shadow); 0.25 default = "no info"
    double realized_pnl = 0.0;  // real trades only, sum of pnl
    double expected_edge = 0.0; // sum of (edge * quantity) at trade time, real trades only

    // Rolling-window Brier: populated from the most recent rolling_window resolved
    // records (real + shadow) in this category. Used to detect drift — a category
    // with good all-time Brier but recently degraded rolling Brier is a broken
    // model, not a well-calibrated one. Default 0.25 = insufficient samples.
    double rolling_brier = 0.25;
    int rolling_window = 0;     // actual sample count used (0 = not computed)
};

class CalibrationLogger {
public:
    CalibrationLogger() = default;

    // Construct with a persistence path. Will load existing records on construction.
    // Empty path = in-memory only.
    explicit CalibrationLogger(std::string persist_path);

    // Log a real trade prediction (will be written to disk if persist enabled).
    void log_trade(const CalibrationRecord& record);

    // Log a shadow prediction (model would have bet but no order placed).
    // Used to avoid selection bias in calibration data.
    void log_shadow_prediction(const CalibrationRecord& record);

    // Update with resolution outcome. Updates ALL records matching ticker
    // (shadow + real). For shadow records, pnl stays 0.
    void resolve(const std::string& ticker, bool outcome, double pnl);

    // Compute Brier score over ALL records (real + shadow) for unbiased calibration.
    BrierResult brier_score(const std::string& category = "") const;

    // Compute Brier score over REAL records only (what we actually traded).
    BrierResult brier_score_real(const std::string& category = "") const;

    // Compute calibration curve (10 buckets) — includes shadows for unbiased view.
    std::vector<CalibrationBucket> calibration_curve(const std::string& category = "") const;

    // Aggregated per-category stats for adaptive sizing. `rolling_n` controls
    // the rolling-Brier window (most recent N resolved records in the category).
    // 0 disables the rolling computation (leaves defaults in the struct).
    CategoryAggregate category_aggregate(const std::string& category,
                                          int rolling_n = 30) const;

    // Brier computed over the last N resolved records matching category.
    // N==0 returns an empty result (num_predictions = 0).
    BrierResult rolling_brier_score(const std::string& category, int n) const;

    // Brier computed only over records from a specific model source
    // (e.g. "settled_max" vs "ensemble"). Lets us compare per-source
    // accuracy in the same category. Empty source matches records that
    // didn't tag their source (pre-upgrade data).
    BrierResult brier_score_by_source(const std::string& source,
                                        const std::string& category = "") const;

    // Distinct categories seen.
    std::vector<std::string> categories() const;

    // Access records (returns a copy under lock).
    std::vector<CalibrationRecord> records() const;
    int total_trades() const;             // real + shadow
    int real_trades() const;
    int shadow_trades() const;
    int resolved_trades() const;
    double total_pnl() const;             // realized PnL from real trades only

    // Persistence path (empty = in-memory).
    const std::string& persist_path() const { return persist_path_; }

    // Force a persist flush (respects mutex). Call this before shutdown or
    // on any caller-initiated flush point — append/resolve normally debounce
    // writes to at most one per persist_min_interval.
    void flush();

    // Debounce window. Each append/resolve normally just marks the log
    // dirty; the actual rewrite happens at most once per this interval, or
    // on an explicit flush(). Default 5s. Set to 0 to write on every append
    // (the old eager behavior — useful for tests).
    void set_persist_min_interval(std::chrono::seconds s) { persist_min_interval_ = s; }

private:
    // Writes the full records_ vector to persist_path_ atomically (tmp file
    // + rename). Caller must hold mutex_.
    void persist_locked() const;
    // Mark the log as dirty and flush to disk only if enough time has
    // elapsed since the last rewrite — O(1) amortized under sustained
    // append load, vs O(N) per append. Caller must hold mutex_.
    void maybe_persist_locked();
    void load_from_disk();                // called from constructor

    mutable std::mutex mutex_;
    std::vector<CalibrationRecord> records_;
    std::string persist_path_;            // JSON file path (snapshot, atomic rewrite)

    mutable bool dirty_ = false;
    mutable std::chrono::system_clock::time_point last_persist_{};
    std::chrono::seconds persist_min_interval_{5};
};

} // namespace trader::kalshi
