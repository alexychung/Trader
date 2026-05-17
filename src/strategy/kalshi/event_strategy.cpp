#include "strategy/kalshi/event_strategy.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace trader::kalshi {

KalshiEventStrategy::KalshiEventStrategy(ProbabilityEngine& prob_engine,
                                           EdgeDetector& edge_detector,
                                           MarketFilter& market_filter,
                                           RiskManager& risk_manager,
                                           CalibrationLogger& calibration,
                                           AdaptiveSizer& sizer,
                                           ProbabilityCalibrator& prob_calibrator,
                                           StalenessGate& staleness,
                                           ClusterLimiter& cluster_limiter)
    : prob_engine_(prob_engine), edge_detector_(edge_detector),
      market_filter_(market_filter), risk_manager_(risk_manager),
      calibration_(calibration), sizer_(sizer),
      prob_calibrator_(prob_calibrator), staleness_(staleness),
      cluster_limiter_(cluster_limiter) {}

void KalshiEventStrategy::on_market_update(const MarketUpdate& update) {
    latest_updates_[update.ticker] = update;
    staleness_.on_market_update(update.ticker);
}

void KalshiEventStrategy::on_data_signal(const DataSignal& signal) {
    spdlog::debug("Strategy received signal: {} = {}", signal.label, signal.value);
    if (!signal.source.empty()) staleness_.on_feed_update(signal.source);
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

    // Propagate the outcome into the per-model BrierAggregator so GFS vs
    // ECMWF weights adapt online. Safe no-op for tickers we never priced.
    if (auto weather = prob_engine_.get_model("weather")) {
        if (auto* w = dynamic_cast<WeatherEnsembleModel*>(weather.get())) {
            w->on_settlement(settlement.ticker, settlement.outcome);
        }
    }
}

void KalshiEventStrategy::set_markets(const std::vector<KalshiMarket>& markets) {
    markets_.clear();
    for (const auto& m : markets) {
        markets_[m.ticker] = m;
        // Seed staleness from REST snapshot. Demo books rarely emit WS ticker
        // updates (no trading activity), so waiting on WS freshness means the
        // order path is permanently blocked. A recent REST fetch IS fresh
        // data and should satisfy the gate.
        staleness_.on_market_update(m.ticker);
    }
}

void KalshiEventStrategy::reset_daily() {
    exploration_spent_today_ = 0.0;
}

std::vector<TradeSignal> KalshiEventStrategy::generate_signals() {
    std::vector<TradeSignal> signals;

    // Refresh adaptive-sizer state from latest calibration (auto-disable / re-enable check).
    sizer_.refresh(calibration_);

    // Build merged market view (REST snapshot + WS overrides).
    std::vector<KalshiMarket> market_list;
    market_list.reserve(markets_.size());
    for (const auto& [ticker, market] : markets_) {
        KalshiMarket m = market;
        auto it = latest_updates_.find(ticker);
        if (it != latest_updates_.end()) {
            m.yes_bid = it->second.yes_bid;
            m.yes_ask = it->second.yes_ask;
            m.volume = it->second.volume;
        }
        market_list.push_back(m);
    }

    auto filtered = market_filter_.filter(market_list);

    double bankroll = risk_manager_.balance();
    double exploration_cap = sizer_.exploration_remaining(bankroll, exploration_spent_today_);

    for (const auto& fm : filtered) {
        const auto& market = fm.market;
        std::string category = market.category;

        // Determine model + raw probability.
        auto ticker_info = WeatherEnsembleModel::parse_weather_ticker(market.ticker);
        Probability raw_prob;
        if (ticker_info.valid) {
            raw_prob = prob_engine_.estimate(market.ticker, "weather", ticker_info.threshold);
            if (category.empty()) category = "weather";
        } else {
            raw_prob = prob_engine_.estimate(market.ticker, category, 0.0);
        }
        latest_probs_[market.ticker] = raw_prob;

        auto model = prob_engine_.get_model(category);
        double confidence = model ? model->confidence() : 0.0;
        const std::string source = model ? model->last_source() : "";

        // Bayesian shrinkage to market price using category track record.
        auto agg = calibration_.category_aggregate(category);
        double mid_price = (market.yes_bid + market.yes_ask) * 0.5;
        if (mid_price <= 0.0) mid_price = std::max(market.yes_ask, market.last_price);
        Probability adj_prob = prob_calibrator_.calibrate(category, raw_prob, mid_price, agg);

        // Compute effective Kelly scale for this category.
        double kelly_scale = sizer_.effective_kelly(category, /*base_kelly=*/1.0, agg);

        // Detect edge with the adjusted probability and per-category Kelly scale.
        auto edge = edge_detector_.detect(
            market.ticker, adj_prob,
            market.yes_bid, market.yes_ask, confidence,
            kelly_scale
        );

        // Stale-data gate: don't trade on a market we haven't heard from.
        bool stale = !staleness_.is_market_fresh(market.ticker);

        // Conservative posture: only settled-max signals can size real
        // positions. Ensemble predictions fall through to shadow-logging so
        // Brier tracking still runs. The source check is string-based
        // because models (weather/econ/fed) each define their own label set.
        const bool source_allowed_real = !settled_max_only_ || (source == "settled_max");

        bool placing_real = (edge.kelly_quantity > 0) && !stale && source_allowed_real;
        bool placing_probe = false;

        // Auto-disable category may still warrant a micro-probe to gather
        // fresh data. Probes are also gated on settled_max_only so we don't
        // leak real capital into untrusted ensemble edges during probe windows.
        if (!placing_real && sizer_.is_disabled(category) && sizer_.should_probe(category)
            && edge.edge >= 0.03 && !stale && source_allowed_real) {
            placing_probe = true;
        }

        // Cold-start exploration: if model produced a positive (but sub-threshold) signal AND
        // we haven't blown the exploration budget, take a tiny probe.
        // Also gated on settled_max_only — the whole point of that flag is to
        // prevent unproven ensemble trades; exploration is a real trade.
        bool placing_exploration = false;
        if (!placing_real && !placing_probe && !stale && source_allowed_real) {
            // Compute would-be 1-contract trade EV; require positive after fees.
            double side_prob = (edge.contract_side == "yes") ? adj_prob : (1.0 - adj_prob);
            double ev = EdgeDetector::net_ev_per_contract(side_prob, edge.market_price);
            double cost = edge.market_price;
            if (ev > 0 && cost > 0 && cost <= exploration_cap && agg.resolved_real < 30) {
                placing_exploration = true;
            }
        }

        if (placing_real || placing_probe || placing_exploration) {
            // kelly_quantity is the TARGET position. Subtract what we already
            // hold so we only order the delta. Without this, each tick
            // re-emits the full target and we stack orders into the same
            // ticker until the per-market risk cap is hit. Probe/exploration
            // sizes are absolute per-shot quantities (small), not targets —
            // leave those alone.
            int qty;
            if (placing_real) {
                int current = risk_manager_.position_quantity(market.ticker);
                qty = std::max(0, edge.kelly_quantity - current);
                if (qty == 0) {
                    // Already at target — nothing to do for this ticker.
                    continue;
                }
            } else {
                qty = placing_exploration ? sizer_.config().exploration_max_qty
                                          : sizer_.config().probe_max_qty;
            }

            auto risk_check = risk_manager_.check_trade(market.ticker, qty, edge.market_price);
            double additional_cost = qty * edge.market_price;
            std::string cluster_reject = cluster_limiter_.reject_reason(market.ticker, additional_cost);
            if (!risk_check.allowed || !cluster_reject.empty()) {
                std::string reason = !risk_check.allowed ? risk_check.reason : cluster_reject;
                spdlog::debug("Trade rejected {} : {}", market.ticker, reason);
                // Log a shadow prediction so calibration data isn't biased toward executed trades.
                CalibrationRecord shadow;
                shadow.market_ticker = market.ticker;
                shadow.category = category;
                shadow.trade_time = std::chrono::system_clock::now();
                shadow.model_probability = adj_prob;
                shadow.market_price = edge.market_price;
                shadow.edge = edge.edge;
                shadow.side = edge.contract_side;
                shadow.model_source = model ? model->last_source() : "";
                calibration_.log_shadow_prediction(shadow);
                ++shadows_logged_;
                continue;
            }

            TradeSignal signal;
            signal.ticker = market.ticker;
            signal.side = edge.side;
            signal.contract_side = edge.contract_side;
            signal.model_probability = adj_prob;
            signal.market_price = edge.market_price;
            // Snapshot top-of-book so the caller can compute a maker-safe
            // limit price (see kalshi_maker_post_price). Uses the merged
            // REST+WS view we already built above; lives on the signal so
            // main.cpp doesn't need its own market-cache read.
            signal.yes_bid_snapshot = market.yes_bid;
            signal.yes_ask_snapshot = market.yes_ask;
            signal.edge = edge.edge;
            signal.confidence = confidence;
            signal.quantity = qty;
            signal.rationale = edge.rationale;
            if (placing_probe) signal.rationale += " [probe]";
            if (placing_exploration) signal.rationale += " [exploration]";
            signals.push_back(signal);

            CalibrationRecord cal;
            cal.market_ticker = market.ticker;
            cal.category = category;
            cal.trade_time = std::chrono::system_clock::now();
            cal.model_probability = adj_prob;
            cal.market_price = edge.market_price;
            cal.edge = edge.edge;
            cal.side = edge.contract_side;
            cal.quantity = qty;
            cal.entry_price = edge.market_price;
            cal.maker_fee = kalshi_maker_fee(qty, edge.market_price);
            cal.is_exploration = (placing_probe || placing_exploration);
            cal.model_source = model ? model->last_source() : "";
            calibration_.log_trade(cal);

            sizer_.on_trade(category);

            if (placing_exploration) {
                exploration_spent_today_ += qty * edge.market_price;
                exploration_cap = sizer_.exploration_remaining(bankroll, exploration_spent_today_);
                ++exploration_taken_;
            }

            ++signals_generated_;
        } else {
            // No trade — but if model produced a meaningful prediction, log a
            // shadow record. Avoids selection bias: real-trade calibration is
            // biased toward overconfident markets where edge passed threshold;
            // shadows level it out.
            //
            // Two guards:
            //   1. Model must have produced a real signal — skip neutral 0.5
            //      defaults that WeatherEnsembleModel returns on missing data.
            //   2. Book must be real — demo markets frequently quote
            //      yes_bid=0.00 / yes_ask=1.00 (a placeholder "anybody home"
            //      pair, not liquidity). Shadows from these just pollute
            //      calibration with a meaningless 0.50 midpoint.
            const bool real_signal     = (raw_prob != 0.5) && (confidence > 0.0);
            const double spread        = market.yes_ask - market.yes_bid;
            const bool real_book       = market.yes_bid > 0.0 &&
                                         market.yes_ask < 1.0 &&
                                         spread < 0.95;

            // Dedup: skip if we've recently shadowed this ticker at a similar
            // model probability. A contract only needs one shadow per
            // meaningful prediction; 48 identical rows per day adds no
            // information to Brier and fills calibration.json with noise.
            // Override the dedup gate when the model moves materially — a
            // real regime shift should still be captured.
            const auto now = std::chrono::system_clock::now();
            bool dedup_skip = false;
            if (real_signal && real_book) {
                auto it = last_shadow_.find(market.ticker);
                if (it != last_shadow_.end()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - it->second.when);
                    double prob_move = std::abs(adj_prob - it->second.model_prob);
                    if (elapsed < shadow_min_interval_ &&
                        prob_move < shadow_prob_move_threshold_) {
                        dedup_skip = true;
                    }
                }
            }

            if (real_signal && real_book && !dedup_skip) {
                CalibrationRecord shadow;
                shadow.market_ticker = market.ticker;
                shadow.category = category;
                shadow.trade_time = now;
                shadow.model_probability = adj_prob;
                shadow.market_price = mid_price;
                shadow.edge = edge.edge;
                shadow.side = edge.contract_side.empty() ? "yes" : edge.contract_side;
                shadow.model_source = model ? model->last_source() : "";
                calibration_.log_shadow_prediction(shadow);
                last_shadow_[market.ticker] = {now, adj_prob};
                ++shadows_logged_;
            }
        }
    }

    return signals;
}

} // namespace trader::kalshi
