#pragma once

#include "core/types.hpp"
#include "strategy/kalshi/weather_feed.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace trader::kalshi {

// Interface for probability models
class IProbabilityModel {
public:
    virtual ~IProbabilityModel() = default;

    // Estimate probability for a market event
    virtual Probability estimate(const std::string& ticker, double threshold) = 0;

    // Model confidence (0-1). Low = don't trade.
    virtual double confidence() const = 0;

    // Human-readable rationale
    virtual std::string rationale() const = 0;

    // Category this model handles
    virtual std::string category() const = 0;
};

// Weather ensemble model — the primary edge source (9/10 rating)
class WeatherEnsembleModel : public IProbabilityModel {
public:
    // Set ensemble data for a station/date
    void set_ensemble(const EnsembleForecast& forecast);

    // Set bias correction (rolling residual mean)
    void set_bias_correction(const std::string& station_id, double bias_f);

    // Estimate P(daily high > threshold) using calibrated ensemble
    Probability estimate(const std::string& ticker, double threshold) override;

    double confidence() const override { return confidence_; }
    std::string rationale() const override { return rationale_; }
    std::string category() const override { return "weather"; }

    // Raw ensemble counting (no calibration)
    static Probability raw_ensemble_probability(const std::vector<double>& member_highs,
                                                  double threshold);

    // Apply bias correction to member highs
    static std::vector<double> apply_bias_correction(const std::vector<double>& member_highs,
                                                       double bias);

    // Forecast horizon scaling: wider uncertainty for further-out forecasts
    // sigma(days) returns temperature error std dev in °F
    static double horizon_sigma(int days_ahead);

    // Blend GFS + ECMWF probabilities (ECMWF weight 0.6, GFS 0.4)
    static Probability blend_models(Probability gfs_prob, Probability ecmwf_prob,
                                      double gfs_weight = 0.4, double ecmwf_weight = 0.6);

    // Parse ticker to extract station and threshold
    // e.g., "KXHIGHNY-26APR-T75" → station="KNYC", threshold=75.0
    struct TickerInfo {
        std::string station_id;
        double threshold = 0.0;
        std::string date;
        bool valid = false;
    };
    static TickerInfo parse_weather_ticker(const std::string& ticker);

private:
    // Ensemble data keyed by "station_date" (e.g., "KNYC_2026-04-26")
    std::unordered_map<std::string, EnsembleForecast> forecasts_;
    // Bias correction per station
    std::unordered_map<std::string, double> bias_;
    double confidence_ = 0.0;
    std::string rationale_;
};

// Probability engine — dispatches to models by category
class ProbabilityEngine {
public:
    void register_model(const std::string& category, std::shared_ptr<IProbabilityModel> model);

    // Estimate probability for a market
    Probability estimate(const std::string& ticker, const std::string& category, double threshold);

    // Get model for category
    std::shared_ptr<IProbabilityModel> get_model(const std::string& category) const;

    // List registered categories
    std::vector<std::string> categories() const;

private:
    std::unordered_map<std::string, std::shared_ptr<IProbabilityModel>> models_;
};

} // namespace trader::kalshi
