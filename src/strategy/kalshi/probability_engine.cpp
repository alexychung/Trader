#include "strategy/kalshi/probability_engine.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <regex>

namespace trader::kalshi {

// ===== WeatherEnsembleModel =====

void WeatherEnsembleModel::set_ensemble(const EnsembleForecast& forecast) {
    std::string key = forecast.station_id + "_" + forecast.target_date;
    forecasts_[key] = forecast;
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

WeatherEnsembleModel::TickerInfo WeatherEnsembleModel::parse_weather_ticker(const std::string& ticker) {
    TickerInfo info;

    // Map ticker prefix to station ID
    static const std::unordered_map<std::string, std::string> prefix_to_station = {
        {"KXHIGHNY", "KNYC"},
        {"KXHIGHCHI", "KORD"},
        {"KXHIGHMIA", "KMIA"},
        {"KXHIGHLAX", "KLAX"},
        {"KXHIGHDEN", "KDEN"},
        {"KXHIGHAUS", "KAUS"},
    };

    // Pattern: KXHIGHNY-26APR-T75 or KXHIGHCHI-26APR-T60
    std::regex re(R"((KXHIGH\w+)-(\d{2}\w{3})-T(\d+))");
    std::smatch match;

    if (!std::regex_match(ticker, match, re)) {
        info.valid = false;
        return info;
    }

    std::string prefix = match[1].str();
    info.date = match[2].str();

    auto it = prefix_to_station.find(prefix);
    if (it != prefix_to_station.end()) {
        info.station_id = it->second;
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
    auto info = parse_weather_ticker(ticker);
    if (!info.valid) {
        confidence_ = 0.0;
        rationale_ = "Invalid weather ticker: " + ticker;
        return 0.5;
    }

    // Look for ensemble data for this station (check all dates for best match)
    std::string prefix = info.station_id + "_";
    const EnsembleForecast* best = nullptr;
    for (const auto& [key, forecast] : forecasts_) {
        if (key.find(prefix) == 0) {
            // Use the forecast if we don't have one yet, or if it's newer
            if (!best || forecast.num_members > best->num_members) {
                best = &forecast;
            }
        }
    }

    if (!best || best->member_highs.empty()) {
        confidence_ = 0.0;
        rationale_ = "No ensemble data for station " + info.station_id;
        return 0.5;
    }

    // Apply bias correction
    auto corrected = best->member_highs;
    auto bias_it = bias_.find(info.station_id);
    if (bias_it != bias_.end()) {
        corrected = apply_bias_correction(corrected, bias_it->second);
    }

    // Compute probability
    double prob = raw_ensemble_probability(corrected, threshold);

    // Compute confidence based on number of members and ensemble agreement
    double agreement = std::abs(prob - 0.5) * 2.0;  // 0 to 1 (0 = split, 1 = unanimous)
    confidence_ = std::min(1.0, agreement * (best->num_members / 31.0));

    // Build rationale
    int above = 0;
    for (double h : corrected) {
        if (h > threshold) ++above;
    }
    std::ostringstream oss;
    oss << above << "/" << corrected.size() << " ensemble members forecast high > "
        << threshold << "°F for " << info.station_id;
    if (bias_it != bias_.end()) {
        oss << " (bias correction: " << bias_it->second << "°F)";
    }
    rationale_ = oss.str();

    return prob;
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
