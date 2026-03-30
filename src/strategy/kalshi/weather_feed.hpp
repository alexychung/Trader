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

// All known Kalshi weather stations
inline std::vector<WeatherStation> kalshi_weather_stations() {
    return {
        {"KNYC", "NYC",     40.7128, -74.0060, "KXHIGHNY"},
        {"KORD", "Chicago", 41.9742, -87.9073, "KXHIGHCHI"},
        {"KMIA", "Miami",   25.7959, -80.2870, "KXHIGHMIA"},
        {"KLAX", "LA",      33.9425, -118.4081, "KXHIGHLAX"},
        {"KDEN", "Denver",  39.8561, -104.6737, "KXHIGHDEN"},
        {"KAUS", "Austin",  30.1975, -97.6664, "KXHIGHAUS"},
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
