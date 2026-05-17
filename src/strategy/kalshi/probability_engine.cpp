#include "strategy/kalshi/probability_engine.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <regex>
#include <climits>
#include <cstdio>
#include <cstdlib>

namespace trader::kalshi {

// ===== WeatherEnsembleModel =====

void WeatherEnsembleModel::set_ensemble(const EnsembleForecast& forecast) {
    // Key by station+date+model so GFS and ECMWF forecasts for the same
    // contract-date live side-by-side rather than clobbering each other.
    forecasts_[forecast_key(forecast.station_id, forecast.target_date,
                             forecast.model)] = forecast;
}

void WeatherEnsembleModel::set_bias_correction(const std::string& station_id, double bias_f) {
    bias_[station_id] = bias_f;
}

Probability WeatherEnsembleModel::raw_ensemble_probability(
    const std::vector<double>& member_highs, double threshold) {
    if (member_highs.empty()) return 0.5;

    int above = 0;
    for (double high : member_highs) {
        if (high > threshold) ++above;
    }
    return static_cast<double>(above) / member_highs.size();
}

std::vector<double> WeatherEnsembleModel::apply_bias_correction(
    const std::vector<double>& member_highs, double bias) {
    std::vector<double> corrected;
    corrected.reserve(member_highs.size());
    for (double h : member_highs) {
        corrected.push_back(h - bias);
    }
    return corrected;
}

double WeatherEnsembleModel::horizon_sigma(int days_ahead) {
    // Calibrated from NWS historical accuracy
    // Source: KALSHI_RESEARCH.md Section 11.1
    switch (days_ahead) {
        case 0: return 1.5;  // Same day
        case 1: return 2.0;
        case 2: return 3.0;
        case 3: return 4.0;
        case 4: return 4.5;
        case 5: return 5.0;
        case 6: return 5.5;
        case 7: return 6.0;
        default: return 6.0 + (days_ahead - 7) * 0.5;
    }
}

Probability WeatherEnsembleModel::blend_models(Probability gfs_prob, Probability ecmwf_prob,
                                                  double gfs_weight, double ecmwf_weight) {
    double total_weight = gfs_weight + ecmwf_weight;
    return (gfs_prob * gfs_weight + ecmwf_prob * ecmwf_weight) / total_weight;
}

namespace {
// Convert Kalshi's ticker date "26APR21" → ISO "2026-04-21" to match the
// key format used by Open-Meteo's ensemble cache
// (weather_feed.cpp stores target_date = times[0].substr(0, 10)).
// Format is YY-MMM-DD with the month as a 3-letter abbrev; no day is
// "26APR" which we interpret as day-1 of that month. Returns empty on
// unparseable input.
std::string ticker_date_to_iso(const std::string& d) {
    static const std::unordered_map<std::string, int> months = {
        {"JAN",1},{"FEB",2},{"MAR",3},{"APR",4},{"MAY",5},{"JUN",6},
        {"JUL",7},{"AUG",8},{"SEP",9},{"OCT",10},{"NOV",11},{"DEC",12}
    };
    if (d.size() < 5) return "";
    int yy = 0;
    try { yy = std::stoi(d.substr(0, 2)); } catch (...) { return ""; }
    if (yy < 0 || yy > 99) return "";
    auto mit = months.find(d.substr(2, 3));
    if (mit == months.end()) return "";
    int day = 1;
    if (d.size() >= 7) {
        try { day = std::stoi(d.substr(5, 2)); } catch (...) { return ""; }
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "20%02d-%02d-%02d", yy, mit->second, day);
    return buf;
}
} // namespace

WeatherEnsembleModel::TickerInfo WeatherEnsembleModel::parse_weather_ticker(const std::string& ticker) {
    TickerInfo info;

    // Legacy prefix -> station map.
    static const std::unordered_map<std::string, std::string> legacy_prefix_to_station = {
        {"KXHIGHNY", "KNYC"},
        {"KXHIGHCHI", "KORD"},
        {"KXHIGHMIA", "KMIA"},
        {"KXHIGHLAX", "KLAX"},
        {"KXHIGHDEN", "KDEN"},
        {"KXHIGHAUS", "KAUS"},
        {"KXHIGHPHIL", "KPHL"},
    };

    // Accept three weather ticker formats:
    //   1. Legacy: KXHIGHNY-26APR-T75             (no day, mapped prefix)
    //   2. Mid:    KXHIGHTSFO-26MAR30-T73         (KXHIGHT + IATA + day)
    //   3. Current (2026-04): KXTEMPNYCH-26APR2310-T70.99
    //      (KXTEMP + IATA + H/L + YY+MMM+DD+HH + decimal threshold)
    // The date group allows 0-4 trailing digits so the current format's
    // 4-digit DDHH and the legacy 2-digit DD both match. Thresholds are
    // always integer or decimal °F.
    std::regex re(R"((KXHIGHT?[A-Z]+|KXTEMP[A-Z]+)-(\d{2}[A-Z]{3}\d{0,4})-T(-?\d+(?:\.\d+)?))");
    std::smatch match;

    if (!std::regex_match(ticker, match, re)) {
        info.valid = false;
        return info;
    }

    std::string prefix = match[1].str();
    info.date = match[2].str();

    auto it = legacy_prefix_to_station.find(prefix);
    if (it != legacy_prefix_to_station.end()) {
        info.station_id = it->second;
    } else if (prefix.size() == 10 && prefix.rfind("KXHIGHT", 0) == 0) {
        // KXHIGHT<3-letter IATA> -> K<IATA>
        info.station_id = "K" + prefix.substr(7, 3);
    } else if (prefix.rfind("KXTEMP", 0) == 0
               && (prefix.back() == 'H' || prefix.back() == 'L')
               && prefix.size() >= 8) {
        // KXTEMP<3+ letter IATA><H|L> -> K<IATA>. The trailing H/L marks
        // High vs Low markets; we treat both as "daily temperature" because
        // the ensemble model scores P(temp > threshold) regardless of
        // which tail of the distribution the market bets on. The strategy
        // layer still infers side (YES vs NO) from price, so a low-temp
        // market priced at P(YES) = P(low > threshold) works with the same
        // raw_ensemble_probability path.
        std::size_t iata_len = prefix.size() - 7;  // strip "KXTEMP" + trailing letter
        info.station_id = "K" + prefix.substr(6, iata_len);
    } else {
        info.valid = false;
        return info;
    }

    try {
        info.threshold = std::stod(match[3].str());
    } catch (...) {
        info.valid = false;
        return info;
    }

    info.valid = true;
    return info;
}

Probability WeatherEnsembleModel::estimate(const std::string& ticker, double threshold) {
    // Default source for this call; overridden on a settled-max hit below.
    // Stays "" if the ticker fails to parse so the strategy doesn't bucket a
    // garbage prediction under a valid source.
    last_source_.clear();

    auto info = parse_weather_ticker(ticker);
    if (!info.valid) {
        confidence_ = 0.0;
        rationale_ = "Invalid weather ticker: " + ticker;
        return 0.5;
    }
    last_source_ = "ensemble";  // default; may be promoted to "settled_max"

    // Settled-max override. If the observation tracker says the station's
    // daily max is locked AND the contract threshold is clearly on one side
    // of the observed max, the outcome is a near-certainty — bypass the
    // ensemble forecast entirely. This is the "settled-but-unpriced" edge:
    // the sun has set, temps are cooling, the max won't be beaten, but
    // Kalshi is still showing two-sided markets because CF6 hasn't dropped.
    //
    // Only applies when the contract date matches the station-local day we
    // have observations for — stale observations would misprice tomorrow's
    // contract. We approximate "matches today" by requiring the latest obs
    // to be within the last 4 hours of the current wall clock.
    if (obs_tracker_) {
        const auto* obs = obs_tracker_->get(info.station_id);
        if (obs && obs->locked) {
            auto obs_age = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::system_clock::now() - obs->latest_time_utc).count();
            if (obs_age < 240) {  // latest obs within 4 hours
                double gap = std::abs(threshold - obs->max_f);
                if (gap > settled_margin_f_) {
                    Probability p = (threshold < obs->max_f) ? 0.99 : 0.01;
                    confidence_ = 0.95;  // high but not 1.0 — late data errors happen
                    std::ostringstream oss;
                    oss << "Settled-max override: observed max " << obs->max_f
                        << "°F at " << info.station_id
                        << " vs threshold " << threshold << "°F (gap "
                        << gap << "°F); " << obs->reason;
                    rationale_ = oss.str();
                    last_source_ = "settled_max";
                    return p;
                }
            }
        }
    }

    // Look up per-model ensembles for the contract's settlement date and
    // produce one probability per expert in the aggregator's model order.
    // We iterate all stored forecasts and pick the nearest-date one per
    // model; if a model has no forecast at all we fall back to whatever
    // other model we do have (so the aggregator always sees a K-vector).
    std::string iso = ticker_date_to_iso(info.date);

    auto to_num = [](const std::string& d) -> int {
        if (d.size() < 10) return 0;
        return std::stoi(d.substr(0, 4) + d.substr(5, 2) + d.substr(8, 2));
    };
    const int target_num = iso.empty() ? 0 : to_num(iso);

    // For each expert slot, find the best-matching forecast.
    std::vector<const EnsembleForecast*> per_model(expert_models_.size(), nullptr);
    std::vector<int> per_model_dist(expert_models_.size(), INT_MAX);
    for (const auto& [key, forecast] : forecasts_) {
        if (forecast.station_id != info.station_id) continue;
        // Find this forecast's expert slot (by model name). If the forecast's
        // model isn't in our expert list (e.g., "historical_archive" from the
        // backtest), route it to slot 0 as a fallback so archive replays
        // still produce a probability.
        std::size_t slot = 0;
        bool matched = false;
        for (std::size_t i = 0; i < expert_models_.size(); ++i) {
            if (forecast.model == expert_models_[i]) {
                slot = i; matched = true; break;
            }
        }
        if (!matched) slot = 0;

        int dist;
        if (iso.empty() || forecast.target_date.empty()) {
            dist = 0;  // no date info; any forecast is equally "close"
        } else {
            dist = std::abs(target_num - to_num(forecast.target_date));
        }
        if (dist < per_model_dist[slot]) {
            per_model_dist[slot] = dist;
            per_model[slot] = &forecast;
        }
    }

    // Fallback: if some slots have no forecast, fill them with the first
    // non-null slot's forecast (so the aggregator sees a complete K-vector).
    const EnsembleForecast* fallback = nullptr;
    for (const auto* f : per_model) if (f) { fallback = f; break; }
    if (!fallback) {
        confidence_ = 0.0;
        rationale_ = "No ensemble data for station " + info.station_id +
                     (iso.empty() ? "" : " on " + iso);
        return 0.5;
    }
    for (auto*& f : per_model) if (!f) f = fallback;

    // Per-model probabilities. Apply bias correction to each independently
    // (bias is per-station, applied identically regardless of source model).
    auto bias_it = bias_.find(info.station_id);
    const double bias = (bias_it != bias_.end()) ? bias_it->second : 0.0;

    std::vector<double> expert_probs(expert_models_.size(), 0.5);
    int total_members = 0;
    for (std::size_t i = 0; i < per_model.size(); ++i) {
        auto corrected = per_model[i]->member_highs;
        if (bias != 0.0) corrected = apply_bias_correction(corrected, bias);
        expert_probs[i] = raw_ensemble_probability(corrected, threshold);
        total_members += per_model[i]->num_members;
    }

    // Aggregate the per-model probabilities via Vovk's Aggregating Algorithm.
    // Weights are updated online by on_settlement() so the aggregator learns
    // which model is more reliable for this station over time.
    double prob = aggregator_.predict(expert_probs);

    // Stash for later settlement — keyed by ticker because a given ticker
    // produces one settlement. If estimate() is called multiple times for
    // the same ticker (e.g., backtest decision points), the latest
    // prediction wins, which matches how settlement resolves.
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_predictions_[ticker] = expert_probs;
    }

    // Confidence: how confident should the bet-sizer be? Combine ensemble
    // agreement with member count. Use the blended prob for the agreement
    // term so we don't penalize cases where experts disagree but the
    // aggregator lands firmly on one side via weighted posterior.
    double agreement = std::abs(prob - 0.5) * 2.0;
    confidence_ = std::min(1.0, agreement * (total_members / (31.0 * expert_models_.size())));

    // Build rationale
    // Rationale: per-model probabilities + learned weights for the reader.
    auto weights = aggregator_.weights();
    std::ostringstream oss;
    oss << info.station_id << " > " << threshold << "°F: ";
    for (std::size_t i = 0; i < expert_models_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << expert_models_[i] << "=" << (expert_probs[i] * 100) << "% (w="
            << (weights[i] * 100) << "%)";
    }
    oss << " → blended " << (prob * 100) << "%";
    if (bias_it != bias_.end()) {
        oss << " [bias " << bias_it->second << "°F]";
    }
    rationale_ = oss.str();

    return prob;
}

void WeatherEnsembleModel::on_settlement(const std::string& ticker, bool outcome_yes) {
    std::vector<double> preds;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_predictions_.find(ticker);
        if (it == pending_predictions_.end()) return;  // never estimated this
        preds = std::move(it->second);
        pending_predictions_.erase(it);
    }
    // Feed the observed outcome back to the aggregator so per-model weights
    // adapt online. The sized K-vector was stored at estimate() time.
    aggregator_.observe(preds, outcome_yes ? 1 : 0);
}

// ===== ProbabilityEngine =====

void ProbabilityEngine::register_model(const std::string& category,
                                         std::shared_ptr<IProbabilityModel> model) {
    models_[category] = std::move(model);
}

Probability ProbabilityEngine::estimate(const std::string& ticker, const std::string& category,
                                          double threshold) {
    auto it = models_.find(category);
    if (it == models_.end()) {
        spdlog::warn("No model registered for category: {}", category);
        return 0.5;
    }
    return it->second->estimate(ticker, threshold);
}

std::shared_ptr<IProbabilityModel> ProbabilityEngine::get_model(const std::string& category) const {
    auto it = models_.find(category);
    if (it != models_.end()) return it->second;
    return nullptr;
}

std::vector<std::string> ProbabilityEngine::categories() const {
    std::vector<std::string> cats;
    for (const auto& [k, v] : models_) {
        cats.push_back(k);
    }
    return cats;
}

} // namespace trader::kalshi
