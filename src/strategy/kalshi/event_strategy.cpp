#include "strategy/kalshi/event_strategy.hpp"
#include <spdlog/spdlog.h>

namespace trader::kalshi {

KalshiEventStrategy::KalshiEventStrategy(ProbabilityEngine& prob_engine,
                                           EdgeDetector& edge_detector,
                                           MarketFilter& market_filter,
                                           RiskManager& risk_manager,
                                           CalibrationLogger& calibration)
    : prob_engine_(prob_engine), edge_detector_(edge_detector),
      market_filter_(market_filter), risk_manager_(risk_manager),
      calibration_(calibration) {}

void KalshiEventStrategy::on_market_update(const MarketUpdate& update) {
    latest_updates_[update.ticker] = update;
}

void KalshiEventStrategy::on_data_signal(const DataSignal& signal) {
    spdlog::debug("Strategy received signal: {} = {}", signal.label, signal.value);
}

void KalshiEventStrategy::on_fill(const Fill& fill) {
    ++trades_executed_;
    spdlog::info("Strategy fill: {} {} {}x @ ${:.4f}",
                 fill.ticker, to_string(fill.side), fill.quantity, fill.price);
}

void KalshiEventStrategy::on_settlement(const Settlement& settlement) {
    calibration_.resolve(settlement.ticker, settlement.outcome, settlement.pnl);
    spdlog::info("Strategy settlement: {} → {} (PnL: ${:.2f})",
                 settlement.ticker, settlement.outcome ? "YES" : "NO", settlement.pnl);
}

void KalshiEventStrategy::set_markets(const std::vector<KalshiMarket>& markets) {
    markets_.clear();
    for (const auto& m : markets) {
        markets_[m.ticker] = m;
    }
}

std::vector<TradeSignal> KalshiEventStrategy::generate_signals() {
    std::vector<TradeSignal> signals;

    // Convert markets to vector for filtering
    std::vector<KalshiMarket> market_list;
    for (const auto& [ticker, market] : markets_) {
        // Update with latest WS data if available
        KalshiMarket m = market;
        auto it = latest_updates_.find(ticker);
        if (it != latest_updates_.end()) {
            m.yes_bid = it->second.yes_bid;
            m.yes_ask = it->second.yes_ask;
            m.volume = it->second.volume;
        }
        market_list.push_back(m);
    }

    // Filter markets
    auto filtered = market_filter_.filter(market_list);

    for (const auto& fm : filtered) {
        const auto& market = fm.market;

        // Get model probability
        double threshold = 0.0;  // Extracted from ticker in real implementation
        auto ticker_info = WeatherEnsembleModel::parse_weather_ticker(market.ticker);
        std::string category = market.category;

        Probability model_prob;
        if (ticker_info.valid) {
            model_prob = prob_engine_.estimate(market.ticker, "weather", ticker_info.threshold);
        } else {
            model_prob = prob_engine_.estimate(market.ticker, category, threshold);
        }

        // Detect edge
        auto model = prob_engine_.get_model(category);
        double confidence = model ? model->confidence() : 0.0;

        auto edge = edge_detector_.detect(
            market.ticker, model_prob,
            market.yes_bid, market.yes_ask, confidence
        );

        if (edge.kelly_quantity > 0) {
            // Check risk gate
            auto risk_check = risk_manager_.check_trade(
                market.ticker, edge.kelly_quantity, edge.market_price);

            if (risk_check.allowed) {
                TradeSignal signal;
                signal.ticker = market.ticker;
                signal.side = edge.side;
                signal.contract_side = edge.contract_side;
                signal.model_probability = model_prob;
                signal.market_price = edge.market_price;
                signal.edge = edge.edge;
                signal.confidence = confidence;
                signal.quantity = edge.kelly_quantity;
                signal.rationale = edge.rationale;

                signals.push_back(signal);

                // Log for calibration
                CalibrationRecord cal;
                cal.market_ticker = market.ticker;
                cal.category = category;
                cal.trade_time = std::chrono::system_clock::now();
                cal.model_probability = model_prob;
                cal.market_price = edge.market_price;
                cal.edge = edge.edge;
                cal.side = edge.contract_side;
                cal.quantity = edge.kelly_quantity;
                cal.entry_price = edge.market_price;
                cal.maker_fee = kalshi_maker_fee(edge.kelly_quantity, edge.market_price);
                calibration_.log_trade(cal);

                ++signals_generated_;
            } else {
                spdlog::debug("Risk rejected {} : {}", market.ticker, risk_check.reason);
            }
        }
    }

    return signals;
}

} // namespace trader::kalshi
