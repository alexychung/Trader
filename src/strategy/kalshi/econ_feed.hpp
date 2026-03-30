#pragma once

#include "core/types.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace trader::kalshi {

// BLS time series data point
struct BlsDataPoint {
    std::string series_id;
    std::string year;
    std::string period;     // "M01" through "M12" for monthly
    double value = 0.0;
    std::string footnotes;
};

// FRED observation
struct FredObservation {
    std::string series_id;
    std::string date;       // "2026-03-15"
    double value = 0.0;
};

// BLS API client
class BlsClient {
public:
    explicit BlsClient(const std::string& api_key = "") : api_key_(api_key) {}

    // Fetch a time series (e.g., CUUR0000SA0 for CPI-U)
    std::vector<BlsDataPoint> fetch_series(const std::string& series_id,
                                            const std::string& start_year,
                                            const std::string& end_year);

    // Parse BLS API v2 JSON response
    static std::vector<BlsDataPoint> parse_response(const nlohmann::json& j,
                                                      const std::string& series_id);

    // Compute year-over-year change from data points
    static std::optional<double> compute_yoy_change(const std::vector<BlsDataPoint>& data);

    // Compute month-over-month change
    static std::optional<double> compute_mom_change(const std::vector<BlsDataPoint>& data);

private:
    std::string api_key_;
};

// FRED API client
class FredClient {
public:
    explicit FredClient(const std::string& api_key = "") : api_key_(api_key) {}

    // Fetch observations for a series
    std::vector<FredObservation> fetch_series(const std::string& series_id,
                                               int limit = 10);

    // Parse FRED API JSON response
    static std::vector<FredObservation> parse_response(const nlohmann::json& j,
                                                         const std::string& series_id);

    // Get latest value for a series
    static std::optional<double> latest_value(const std::vector<FredObservation>& data);

private:
    std::string api_key_;
};

// Combined economic data feed
class EconFeed {
public:
    EconFeed(const std::string& bls_key = "", const std::string& fred_key = "")
        : bls_(bls_key), fred_(fred_key) {}

    // Refresh all economic data
    void refresh();

    // Get latest CPI data
    std::vector<BlsDataPoint> get_cpi() const;
    std::vector<BlsDataPoint> get_nfp() const;

    // Get FRED data
    std::vector<FredObservation> get_fed_funds() const;
    std::vector<FredObservation> get_gdpnow() const;
    std::vector<FredObservation> get_gasoline() const;

    // Convert to DataSignals for the event bus
    std::vector<DataSignal> to_signals() const;

private:
    BlsClient bls_;
    FredClient fred_;

    std::vector<BlsDataPoint> cpi_data_;
    std::vector<BlsDataPoint> nfp_data_;
    std::vector<FredObservation> fed_funds_;
    std::vector<FredObservation> gdpnow_;
    std::vector<FredObservation> gasoline_;
};

} // namespace trader::kalshi
