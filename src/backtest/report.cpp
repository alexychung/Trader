#include "backtest/report.hpp"
#include "core/types.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace trader::backtest {

namespace fs = std::filesystem;

namespace {

std::string default_run_id() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

} // namespace

BacktestReport::BacktestReport(const trader::kalshi::CalibrationLogger& calibration,
                                 const trader::MockExchange& exchange,
                                 const BacktestDriver::RunResult& run_result,
                                 Config config)
    : calibration_(calibration), exchange_(exchange),
      run_result_(run_result), config_(std::move(config)) {
    if (config_.run_id.empty()) config_.run_id = default_run_id();
}

BacktestReport::Summary BacktestReport::compute_summary() const {
    Summary s;
    auto records = calibration_.records();
    s.samples = static_cast<int>(records.size());

    double model_sse_all = 0.0, market_sse_all = 0.0;
    double model_sse_real = 0.0;
    int n_all = 0, n_real = 0;
    int hits = 0, hit_eligible = 0;

    std::unordered_map<std::string, double> cat_model_sse, cat_market_sse;
    std::unordered_map<std::string, int> cat_n;

    for (const auto& r : records) {
        if (!r.is_shadow) ++s.real_trades; else ++s.shadow_trades;
        if (!r.outcome.has_value()) continue;
        ++s.resolved;

        double model_p = (r.side == "no") ? (1.0 - r.model_probability) : r.model_probability;
        double market_p = (r.side == "no") ? (1.0 - r.market_price) : r.market_price;
        bool won = (r.side == "yes") ? *r.outcome : !*r.outcome;
        double actual = won ? 1.0 : 0.0;

        double model_err = model_p - actual;
        double market_err = market_p - actual;
        model_sse_all += model_err * model_err;
        market_sse_all += market_err * market_err;
        ++n_all;

        cat_model_sse[r.category] += model_err * model_err;
        cat_market_sse[r.category] += market_err * market_err;
        cat_n[r.category] += 1;

        if (model_p >= 0.55) {  // "strong" signal — model thinks our side wins
            ++hit_eligible;
            if (won) ++hits;
        }

        if (!r.is_shadow) {
            model_sse_real += model_err * model_err;
            ++n_real;

            // Adverse fill slippage: assume we paid `slippage` more per
            // contract than the quoted entry_price. On a winning trade this
            // simply reduces payout; on a losing trade it adds to our loss.
            // Symmetric either way — it's a cost subtracted from PnL.
            double slip = config_.adverse_fill_slippage * r.quantity;
            double pnl_win = (1.0 - r.entry_price) * r.quantity;
            double pnl_loss = -r.entry_price * r.quantity;
            double trade_pnl = won ? pnl_win : pnl_loss;

            // Fee math: taker by default (see Config::assume_taker_fills
            // rationale). r.maker_fee was computed at trade-log time; when
            // assume_taker_fills is true, scale it up to taker by the rate
            // ratio. Doing this at report time (rather than re-logging) lets
            // us flip the assumption without re-running the backtest.
            double per_side_fee = r.maker_fee;
            if (config_.assume_taker_fills) {
                per_side_fee *= (kKalshiDefaultTakerRate /
                                 kKalshiDefaultMakerRate);
            }
            s.hypothetical_pnl += trade_pnl - 2.0 * per_side_fee - slip;
            s.fee_drag += 2.0 * per_side_fee;
            s.slippage_drag += slip;
        }
    }

    s.brier_all = n_all > 0 ? model_sse_all / n_all : 0.0;
    s.brier_market_baseline = n_all > 0 ? market_sse_all / n_all : 0.0;
    s.brier_real = n_real > 0 ? model_sse_real / n_real : 0.0;
    s.hit_rate = hit_eligible > 0 ? static_cast<double>(hits) / hit_eligible : 0.0;

    for (const auto& [cat, n] : cat_n) {
        if (n == 0) continue;
        s.brier_by_category[cat] = cat_model_sse[cat] / n;
        s.brier_market_by_category[cat] = cat_market_sse[cat] / n;
    }
    return s;
}

std::string BacktestReport::write() {
    auto dir = fs::path(config_.out_dir_base) / config_.run_id;
    std::error_code ec;
    fs::create_directories(dir, ec);

    auto summary = compute_summary();

    {
        nlohmann::json j;
        j["run_id"] = config_.run_id;
        j["samples"] = summary.samples;
        j["resolved"] = summary.resolved;
        j["real_trades"] = summary.real_trades;
        j["shadow_trades"] = summary.shadow_trades;
        j["brier"] = {
            {"all", summary.brier_all},
            {"real", summary.brier_real},
            {"market_baseline", summary.brier_market_baseline}
        };
        j["hit_rate_strong"] = summary.hit_rate;
        j["hypothetical_pnl"] = summary.hypothetical_pnl;
        j["fee_drag"] = summary.fee_drag;
        j["slippage_drag"] = summary.slippage_drag;
        j["adverse_fill_slippage_per_contract"] = config_.adverse_fill_slippage;
        j["driver"] = {
            {"decisions_made", run_result_.decisions_made},
            {"settlements_applied", run_result_.settlements_applied},
            {"orders_placed", run_result_.orders_placed}
        };
        j["exchange"] = {
            {"orders_logged", static_cast<int>(exchange_.order_count())}
        };
        nlohmann::json bycat = nlohmann::json::object();
        for (const auto& [cat, br] : summary.brier_by_category) {
            auto it = summary.brier_market_by_category.find(cat);
            double mb = it == summary.brier_market_by_category.end() ? 0.0 : it->second;
            bycat[cat] = {
                {"brier_model", br},
                {"brier_market", mb},
                {"edge_vs_market", mb - br}  // positive = model beats market
            };
        }
        j["by_category"] = bycat;

        std::ofstream out(dir / "summary.json");
        out << j.dump(2);
    }

    {
        std::ofstream out(dir / "calibration_curve.csv");
        out << "bucket_center,avg_prediction,actual_frequency,sample_count\n";
        auto curve = calibration_.calibration_curve();
        for (const auto& b : curve) {
            out << b.bucket_center << "," << b.avg_prediction << ","
                << b.actual_frequency << "," << b.sample_count << "\n";
        }
    }

    {
        std::ofstream out(dir / "trades.csv");
        out << "ticker,category,decision_time_epoch,model_prob,market_price,edge,side,"
               "quantity,entry_price,is_shadow,is_exploration,outcome,trade_pnl,maker_fee\n";
        auto records = calibration_.records();
        for (const auto& r : records) {
            double trade_pnl = 0.0;
            std::string outcome_str = "";
            if (r.outcome.has_value()) {
                bool won = (r.side == "yes") ? *r.outcome : !*r.outcome;
                outcome_str = *r.outcome ? "yes" : "no";
                if (!r.is_shadow) {
                    double per_side_fee = r.maker_fee;
                    if (config_.assume_taker_fills) {
                        per_side_fee *= (kKalshiDefaultTakerRate /
                                         kKalshiDefaultMakerRate);
                    }
                    trade_pnl = (won ? (1.0 - r.entry_price) : -r.entry_price) * r.quantity
                                - 2.0 * per_side_fee
                                - config_.adverse_fill_slippage * r.quantity;
                }
            }
            auto trade_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                r.trade_time.time_since_epoch()).count();
            out << r.market_ticker << "," << r.category << "," << trade_epoch << ","
                << r.model_probability << "," << r.market_price << "," << r.edge << ","
                << r.side << "," << r.quantity << "," << r.entry_price << ","
                << (r.is_shadow ? 1 : 0) << "," << (r.is_exploration ? 1 : 0) << ","
                << outcome_str << "," << trade_pnl << "," << r.maker_fee << "\n";
        }
    }

    {
        nlohmann::json j;
        j["total_markets"] = static_cast<int>(run_result_.coverage.size());
        int attempted = 0, skipped = 0;
        std::unordered_map<std::string, int> skip_reasons;
        nlohmann::json per_market = nlohmann::json::array();
        for (const auto& c : run_result_.coverage) {
            nlohmann::json e;
            e["ticker"] = c.ticker;
            e["category"] = c.category;
            e["attempted"] = c.attempted;
            e["decisions"] = c.decisions;
            if (!c.skip_reason.empty()) e["skip_reason"] = c.skip_reason;
            per_market.push_back(e);
            if (c.attempted) ++attempted;
            else { ++skipped; if (!c.skip_reason.empty()) skip_reasons[c.skip_reason]++; }
        }
        j["attempted"] = attempted;
        j["skipped"] = skipped;
        j["skip_reasons"] = skip_reasons;
        j["markets"] = per_market;

        std::ofstream out(dir / "coverage.json");
        out << j.dump(2);
    }

    spdlog::info("Backtest report written to {}", dir.string());
    return dir.string();
}

} // namespace trader::backtest
