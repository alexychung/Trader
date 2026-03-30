#include "strategy/kalshi/weather_feed.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

// Reuse the HTTP GET from rest_client for now
// In production, this would use a shared HTTP client
#include "exchange/kalshi/rest_client.hpp"

namespace trader::kalshi {

// ===== OpenMeteoClient =====

std::string OpenMeteoClient::build_url(const WeatherStation& station, int forecast_days) {
    std::ostringstream url;
    url << "https://ensemble-api.open-meteo.com/v1/ensemble"
        << "?latitude=" << std::fixed << std::setprecision(4) << station.lat
        << "&longitude=" << std::setprecision(4) << station.lon
        << "&models=gfs_seamless,ecmwf_ifs025"
        << "&hourly=temperature_2m"
        << "&forecast_days=" << forecast_days
        << "&temperature_unit=fahrenheit";
    return url.str();
}

std::vector<EnsembleForecast> OpenMeteoClient::fetch_ensemble(
    const WeatherStation& station, int forecast_days) {
    // Network fetch would go here — using Boost.Beast HTTPS GET
    // For unit testing, we test parse_response directly with sample data
    spdlog::debug("OpenMeteo fetch for {} ({}, {})", station.id, station.lat, station.lon);
    return {};  // Production implementation would call HTTP and parse
}

std::vector<double> OpenMeteoClient::extract_daily_highs(
    const std::vector<double>& hourly_temps, int day_index, int hours_per_day) {
    int start = day_index * hours_per_day;
    int end = std::min(start + hours_per_day, static_cast<int>(hourly_temps.size()));

    if (start >= static_cast<int>(hourly_temps.size())) return {};

    std::vector<double> day_temps(hourly_temps.begin() + start, hourly_temps.begin() + end);
    return day_temps;
}

std::vector<EnsembleForecast> OpenMeteoClient::parse_response(
    const nlohmann::json& j, const std::string& station_id) {
    std::vector<EnsembleForecast> forecasts;

    if (!j.contains("hourly")) return forecasts;
    const auto& hourly = j["hourly"];

    if (!hourly.contains("time") || !hourly["time"].is_array()) return forecasts;
    const auto& times = hourly["time"];

    // Collect all member keys for each model
    // GFS: temperature_2m_member01 ... temperature_2m_member31
    // ECMWF: temperature_2m_member01 ... temperature_2m_member51

    auto collect_members = [&](const std::string& prefix, int max_members) -> std::vector<std::vector<double>> {
        std::vector<std::vector<double>> members;
        for (int m = 0; m <= max_members; ++m) {
            std::ostringstream key;
            key << prefix << std::setfill('0') << std::setw(2) << m;
            std::string k = key.str();
            if (hourly.contains(k) && hourly[k].is_array()) {
                std::vector<double> temps;
                for (const auto& v : hourly[k]) {
                    if (v.is_number()) temps.push_back(v.get<double>());
                    else temps.push_back(std::numeric_limits<double>::quiet_NaN());
                }
                members.push_back(std::move(temps));
            }
        }
        return members;
    };

    // Determine number of days from time array
    int total_hours = static_cast<int>(times.size());
    int num_days = total_hours / 24;

    // Try GFS members (typically member00 through member30 = 31 members)
    auto gfs_members = collect_members("temperature_2m_member", 50);

    if (!gfs_members.empty()) {
        // For each day, extract daily highs per member
        for (int day = 0; day < num_days; ++day) {
            EnsembleForecast ef;
            ef.station_id = station_id;
            ef.model = "gfs";
            ef.num_members = static_cast<int>(gfs_members.size());

            // Extract date from time string (first hour of the day)
            int hour_idx = day * 24;
            if (hour_idx < static_cast<int>(times.size())) {
                std::string datetime = times[hour_idx].get<std::string>();
                ef.target_date = datetime.substr(0, 10);  // "2026-04-26"
            }

            // Get daily high for each member
            for (const auto& member_hourly : gfs_members) {
                auto day_temps = extract_daily_highs(member_hourly, day);
                if (!day_temps.empty()) {
                    double high = *std::max_element(day_temps.begin(), day_temps.end());
                    if (!std::isnan(high)) {
                        ef.member_highs.push_back(high);
                    }
                }
            }

            ef.num_members = static_cast<int>(ef.member_highs.size());
            if (ef.num_members > 0) {
                forecasts.push_back(std::move(ef));
            }
        }
    }

    return forecasts;
}

// ===== NwsClient =====

std::vector<NwsForecast> NwsClient::fetch_forecast(const WeatherStation& station) {
    spdlog::debug("NWS fetch for {} ({}, {})", station.id, station.lat, station.lon);
    return {};  // Production implementation would call api.weather.gov
}

std::vector<NwsForecast> NwsClient::parse_forecast(
    const nlohmann::json& j, const std::string& station_id) {
    std::vector<NwsForecast> forecasts;

    if (!j.contains("properties") || !j["properties"].contains("periods")) {
        return forecasts;
    }

    const auto& periods = j["properties"]["periods"];
    NwsForecast current;
    current.station_id = station_id;

    for (const auto& period : periods) {
        bool is_daytime = period.value("isDaytime", true);
        double temp = period.value("temperature", 0.0);
        std::string name = period.value("name", "");

        if (is_daytime) {
            current = NwsForecast{};
            current.station_id = station_id;
            current.high_f = temp;
            current.short_forecast = period.value("shortForecast", "");

            // Extract date from startTime
            std::string start = period.value("startTime", "");
            if (start.size() >= 10) {
                current.date = start.substr(0, 10);
            }

            // Probability of precipitation
            if (period.contains("probabilityOfPrecipitation") &&
                period["probabilityOfPrecipitation"].contains("value") &&
                !period["probabilityOfPrecipitation"]["value"].is_null()) {
                current.precip_probability = period["probabilityOfPrecipitation"]["value"].get<double>() / 100.0;
            }
        } else {
            current.low_f = temp;
            if (!current.date.empty()) {
                forecasts.push_back(current);
            }
        }
    }

    return forecasts;
}

// ===== WeatherFeed =====

void WeatherFeed::refresh() {
    OpenMeteoClient meteo;
    NwsClient nws;

    for (const auto& station : kalshi_weather_stations()) {
        auto ensemble = meteo.fetch_ensemble(station);
        if (!ensemble.empty()) {
            ensemble_cache_[station.id] = std::move(ensemble);
        }

        auto forecast = nws.fetch_forecast(station);
        if (!forecast.empty()) {
            nws_cache_[station.id] = std::move(forecast);
        }
    }

    last_refresh_ = std::chrono::system_clock::now();
}

std::vector<EnsembleForecast> WeatherFeed::get_ensemble(const std::string& station_id) const {
    auto it = ensemble_cache_.find(station_id);
    if (it != ensemble_cache_.end()) return it->second;
    return {};
}

std::vector<NwsForecast> WeatherFeed::get_nws_forecast(const std::string& station_id) const {
    auto it = nws_cache_.find(station_id);
    if (it != nws_cache_.end()) return it->second;
    return {};
}

} // namespace trader::kalshi
