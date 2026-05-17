#include "strategy/kalshi/weather_feed.hpp"
#include "core/http.hpp"
#include "strategy/kalshi/probability_engine.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <filesystem>
#include <fstream>

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
    const std::string url = build_url(station, forecast_days);
    spdlog::debug("OpenMeteo fetch for {} ({}, {}): {}", station.id, station.lat, station.lon, url);

    auto resp = https_get(url);
    spdlog::debug("OpenMeteo HTTP status={} body_size={}",
                  resp.status_code, resp.body.size());
    if (resp.ok() && !resp.body.empty()) {
        // Log the key names present under "hourly" to diagnose parse mismatches.
        try {
            auto j = nlohmann::json::parse(resp.body);
            if (j.contains("hourly") && j["hourly"].is_object()) {
                std::string keys;
                int n = 0;
                for (auto it = j["hourly"].begin(); it != j["hourly"].end() && n < 20; ++it, ++n) {
                    if (!keys.empty()) keys += ",";
                    keys += it.key();
                }
                spdlog::debug("OpenMeteo hourly keys (first 20): {}", keys);
            }
        } catch (...) {}
    }
    if (!resp.ok()) {
        spdlog::warn("OpenMeteo fetch {} failed: HTTP {} body={}",
                     station.id, resp.status_code,
                     resp.body.substr(0, 200));
        return {};
    }

    try {
        auto j = nlohmann::json::parse(resp.body);
        auto parsed = parse_response(j, station.id);
        spdlog::debug("OpenMeteo parse for {}: {} forecast(s)", station.id, parsed.size());
        return parsed;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("OpenMeteo parse failed for {}: {}", station.id, e.what());
        return {};
    }
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

    // Open-Meteo ensemble API 2026 naming:
    //   temperature_2m_memberNN_<model_suffix>
    // Where model_suffix is e.g. "ncep_gefs_seamless" (GFS GEFS, 31 members) or
    //   "ecmwf_ifs025_ensemble" (ECMWF IFS25, 51 members). The deterministic
    // control member lives at temperature_2m_<model_suffix> (no memberNN).
    // Legacy naming (pre-2025) was temperature_2m_memberNN — we accept that too.
    //
    // Strategy: scan every key in `hourly`, match any of:
    //   (a) temperature_2m_memberNN                 — legacy flat
    //   (b) temperature_2m_memberNN_<model_suffix>  — current per-model
    // Group members by model_suffix (empty = legacy/default model).

    std::unordered_map<std::string, std::vector<std::vector<double>>> by_model;

    const std::string var_prefix = "temperature_2m_member";
    for (auto it = hourly.begin(); it != hourly.end(); ++it) {
        const std::string& key = it.key();
        if (key.compare(0, var_prefix.size(), var_prefix) != 0) continue;
        if (!it.value().is_array()) continue;

        // Rest of key after "temperature_2m_member": "01_ncep_gefs_seamless"
        std::string tail = key.substr(var_prefix.size());
        if (tail.size() < 2) continue;
        // Skip leading digits (member number).
        std::size_t p = 0;
        while (p < tail.size() && std::isdigit(static_cast<unsigned char>(tail[p]))) ++p;
        std::string model_suffix;
        if (p < tail.size()) {
            if (tail[p] == '_') {
                model_suffix = tail.substr(p + 1);
            } else {
                continue;  // malformed
            }
        }

        std::vector<double> temps;
        temps.reserve(it.value().size());
        for (const auto& v : it.value()) {
            if (v.is_number()) temps.push_back(v.get<double>());
            else temps.push_back(std::numeric_limits<double>::quiet_NaN());
        }
        by_model[model_suffix].push_back(std::move(temps));
    }

    if (by_model.empty()) return forecasts;

    const int total_hours = static_cast<int>(times.size());
    const int num_days = total_hours / 24;

    auto suffix_to_short_name = [](const std::string& s) -> std::string {
        // Compact names that match the rest of the system's "gfs" / "ecmwf" labels.
        if (s.find("gefs") != std::string::npos || s.find("gfs") != std::string::npos) return "gfs";
        if (s.find("ecmwf") != std::string::npos || s.find("ifs") != std::string::npos) return "ecmwf";
        if (s.empty()) return "gfs";  // legacy default
        return s;
    };

    for (const auto& [suffix, members] : by_model) {
        for (int day = 0; day < num_days; ++day) {
            EnsembleForecast ef;
            ef.station_id = station_id;
            ef.model = suffix_to_short_name(suffix);

            int hour_idx = day * 24;
            if (hour_idx < static_cast<int>(times.size()) && times[hour_idx].is_string()) {
                ef.target_date = times[hour_idx].get<std::string>().substr(0, 10);
            }

            for (const auto& member_hourly : members) {
                auto day_temps = extract_daily_highs(member_hourly, day);
                if (day_temps.empty()) continue;
                double high = *std::max_element(day_temps.begin(), day_temps.end());
                if (!std::isnan(high)) ef.member_highs.push_back(high);
            }

            ef.num_members = static_cast<int>(ef.member_highs.size());
            if (ef.num_members > 0) {
                forecasts.push_back(std::move(ef));
            }
        }
    }

    return forecasts;
}

std::pair<double, double> OpenMeteoClient::parse_archive_daily(
    const nlohmann::json& j, const std::string& target_date) {
    double high = std::numeric_limits<double>::quiet_NaN();
    double low = std::numeric_limits<double>::quiet_NaN();
    if (!j.contains("daily")) return {high, low};
    const auto& daily = j["daily"];
    if (!daily.contains("time") || !daily["time"].is_array()) return {high, low};

    int idx = -1;
    for (std::size_t i = 0; i < daily["time"].size(); ++i) {
        if (daily["time"][i].is_string() && daily["time"][i].get<std::string>() == target_date) {
            idx = static_cast<int>(i);
            break;
        }
    }
    if (idx < 0) return {high, low};

    auto read_at = [&](const std::string& key) -> double {
        if (!daily.contains(key) || !daily[key].is_array() || idx >= static_cast<int>(daily[key].size())) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const auto& v = daily[key][idx];
        if (v.is_number()) return v.get<double>();
        return std::numeric_limits<double>::quiet_NaN();
    };
    high = read_at("temperature_2m_max");
    low = read_at("temperature_2m_min");
    return {high, low};
}

std::vector<double> OpenMeteoClient::synthesize_members(double mean_high, double sigma, int num_members) {
    std::vector<double> members;
    if (num_members < 2) num_members = 2;
    // Evenly-spaced quantiles in (0,1) with a normal inverse-CDF approximation.
    // Using Beasley-Springer-Moro would be more accurate, but rational-approx is fine for ensemble synthesis.
    auto norm_inv = [](double p) -> double {
        // Acklam's rational approximation for inverse normal CDF.
        static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                                    -2.759285104469687e+02, 1.383577518672690e+02,
                                    -3.066479806614716e+01, 2.506628277459239e+00};
        static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                                    -1.556989798598866e+02, 6.680131188771972e+01,
                                    -1.328068155288572e+01};
        static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                                    -2.400758277161838e+00, -2.549732539343734e+00,
                                    4.374664141464968e+00, 2.938163982698783e+00};
        static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                                    2.445134137142996e+00, 3.754408661907416e+00};
        double plow = 0.02425, phigh = 1.0 - plow;
        double q, r;
        if (p < plow) {
            q = std::sqrt(-2.0 * std::log(p));
            return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                   ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
        } else if (p <= phigh) {
            q = p - 0.5;
            r = q*q;
            return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
                   (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
        } else {
            q = std::sqrt(-2.0 * std::log(1.0 - p));
            return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                    ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
        }
    };
    members.reserve(num_members);
    for (int i = 0; i < num_members; ++i) {
        double p = (i + 0.5) / num_members;
        double z = norm_inv(p);
        members.push_back(mean_high + sigma * z);
    }
    return members;
}

EnsembleForecast OpenMeteoClient::fetch_archived_forecast(
    const WeatherStation& station,
    const std::string& target_date,
    int days_ahead_hint,
    const std::string& cache_dir) {

    EnsembleForecast ef;
    ef.station_id = station.id;
    ef.model = "historical_archive";
    ef.target_date = target_date;
    ef.forecast_time = std::chrono::system_clock::now();

    // On-disk cache short-circuit.
    std::string cache_path;
    if (!cache_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(cache_dir, ec);
        cache_path = cache_dir + "/" + station.id + "_" + target_date + ".json";
        std::ifstream cached(cache_path);
        if (cached.good()) {
            try {
                nlohmann::json j;
                cached >> j;
                double high = j.value("high_f", std::numeric_limits<double>::quiet_NaN());
                if (std::isfinite(high)) {
                    double sigma = WeatherEnsembleModel::horizon_sigma(std::max(1, days_ahead_hint));
                    ef.member_highs = synthesize_members(high, sigma);
                    ef.num_members = static_cast<int>(ef.member_highs.size());
                    return ef;
                }
            } catch (...) {
                // Fall through to refetch.
            }
        }
    }

    std::ostringstream url;
    url << "https://historical-forecast-api.open-meteo.com/v1/forecast"
        << "?latitude=" << std::fixed << std::setprecision(4) << station.lat
        << "&longitude=" << std::setprecision(4) << station.lon
        << "&start_date=" << target_date
        << "&end_date=" << target_date
        << "&daily=temperature_2m_max,temperature_2m_min"
        << "&temperature_unit=fahrenheit"
        << "&timezone=UTC";

    auto resp = https_get(url.str());
    if (!resp.ok()) {
        spdlog::debug("archive fetch {} {} failed: HTTP {}",
                      station.id, target_date, resp.status_code);
        return ef;
    }

    try {
        auto j = nlohmann::json::parse(resp.body);
        auto [high, low] = parse_archive_daily(j, target_date);
        if (!std::isfinite(high)) return ef;

        if (!cache_path.empty()) {
            std::ofstream out(cache_path);
            if (out.good()) {
                nlohmann::json cached;
                cached["station_id"] = station.id;
                cached["target_date"] = target_date;
                cached["high_f"] = high;
                cached["low_f"] = low;
                out << cached.dump();
            }
        }

        double sigma = WeatherEnsembleModel::horizon_sigma(std::max(1, days_ahead_hint));
        ef.member_highs = synthesize_members(high, sigma);
        ef.num_members = static_cast<int>(ef.member_highs.size());
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("archive parse {} {}: {}", station.id, target_date, e.what());
    }
    return ef;
}

// ===== NwsClient =====

std::vector<NwsForecast> NwsClient::fetch_forecast(const WeatherStation& station) {
    // NWS API is two-stage: resolve lat/lon → gridpoint → forecast URL.
    // Step 1: GET /points/{lat},{lon} returns properties.forecast URL.
    std::ostringstream points_url;
    points_url << "https://api.weather.gov/points/"
               << std::fixed << std::setprecision(4) << station.lat
               << "," << std::setprecision(4) << station.lon;

    spdlog::debug("NWS points lookup for {} ({}, {})",
                  station.id, station.lat, station.lon);
    auto points_resp = https_get(points_url.str());
    if (!points_resp.ok()) {
        spdlog::warn("NWS points {} failed: HTTP {}", station.id, points_resp.status_code);
        return {};
    }

    std::string forecast_url;
    try {
        auto jp = nlohmann::json::parse(points_resp.body);
        if (jp.contains("properties") && jp["properties"].contains("forecast")) {
            forecast_url = jp["properties"]["forecast"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("NWS points parse for {}: {}", station.id, e.what());
        return {};
    }
    if (forecast_url.empty()) {
        spdlog::warn("NWS points {} returned no forecast URL", station.id);
        return {};
    }

    // Step 2: GET the resolved forecast URL.
    auto fc_resp = https_get(forecast_url);
    if (!fc_resp.ok()) {
        spdlog::warn("NWS forecast {} failed: HTTP {}", station.id, fc_resp.status_code);
        return {};
    }
    try {
        auto j = nlohmann::json::parse(fc_resp.body);
        return parse_forecast(j, station.id);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("NWS forecast parse for {}: {}", station.id, e.what());
        return {};
    }
}

std::vector<NwsForecast> NwsClient::parse_forecast(
    const nlohmann::json& j, const std::string& station_id) {
    std::vector<NwsForecast> forecasts;

    if (!j.contains("properties") || !j["properties"].contains("periods")) {
        return forecasts;
    }

    // nlohmann's json::value() throws type_error.302 if the field exists but
    // is null. NWS periodically returns `temperature: null` or
    // `shortForecast: null` for future periods where the forecaster hasn't
    // filled in values yet (observed e.g. on KMIA). Null-safe readers below.
    auto opt_double = [](const nlohmann::json& obj, const char* key,
                         double dflt) -> double {
        auto it = obj.find(key);
        if (it == obj.end() || it->is_null()) return dflt;
        if (it->is_number()) return it->get<double>();
        return dflt;
    };
    auto opt_string = [](const nlohmann::json& obj, const char* key,
                         const std::string& dflt) -> std::string {
        auto it = obj.find(key);
        if (it == obj.end() || it->is_null()) return dflt;
        if (it->is_string()) return it->get<std::string>();
        return dflt;
    };
    auto opt_bool = [](const nlohmann::json& obj, const char* key,
                       bool dflt) -> bool {
        auto it = obj.find(key);
        if (it == obj.end() || it->is_null()) return dflt;
        if (it->is_boolean()) return it->get<bool>();
        return dflt;
    };

    const auto& periods = j["properties"]["periods"];
    NwsForecast current;
    current.station_id = station_id;

    for (const auto& period : periods) {
        bool is_daytime = opt_bool(period, "isDaytime", true);
        double temp = opt_double(period, "temperature", 0.0);

        if (is_daytime) {
            current = NwsForecast{};
            current.station_id = station_id;
            current.high_f = temp;
            current.short_forecast = opt_string(period, "shortForecast", "");

            // Extract date from startTime.
            std::string start = opt_string(period, "startTime", "");
            if (start.size() >= 10) {
                current.date = start.substr(0, 10);
            }

            // probabilityOfPrecipitation has shape {"value": null | number}.
            if (period.contains("probabilityOfPrecipitation") &&
                period["probabilityOfPrecipitation"].is_object()) {
                auto pop_it = period["probabilityOfPrecipitation"].find("value");
                if (pop_it != period["probabilityOfPrecipitation"].end() &&
                    pop_it->is_number()) {
                    current.precip_probability = pop_it->get<double>() / 100.0;
                }
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
