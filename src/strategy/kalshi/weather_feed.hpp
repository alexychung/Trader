#pragma once

#include "core/types.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace trader::kalshi {

// Station info for Kalshi weather markets
struct WeatherStation {
    std::string id;     // e.g., "KNYC"
    std::string city;   // e.g., "NYC"
    double lat;
    double lon;
    std::string kalshi_ticker_prefix;  // e.g., "KXHIGHNY"
};

// Ensemble forecast data for a single station and date
struct EnsembleForecast {
    std::string station_id;
    std::string model;          // "gfs" or "ecmwf"
    int num_members = 0;
    Timestamp forecast_time;
    std::string target_date;    // "2026-04-26"
    std::vector<double> member_highs;   // High temp per ensemble member (°F)
    std::vector<double> member_lows;    // Low temp per ensemble member (°F)
};

// NWS point forecast for comparison
struct NwsForecast {
    std::string station_id;
    std::string date;           // "2026-04-26"
    double high_f = 0.0;
    double low_f = 0.0;
    std::string short_forecast; // "Sunny, with a high near 75"
    double precip_probability = 0.0;
};

// All known Kalshi weather stations. Legacy tickers use `KXHIGH<code>`; current
// tickers use `KXHIGHT<IATA>` where IATA is a 3-letter airport code that maps
// 1:1 to a K-prefixed ICAO identifier.
inline std::vector<WeatherStation> kalshi_weather_stations() {
    return {
        // Legacy stations (KXHIGH<code> tickers)
        {"KNYC", "NYC",     40.7128,  -74.0060, "KXHIGHNY"},
        {"KORD", "Chicago", 41.9742,  -87.9073, "KXHIGHCHI"},
        {"KMIA", "Miami",   25.7959,  -80.2870, "KXHIGHMIA"},
        {"KLAX", "LA",      33.9425, -118.4081, "KXHIGHLAX"},
        {"KDEN", "Denver",  39.8561, -104.6737, "KXHIGHDEN"},
        {"KAUS", "Austin",  30.1975,  -97.6664, "KXHIGHAUS"},
        // Current KXHIGHT<IATA> stations
        {"KSFO", "SFO",     37.6213, -122.3790, "KXHIGHTSFO"},
        {"KSEA", "Seattle", 47.4502, -122.3088, "KXHIGHTSEA"},
        {"KPHX", "Phoenix", 33.4342, -112.0080, "KXHIGHTPHX"},
        {"KBOS", "Boston",  42.3656,  -71.0096, "KXHIGHTBOS"},
        {"KDCA", "DC",      38.8521,  -77.0377, "KXHIGHTDCA"},
        {"KATL", "Atlanta", 33.6407,  -84.4277, "KXHIGHTATL"},
        {"KDFW", "Dallas",  32.8998,  -97.0403, "KXHIGHTDFW"},
        {"KMSP", "Mpls",    44.8848,  -93.2223, "KXHIGHTMSP"},
        {"KPHL", "Philly",  39.8729,  -75.2437, "KXHIGHTPHL"},
        {"KDTW", "Detroit", 42.2124,  -83.3534, "KXHIGHTDTW"},
        {"KLAS", "Vegas",   36.0840, -115.1537, "KXHIGHTLAS"},
        {"KMCO", "Orlando", 28.4312,  -81.3081, "KXHIGHTMCO"},
    };
}

// Open-Meteo ensemble API client
class OpenMeteoClient {
public:
    // Fetch GFS + ECMWF ensemble forecasts for a station
    // Returns one EnsembleForecast per (model, date) combination
    std::vector<EnsembleForecast> fetch_ensemble(const WeatherStation& station,
                                                   int forecast_days = 7);

    // Parse Open-Meteo JSON response into EnsembleForecast structs
    static std::vector<EnsembleForecast> parse_response(const nlohmann::json& j,
                                                          const std::string& station_id);

    // Extract daily high temperatures from hourly member data
    static std::vector<double> extract_daily_highs(const std::vector<double>& hourly_temps,
                                                     int day_index, int hours_per_day = 24);

    // Backtest: pull archived deterministic forecast for `target_date` from
    // Open-Meteo's historical-forecast archive. Returned as a synthetic
    // EnsembleForecast (model="historical_archive") where members are drawn
    // from N(forecast_high, horizon_sigma(days_ahead_hint)^2) so it can flow
    // through WeatherEnsembleModel unchanged. days_ahead_hint governs spread:
    // larger hint = wider uncertainty.
    // Caches to `cache_dir/<station>_<target>.json` when set.
    static EnsembleForecast fetch_archived_forecast(const WeatherStation& station,
                                                      const std::string& target_date,
                                                      int days_ahead_hint = 2,
                                                      const std::string& cache_dir = "");

    // Parse historical-forecast daily JSON into (high_f, low_f) for a single date.
    // Returns {NaN, NaN} if no data.
    static std::pair<double, double> parse_archive_daily(const nlohmann::json& j,
                                                            const std::string& target_date);

    // Build a synthetic ensemble around `mean_high` using normal quantiles,
    // spread by `sigma`. Produces `num_members` members roughly spanning ±3σ.
    static std::vector<double> synthesize_members(double mean_high, double sigma,
                                                    int num_members = 21);

private:
    std::string build_url(const WeatherStation& station, int forecast_days);
};

// NWS API client
class NwsClient {
public:
    // Fetch 7-day forecast for a station
    std::vector<NwsForecast> fetch_forecast(const WeatherStation& station);

    // Parse NWS forecast JSON
    static std::vector<NwsForecast> parse_forecast(const nlohmann::json& j,
                                                     const std::string& station_id);
};

// Combined weather data feed
class WeatherFeed {
public:
    // Fetch all data for all stations
    void refresh();

    // Get latest ensemble for a station
    std::vector<EnsembleForecast> get_ensemble(const std::string& station_id) const;

    // Get NWS forecast for a station
    std::vector<NwsForecast> get_nws_forecast(const std::string& station_id) const;

    // Cache management
    Timestamp last_refresh() const { return last_refresh_; }

private:
    std::unordered_map<std::string, std::vector<EnsembleForecast>> ensemble_cache_;
    std::unordered_map<std::string, std::vector<NwsForecast>> nws_cache_;
    Timestamp last_refresh_;
};

} // namespace trader::kalshi
