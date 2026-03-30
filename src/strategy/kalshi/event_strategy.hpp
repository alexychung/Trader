#pragma once

#include "strategy/istrategy.hpp"
#include "strategy/kalshi/probability_engine.hpp"
#include "strategy/kalshi/edge_detector.hpp"
#include "strategy/kalshi/market_filter.hpp"
#include "strategy/kalshi/calibration.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include "exchange/kalshi/ws_client.hpp"
#include "risk/risk_manager.hpp"
#include <vector>
#include <unordered_map>

namespace trader::kalshi {

class KalshiEventStrategy : public IStrategy {
public:
    KalshiEventStrategy(ProbabilityEngine& prob_engine,
                         EdgeDetector& edge_detector,
                         MarketFilter& market_filter,
                         RiskManager& risk_manager,
                         CalibrationLogger& calibration);

    // IStrategy interface
    void on_market_update(const MarketUpdate& update) override;
    void on_data_signal(const DataSignal& signal) override;
    void on_fill(const Fill& fill) override;
    void on_settlement(const Settlement& settlement) override;
    std::vector<TradeSignal> generate_signals() override;

    // Set available markets (from exchange)
    void set_markets(const std::vector<KalshiMarket>& markets);

    // Stats
    int signals_generated() const { return signals_generated_; }
    int trades_executed() const { return trades_executed_; }

private:
    ProbabilityEngine& prob_engine_;
    EdgeDetector& edge_detector_;
    MarketFilter& market_filter_;
    RiskManager& risk_manager_;
    CalibrationLogger& calibration_;

    std::unordered_map<std::string, KalshiMarket> markets_;
    std::unordered_map<std::string, MarketUpdate> latest_updates_;

    int signals_generated_ = 0;
    int trades_executed_ = 0;
};

} // namespace trader::kalshi
