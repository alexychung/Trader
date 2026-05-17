#pragma once

#include "core/types.hpp"
#include "strategy/kalshi/weather_feed.hpp"
#include "strategy/kalshi/observations.hpp"
#include "strategy/kalshi/brier_aggregator.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <mutex>

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

    // Which sub-model actually produced the last estimate(). Lets the caller
    // record per-source accuracy without knowing the model's internals.
    // Default: the category name. WeatherEnsembleModel overrides this to
    // distinguish "ensemble" from "settled_max".
    virtual std::string last_source() const { return category(); }
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
    std::string last_source() const override { return last_source_; }

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

    // Optional observation tracker. When provided, estimate() first checks
    // whether the station's daily max is locked (post-peak cooling detected);
    // if so and the threshold is clearly on one side, returns a near-certain
    // probability with high confidence instead of the forward ensemble.
    // Pass nullptr to disable. The tracker is owned by the caller.
    void set_observations_tracker(StationObservationsTracker* t) { obs_tracker_ = t; }

    // Record a settlement. Updates the per-model BrierAggregator weights from
    // the predictions stored at estimate() time. Called by the strategy layer
    // on its on_settlement callback. Silent no-op for tickers we never
    // estimated (e.g., settled before the bot started).
    void on_settlement(const std::string& ticker, bool outcome_yes);

    // Diagnostic: current per-model weights learned by the aggregator.
    // Ordering matches the `expert_models_` list (defaults: ecmwf, gfs).
    std::vector<double> aggregator_weights() const {
        return aggregator_.weights();
    }
    std::vector<std::string> aggregator_model_order() const {
        return expert_models_;
    }

    // Settled-max gating knobs. A contract threshold within
    // `settled_margin_f` of the observed max is treated as ambiguous — we
    // fall back to the ensemble rather than declare a near-certain outcome.
    void set_settled_margin_f(double f) { settled_margin_f_ = f; }

private:
    // Ensemble data keyed by "station_date_model" (e.g.,
    // "KNYC_2026-04-26_ecmwf"). Prior code keyed on "station_date" alone —
    // when Open-Meteo returned separate GFS + ECMWF ensembles, the second
    // set_ensemble() silently clobbered the first. Keying by model keeps
    // both and lets estimate() blend them per-market via BrierAggregator.
    // Legacy callers (archive cache) that leave model empty store under
    // station_date_ — still unique so long as they don't overlap with a
    // live-model insertion.
    static std::string forecast_key(const std::string& station_id,
                                     const std::string& target_date,
                                     const std::string& model) {
        return station_id + "_" + target_date + "_" + model;
    }
    std::unordered_map<std::string, EnsembleForecast> forecasts_;
    // Bias correction per station
    std::unordered_map<std::string, double> bias_;
    double confidence_ = 0.0;
    std::string rationale_;

    StationObservationsTracker* obs_tracker_ = nullptr;
    double settled_margin_f_ = 2.0;  // |threshold - max| must exceed this

    // Set per estimate() call; "ensemble", "settled_max", or "" when the
    // ticker didn't parse as a weather contract.
    std::string last_source_;

    // Fixed expert order. The aggregator works with K=expert_models_.size()
    // experts; predictions are built in this order. Keep ECMWF first — it
    // historically has ~60/40 edge over GFS on temperature (sister doc §3.1),
    // and initial weights are uniform so the starting bias is zero anyway.
    // A missing model on a given day is filled with the other model's
    // prediction (so the aggregator always sees a K-length vector).
    std::vector<std::string> expert_models_{"ecmwf", "gfs"};
    BrierAggregator aggregator_{expert_models_.size(), 2.0, 0.01};

    // Stored per-ticker predictions so on_settlement() can replay them into
    // the aggregator's observe(). Cleared after each settlement (no need to
    // keep once the weights have been updated).
    std::unordered_map<std::string, std::vector<double>> pending_predictions_;
    // Guards pending_predictions_ only; aggregator_ has its own lock.
    mutable std::mutex pending_mutex_;
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
