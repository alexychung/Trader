#include "strategy/kalshi/econ_feed.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace trader::kalshi {

// ===== BLS Client =====

std::vector<BlsDataPoint> BlsClient::fetch_series(const std::string& series_id,
                                                    const std::string& start_year,
                                                    const std::string& end_year) {
    spdlog::debug("BLS fetch: {} ({}-{})", series_id, start_year, end_year);
    return {};  // Production: HTTP POST to api.bls.gov
}

std::vector<BlsDataPoint> BlsClient::parse_response(const nlohmann::json& j,
                                                       const std::string& series_id) {
    std::vector<BlsDataPoint> points;

    // BLS API v2 response structure:
    // { "Results": { "series": [ { "seriesID": "...", "data": [ { "year": "2026", "period": "M03", "value": "311.054" } ] } ] } }
    if (!j.contains("Results") || !j["Results"].contains("series")) return points;

    for (const auto& series : j["Results"]["series"]) {
        std::string sid = series.value("seriesID", "");
        if (!series_id.empty() && sid != series_id) continue;

        if (!series.contains("data") || !series["data"].is_array()) continue;

        for (const auto& dp : series["data"]) {
            BlsDataPoint point;
            point.series_id = sid;
            point.year = dp.value("year", "");
            point.period = dp.value("period", "");

            // Value is a string in BLS API
            std::string val_str = dp.value("value", "0");
            try {
                point.value = std::stod(val_str);
            } catch (...) {
                continue;
            }

            if (dp.contains("footnotes") && dp["footnotes"].is_array() && !dp["footnotes"].empty()) {
                point.footnotes = dp["footnotes"][0].value("text", "");
            }

            points.push_back(point);
        }
    }

    // BLS returns newest first — sort oldest first for time-series analysis
    std::reverse(points.begin(), points.end());
    return points;
}

std::optional<double> BlsClient::compute_yoy_change(const std::vector<BlsDataPoint>& data) {
    if (data.size() < 13) return std::nullopt;  // Need at least 13 months for YoY

    // Latest value and value 12 months ago (data is sorted oldest first)
    double latest = data.back().value;
    double year_ago = data[data.size() - 13].value;

    if (year_ago == 0.0) return std::nullopt;
    return ((latest - year_ago) / year_ago) * 100.0;
}

std::optional<double> BlsClient::compute_mom_change(const std::vector<BlsDataPoint>& data) {
    if (data.size() < 2) return std::nullopt;

    double latest = data.back().value;
    double prev = data[data.size() - 2].value;

    if (prev == 0.0) return std::nullopt;
    return ((latest - prev) / prev) * 100.0;
}

// ===== FRED Client =====

std::vector<FredObservation> FredClient::fetch_series(const std::string& series_id,
                                                        int limit) {
    spdlog::debug("FRED fetch: {} (limit {})", series_id, limit);
    return {};  // Production: HTTP GET from api.stlouisfed.org
}

std::vector<FredObservation> FredClient::parse_response(const nlohmann::json& j,
                                                           const std::string& series_id) {
    std::vector<FredObservation> observations;

    // FRED response: { "observations": [ { "date": "2026-03-15", "value": "5.33" } ] }
    if (!j.contains("observations") || !j["observations"].is_array()) return observations;

    for (const auto& obs : j["observations"]) {
        FredObservation o;
        o.series_id = series_id;
        o.date = obs.value("date", "");

        std::string val_str = obs.value("value", ".");
        if (val_str == "." || val_str.empty()) continue;  // FRED uses "." for missing

        try {
            o.value = std::stod(val_str);
        } catch (...) {
            continue;
        }

        observations.push_back(o);
    }

    return observations;
}

std::optional<double> FredClient::latest_value(const std::vector<FredObservation>& data) {
    if (data.empty()) return std::nullopt;
    return data.back().value;
}

// ===== EconFeed =====

void EconFeed::refresh() {
    cpi_data_ = bls_.fetch_series("CUUR0000SA0", "2024", "2026");
    nfp_data_ = bls_.fetch_series("CES0000000001", "2024", "2026");
    fed_funds_ = fred_.fetch_series("FEDFUNDS", 10);
    gdpnow_ = fred_.fetch_series("GDPNOW", 5);
    gasoline_ = fred_.fetch_series("GASREGW", 10);
}

std::vector<BlsDataPoint> EconFeed::get_cpi() const { return cpi_data_; }
std::vector<BlsDataPoint> EconFeed::get_nfp() const { return nfp_data_; }
std::vector<FredObservation> EconFeed::get_fed_funds() const { return fed_funds_; }
std::vector<FredObservation> EconFeed::get_gdpnow() const { return gdpnow_; }
std::vector<FredObservation> EconFeed::get_gasoline() const { return gasoline_; }

std::vector<DataSignal> EconFeed::to_signals() const {
    std::vector<DataSignal> signals;

    // CPI YoY
    auto yoy = BlsClient::compute_yoy_change(cpi_data_);
    if (yoy) {
        DataSignal s;
        s.source = "bls";
        s.series_id = "CUUR0000SA0";
        s.label = "CPI YoY %";
        s.value = *yoy;
        if (cpi_data_.size() >= 2) {
            auto prev_yoy = BlsClient::compute_yoy_change(
                std::vector<BlsDataPoint>(cpi_data_.begin(), cpi_data_.end() - 1));
            if (prev_yoy) s.previous_value = *prev_yoy;
        }
        s.fetch_time = std::chrono::system_clock::now();
        signals.push_back(s);
    }

    // Fed funds rate
    auto ff = FredClient::latest_value(fed_funds_);
    if (ff) {
        DataSignal s;
        s.source = "fred";
        s.series_id = "FEDFUNDS";
        s.label = "Fed Funds Rate";
        s.value = *ff;
        if (fed_funds_.size() >= 2) s.previous_value = fed_funds_[fed_funds_.size()-2].value;
        s.fetch_time = std::chrono::system_clock::now();
        signals.push_back(s);
    }

    // Gasoline
    auto gas = FredClient::latest_value(gasoline_);
    if (gas) {
        DataSignal s;
        s.source = "fred";
        s.series_id = "GASREGW";
        s.label = "Regular Gasoline $/gal";
        s.value = *gas;
        if (gasoline_.size() >= 2) s.previous_value = gasoline_[gasoline_.size()-2].value;
        s.fetch_time = std::chrono::system_clock::now();
        signals.push_back(s);
    }

    return signals;
}

} // namespace trader::kalshi
