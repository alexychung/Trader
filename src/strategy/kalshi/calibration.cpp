#include "strategy/kalshi/calibration.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

namespace trader::kalshi {

namespace fs = std::filesystem;
using json = nlohmann::json;

static int64_t to_epoch_seconds(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::seconds>(ts.time_since_epoch()).count();
}

static Timestamp from_epoch_seconds(int64_t s) {
    return Timestamp(std::chrono::seconds(s));
}

static json record_to_json(const CalibrationRecord& r) {
    json j;
    j["market_ticker"]      = r.market_ticker;
    j["category"]           = r.category;
    j["trade_time"]         = to_epoch_seconds(r.trade_time);
    j["model_probability"]  = r.model_probability;
    j["market_price"]       = r.market_price;
    j["edge"]               = r.edge;
    j["side"]               = r.side;
    j["quantity"]           = r.quantity;
    j["entry_price"]        = r.entry_price;
    j["maker_fee"]          = r.maker_fee;
    j["is_shadow"]          = r.is_shadow;
    j["is_exploration"]     = r.is_exploration;
    if (!r.model_source.empty()) j["model_source"] = r.model_source;
    if (r.resolution_time) j["resolution_time"] = to_epoch_seconds(*r.resolution_time);
    if (r.outcome)         j["outcome"]         = *r.outcome;
    if (r.pnl)             j["pnl"]             = *r.pnl;
    return j;
}

static CalibrationRecord record_from_json(const json& j) {
    CalibrationRecord r;
    r.market_ticker     = j.value("market_ticker", "");
    r.category          = j.value("category", "");
    r.trade_time        = from_epoch_seconds(j.value("trade_time", int64_t{0}));
    r.model_probability = j.value("model_probability", 0.0);
    r.market_price      = j.value("market_price", 0.0);
    r.edge              = j.value("edge", 0.0);
    r.side              = j.value("side", "yes");
    r.quantity          = j.value("quantity", 0);
    r.entry_price       = j.value("entry_price", 0.0);
    r.maker_fee         = j.value("maker_fee", 0.0);
    r.is_shadow         = j.value("is_shadow", false);
    r.is_exploration    = j.value("is_exploration", false);
    r.model_source      = j.value("model_source", std::string{});
    if (j.contains("resolution_time")) r.resolution_time = from_epoch_seconds(j["resolution_time"].get<int64_t>());
    if (j.contains("outcome"))         r.outcome         = j["outcome"].get<bool>();
    if (j.contains("pnl"))             r.pnl             = j["pnl"].get<double>();
    return r;
}

CalibrationLogger::CalibrationLogger(std::string persist_path)
    : persist_path_(std::move(persist_path)) {
    if (!persist_path_.empty()) {
        auto parent = fs::path(persist_path_).parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            std::error_code ec;
            fs::create_directories(parent, ec);
        }
        load_from_disk();
    }
}

void CalibrationLogger::load_from_disk() {
    if (!fs::exists(persist_path_)) return;
    std::ifstream in(persist_path_);
    if (!in) {
        spdlog::warn("CalibrationLogger: cannot open {} for read", persist_path_);
        return;
    }
    try {
        json j; in >> j;
        if (!j.is_array()) {
            spdlog::warn("CalibrationLogger: unexpected format in {}, ignoring", persist_path_);
            return;
        }
        records_.reserve(j.size());
        for (const auto& item : j) {
            records_.push_back(record_from_json(item));
        }
        spdlog::info("CalibrationLogger: loaded {} records from {}", records_.size(), persist_path_);
    } catch (const std::exception& e) {
        spdlog::error("CalibrationLogger: parse failure on {}: {}. Starting empty.", persist_path_, e.what());
        records_.clear();
    }
}

void CalibrationLogger::maybe_persist_locked() {
    dirty_ = true;
    if (persist_min_interval_.count() == 0) {
        // Write-through mode — used by tests that assert on-disk state.
        persist_locked();
        dirty_ = false;
        last_persist_ = std::chrono::system_clock::now();
        return;
    }
    auto now = std::chrono::system_clock::now();
    if (now - last_persist_ >= persist_min_interval_) {
        persist_locked();
        dirty_ = false;
        last_persist_ = now;
    }
    // else: stay dirty, flushed on the next qualifying append/resolve or on
    // an explicit flush() (called from main.cpp before shutdown).
}

void CalibrationLogger::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dirty_ || last_persist_ == std::chrono::system_clock::time_point{}) {
        persist_locked();
        dirty_ = false;
        last_persist_ = std::chrono::system_clock::now();
    }
}

void CalibrationLogger::persist_locked() const {
    if (persist_path_.empty()) return;
    json arr = json::array();
    for (const auto& r : records_) arr.push_back(record_to_json(r));

    std::string tmp = persist_path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            spdlog::error("CalibrationLogger: cannot open {} for write", tmp);
            return;
        }
        out << arr.dump();
        out.flush();
        if (!out) {
            spdlog::error("CalibrationLogger: write failed for {}", tmp);
            return;
        }
    }
    std::error_code ec;
    fs::rename(tmp, persist_path_, ec);
    if (ec) {
        fs::remove(persist_path_, ec); ec.clear();
        fs::rename(tmp, persist_path_, ec);
    }
    if (ec) spdlog::error("CalibrationLogger: rename failed: {}", ec.message());
}

void CalibrationLogger::log_trade(const CalibrationRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.push_back(record);
    spdlog::info("Calibration: {} {} {}x @ ${:.4f} (model: {:.1f}%, market: {:.1f}%, edge: {:.1f}%){}{}",
                 record.market_ticker, record.side, record.quantity,
                 record.entry_price, record.model_probability * 100,
                 record.market_price * 100, record.edge * 100,
                 record.is_exploration ? " [exploration]" : "",
                 record.is_shadow ? " [shadow]" : "");
    maybe_persist_locked();
}

void CalibrationLogger::log_shadow_prediction(const CalibrationRecord& record) {
    CalibrationRecord copy = record;
    copy.is_shadow = true;
    copy.quantity = 0;
    copy.maker_fee = 0.0;
    log_trade(copy);
}

void CalibrationLogger::resolve(const std::string& ticker, bool outcome, double pnl) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();

    // Two issues the naive assignment (`rec.pnl = pnl` on every match) caused:
    //   1. If the strategy placed N real trades on the same ticker, each
    //      record got the full settlement pnl, inflating total_pnl() Nx.
    //   2. The pnl arg already represents the whole-ticker settlement, so
    //      the right split is by each record's notional (quantity * entry),
    //      which is how the trade's cash share of the settlement flows back.
    //
    // Compute total notional first, then distribute. Shadows record the
    // outcome but contribute zero notional and zero pnl.
    double total_notional = 0.0;
    for (const auto& rec : records_) {
        if (rec.market_ticker != ticker || rec.outcome.has_value()) continue;
        if (rec.is_shadow) continue;
        total_notional += static_cast<double>(rec.quantity) * rec.entry_price;
    }

    int updated = 0;
    for (auto& rec : records_) {
        if (rec.market_ticker != ticker || rec.outcome.has_value()) continue;
        rec.resolution_time = now;
        rec.outcome = outcome;
        if (rec.is_shadow) {
            rec.pnl = 0.0;
        } else if (total_notional > 0.0) {
            double share = (static_cast<double>(rec.quantity) * rec.entry_price)
                           / total_notional;
            rec.pnl = pnl * share;
        } else {
            // No real records to split against — assign full pnl to whatever
            // single real record exists (preserves prior behavior for N=1).
            rec.pnl = pnl;
        }
        ++updated;
    }
    if (updated > 0) maybe_persist_locked();
}

std::vector<CalibrationRecord> CalibrationLogger::records() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

int CalibrationLogger::total_trades() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(records_.size());
}

int CalibrationLogger::real_trades() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int c = 0;
    for (const auto& r : records_) if (!r.is_shadow) ++c;
    return c;
}

int CalibrationLogger::shadow_trades() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int c = 0;
    for (const auto& r : records_) if (r.is_shadow) ++c;
    return c;
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
        if (!rec.is_shadow && rec.pnl.has_value()) total += *rec.pnl;
    }
    return total;
}

// Helper: compute Brier from a filter predicate while holding lock.
template <typename Pred>
static BrierResult brier_filtered(const std::vector<CalibrationRecord>& records,
                                   const std::string& category, Pred pred) {
    BrierResult result;
    result.category = category;
    double sum_sq = 0.0;
    int n = 0;
    for (const auto& rec : records) {
        if (!rec.outcome.has_value()) continue;
        if (!category.empty() && rec.category != category) continue;
        if (!pred(rec)) continue;

        double predicted = (rec.side == "no") ? (1.0 - rec.model_probability) : rec.model_probability;
        bool our_side_won = (rec.side == "yes") ? (*rec.outcome) : !(*rec.outcome);
        double actual = our_side_won ? 1.0 : 0.0;
        double err = predicted - actual;
        sum_sq += err * err;
        ++n;
    }
    result.num_predictions = n;
    result.score = (n > 0) ? sum_sq / n : 0.0;
    return result;
}

BrierResult CalibrationLogger::brier_score(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return brier_filtered(records_, category, [](const CalibrationRecord&) { return true; });
}

BrierResult CalibrationLogger::brier_score_real(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return brier_filtered(records_, category, [](const CalibrationRecord& r) { return !r.is_shadow; });
}

BrierResult CalibrationLogger::brier_score_by_source(const std::string& source,
                                                    const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return brier_filtered(records_, category,
        [&source](const CalibrationRecord& r) { return r.model_source == source; });
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
        if (bucket < 0) bucket = 0;
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

CategoryAggregate CalibrationLogger::category_aggregate(const std::string& category,
                                                        int rolling_n) const {
    std::lock_guard<std::mutex> lock(mutex_);
    CategoryAggregate agg;
    agg.category = category;

    double sum_sq = 0.0;
    int n_brier = 0;
    int n_resolved_total = 0;
    int n_resolved_real = 0;

    // records_ is append-only + resolve mutates in place (no reordering), so
    // iterating front-to-back preserves trade-time order. The rolling tail
    // is computed by collecting resolved records in order and taking the last N.
    // This is O(N) in records_ and allocation-free for small rolling_n.
    struct ResolvedPoint { double predicted; bool won; };
    std::vector<ResolvedPoint> resolved_seq;
    if (rolling_n > 0) resolved_seq.reserve(records_.size() / 4);

    for (const auto& rec : records_) {
        if (rec.category != category) continue;
        if (!rec.outcome.has_value()) continue;

        double predicted = (rec.side == "no") ? (1.0 - rec.model_probability) : rec.model_probability;
        bool our_side_won = (rec.side == "yes") ? (*rec.outcome) : !(*rec.outcome);
        double actual = our_side_won ? 1.0 : 0.0;
        double err = predicted - actual;
        sum_sq += err * err;
        ++n_brier;
        ++n_resolved_total;
        if (rolling_n > 0) resolved_seq.push_back({predicted, our_side_won});

        if (!rec.is_shadow) {
            ++n_resolved_real;
            if (rec.pnl.has_value()) agg.realized_pnl += *rec.pnl;
            // expected_edge per contract * quantity (in dollars)
            agg.expected_edge += rec.edge * rec.quantity;
        }
    }

    agg.brier = (n_brier > 0) ? sum_sq / n_brier : 0.25;
    agg.resolved_total = n_resolved_total;
    agg.resolved_real = n_resolved_real;

    // Rolling Brier over the last rolling_n resolved records.
    if (rolling_n > 0 && !resolved_seq.empty()) {
        const std::size_t start = (resolved_seq.size() > static_cast<std::size_t>(rolling_n))
            ? resolved_seq.size() - static_cast<std::size_t>(rolling_n)
            : 0;
        double rsum = 0.0;
        const std::size_t n = resolved_seq.size() - start;
        for (std::size_t i = start; i < resolved_seq.size(); ++i) {
            double e = resolved_seq[i].predicted - (resolved_seq[i].won ? 1.0 : 0.0);
            rsum += e * e;
        }
        agg.rolling_brier = rsum / n;
        agg.rolling_window = static_cast<int>(n);
    }
    return agg;
}

BrierResult CalibrationLogger::rolling_brier_score(const std::string& category, int n) const {
    BrierResult result;
    result.category = category;
    if (n <= 0) return result;

    std::lock_guard<std::mutex> lock(mutex_);
    // Collect resolved records (real + shadow) matching category, in order.
    std::vector<std::pair<double, bool>> seq;
    seq.reserve(records_.size() / 4);
    for (const auto& rec : records_) {
        if (!rec.outcome.has_value()) continue;
        if (!category.empty() && rec.category != category) continue;
        double predicted = (rec.side == "no") ? (1.0 - rec.model_probability) : rec.model_probability;
        bool won = (rec.side == "yes") ? (*rec.outcome) : !(*rec.outcome);
        seq.emplace_back(predicted, won);
    }
    if (seq.empty()) return result;

    const std::size_t start = (seq.size() > static_cast<std::size_t>(n))
        ? seq.size() - static_cast<std::size_t>(n) : 0;
    double sum_sq = 0.0;
    for (std::size_t i = start; i < seq.size(); ++i) {
        double e = seq[i].first - (seq[i].second ? 1.0 : 0.0);
        sum_sq += e * e;
    }
    result.num_predictions = static_cast<int>(seq.size() - start);
    result.score = sum_sq / result.num_predictions;
    return result;
}

std::vector<std::string> CalibrationLogger::categories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> uniq;
    for (const auto& r : records_) uniq.insert(r.category);
    return std::vector<std::string>(uniq.begin(), uniq.end());
}

} // namespace trader::kalshi
