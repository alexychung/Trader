#pragma once

#include "backtest/driver.hpp"
#include "exchange/mock_exchange.hpp"
#include "strategy/kalshi/calibration.hpp"
#include <string>
#include <unordered_map>

namespace trader::backtest {

class BacktestReport {
public:
    struct Config {
        std::string run_id;                       // empty => auto from timestamp
        std::string out_dir_base = "data/backtest";
        // Additional adverse fill cost applied to entry_price in PnL
        // computation. Models the reality that post-only orders can rest in
        // queue while the book moves against you, or that we cross as taker
        // and eat the near-book depth. Does NOT feed back into the
        // strategy's edge — only discounts reported PnL. Default matches
        // BacktestDriver::Config::adverse_fill_slippage; backtest_main.cpp
        // should propagate the driver config value here.
        double adverse_fill_slippage = 0.02;

        // Fee model. Kalshi maker=0.0175, taker=0.07 (~4×). Our April 2026
        // demo trials showed post-only orders at the ask get rejected with
        // "would cross" on thin books; in practice the strategy crosses as
        // taker, so the `maker_fee` recorded in each CalibrationRecord
        // underestimates reality. The report defaults to taker math to
        // avoid overstating PnL by ~2-3¢/contract. Flip to false only when
        // the strategy demonstrably rests on the book (posts at the bid,
        // not at the ask).
        bool assume_taker_fills = true;
    };

    struct Summary {
        int samples = 0;
        int real_trades = 0;
        int shadow_trades = 0;
        int resolved = 0;
        double brier_all = 0.0;              // model Brier over all records
        double brier_real = 0.0;             // model Brier over real trades
        double brier_market_baseline = 0.0;  // market-implied Brier, same samples as brier_all
        double hit_rate = 0.0;               // model_p > 0.5 and correct
        double hypothetical_pnl = 0.0;       // sum per-trade hypothetical pnl (after slippage)
        double fee_drag = 0.0;               // sum of maker fees (real trades)
        double slippage_drag = 0.0;          // sum of modelled adverse fill cost
        std::unordered_map<std::string, double> brier_by_category;
        std::unordered_map<std::string, double> brier_market_by_category;
    };

    BacktestReport(const trader::kalshi::CalibrationLogger& calibration,
                   const trader::MockExchange& exchange,
                   const BacktestDriver::RunResult& run_result,
                   Config config = {});

    Summary compute_summary() const;

    // Writes summary.json, calibration_curve.csv, trades.csv, coverage.json
    // under `out_dir_base/run_id/`. Returns the absolute directory path.
    std::string write();

private:
    const trader::kalshi::CalibrationLogger& calibration_;
    const trader::MockExchange& exchange_;
    const BacktestDriver::RunResult& run_result_;
    Config config_;
};

} // namespace trader::backtest
