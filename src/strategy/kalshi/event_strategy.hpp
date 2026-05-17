#pragma once

#include "strategy/istrategy.hpp"
#include "strategy/kalshi/probability_engine.hpp"
#include "strategy/kalshi/edge_detector.hpp"
#include "strategy/kalshi/market_filter.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "strategy/kalshi/adaptive_sizer.hpp"
#include "strategy/kalshi/probability_calibrator.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include "exchange/kalshi/ws_client.hpp"
#include "core/staleness_gate.hpp"
#include "risk/risk_manager.hpp"
#include "risk/cluster_limiter.hpp"
#include <vector>
#include <unordered_map>
#include <chrono>

namespace trader::kalshi {

class KalshiEventStrategy : public IStrategy {
public:
    KalshiEventStrategy(ProbabilityEngine& prob_engine,
                         EdgeDetector& edge_detector,
                         MarketFilter& market_filter,
                         RiskManager& risk_manager,
                         CalibrationLogger& calibration,
                         AdaptiveSizer& sizer,
                         ProbabilityCalibrator& prob_calibrator,
                         StalenessGate& staleness,
                         ClusterLimiter& cluster_limiter);

    // IStrategy interface
    void on_market_update(const MarketUpdate& update) override;
    void on_data_signal(const DataSignal& signal) override;
    void on_fill(const Fill& fill) override;
    void on_settlement(const Settlement& settlement) override;
    std::vector<TradeSignal> generate_signals() override;

    // Set available markets (from exchange)
    void set_markets(const std::vector<KalshiMarket>& markets);

    // Latest model probabilities per ticker (used by OrderManager for force-exit).
    std::unordered_map<std::string, Probability> current_model_probs() const { return latest_probs_; }
    std::unordered_map<std::string, MarketUpdate> latest_market_updates() const { return latest_updates_; }

    // Stats
    int signals_generated() const { return signals_generated_; }
    int trades_executed() const { return trades_executed_; }
    int shadows_logged() const { return shadows_logged_; }
    int exploration_taken() const { return exploration_taken_; }

    // Reset daily exploration spent (call when day rolls over).
    void reset_daily();

    // When true, only signals whose model last_source == "settled_max" may
    // fire real trades. Ensemble predictions still shadow-log for Brier
    // calibration but never size positions. Defaults to false — main.cpp
    // reads StrategyConfig::settled_max_only from the YAML config and flips
    // this on for live trading. Tests and backtest leave it off so they can
    // score full-ensemble signals against outcomes.
    void set_settled_max_only(bool enabled) { settled_max_only_ = enabled; }
    bool settled_max_only() const { return settled_max_only_; }

private:
    ProbabilityEngine& prob_engine_;
    EdgeDetector& edge_detector_;
    MarketFilter& market_filter_;
    RiskManager& risk_manager_;
    CalibrationLogger& calibration_;
    AdaptiveSizer& sizer_;
    ProbabilityCalibrator& prob_calibrator_;
    StalenessGate& staleness_;
    ClusterLimiter& cluster_limiter_;

    std::unordered_map<std::string, KalshiMarket> markets_;
    std::unordered_map<std::string, MarketUpdate> latest_updates_;
    std::unordered_map<std::string, Probability> latest_probs_;

    // Shadow-dedup cache. A shadow prediction carries no new information
    // when the same ticker, model, and market price repeat tick after tick —
    // overnight we saw one ticker shadowed ~48 times with identical values,
    // bloating calibration.json and flooding logs without adding Brier-
    // score signal. Record each shadow's (time, model prob) per ticker and
    // skip if we saw one recently whose model prob hasn't moved much.
    struct ShadowState {
        Timestamp when;
        Probability model_prob = 0.0;
    };
    std::unordered_map<std::string, ShadowState> last_shadow_;
    // Min gap before the same ticker gets another shadow with similar prob.
    // 6h captures intraday model drift while cutting ~60x noise at 60s ticks.
    std::chrono::seconds shadow_min_interval_{6 * 3600};
    // If the model prob moves this much, log anyway regardless of interval.
    double shadow_prob_move_threshold_ = 0.05;

    int signals_generated_ = 0;
    int trades_executed_ = 0;
    int shadows_logged_ = 0;
    int exploration_taken_ = 0;
    double exploration_spent_today_ = 0.0;

    bool settled_max_only_ = false;
};

} // namespace trader::kalshi
