#include "strategy/kalshi/observations.hpp"
#include "core/http.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace trader::kalshi {

namespace {

// Parse an ISO-8601 timestamp like "2026-04-21T14:51:00+00:00" or
// "2026-04-21T14:51:00Z" into a system_clock::time_point.
std::optional<Timestamp> parse_iso8601(const std::string& s) {
    if (s.size() < 19) return std::nullopt;
    std::tm tm{};
    int y, mo, d, h, mi, se;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) {
        return std::nullopt;
    }
    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = se;
    tm.tm_isdst = 0;
    // Interpret as UTC regardless of the trailing offset — NWS always
    // publishes in UTC with either "Z" or "+00:00".
#ifdef _WIN32
    std::time_t tt = _mkgmtime(&tm);
#else
    std::time_t tt = timegm(&tm);
#endif
    if (tt < 0) return std::nullopt;
    return Timestamp(std::chrono::seconds(tt));
}

double celsius_to_f(double c) { return c * 9.0 / 5.0 + 32.0; }

// Render a Timestamp as "YYYY-MM-DDTHH:MM:SSZ" for NWS query params.
std::string to_iso_z(Timestamp ts) {
    auto tt = std::chrono::system_clock::to_time_t(ts);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

} // namespace

std::optional<StationMaxObservation> StationObservationsClient::parse_observations(
    const std::string& station_id,
    const nlohmann::json& j,
    int min_minutes_since_max,
    double min_cooldown_f,
    int min_samples) {

    if (!j.contains("features") || !j["features"].is_array() || j["features"].empty()) {
        return std::nullopt;
    }

    struct Sample { Timestamp t; double f; };
    std::vector<Sample> samples;
    samples.reserve(j["features"].size());

    for (const auto& feat : j["features"]) {
        if (!feat.contains("properties")) continue;
        const auto& props = feat["properties"];
        auto ts_str = props.value("timestamp", std::string{});
        auto ts = parse_iso8601(ts_str);
        if (!ts) continue;
        if (!props.contains("temperature") || !props["temperature"].is_object()) continue;
        const auto& temp = props["temperature"];
        if (temp.value("value", nlohmann::json{}).is_null()) continue;
        if (!temp["value"].is_number()) continue;
        double c = temp["value"].get<double>();
        // NWS unit codes: "wmoUnit:degC" (typical) or "wmoUnit:degF" (rare).
        auto unit = temp.value("unitCode", std::string{"wmoUnit:degC"});
        double f = (unit.find("degF") != std::string::npos) ? c : celsius_to_f(c);
        samples.push_back({*ts, f});
    }

    if (static_cast<int>(samples.size()) < min_samples) return std::nullopt;

    // Sort by time ascending so "latest" is last and max-age computations are
    // straightforward. NWS returns newest-first typically; don't assume.
    std::sort(samples.begin(), samples.end(),
              [](const Sample& a, const Sample& b) { return a.t < b.t; });

    StationMaxObservation obs;
    obs.station_id = station_id;
    obs.sample_count = static_cast<int>(samples.size());

    // Find max and its time.
    auto max_it = std::max_element(
        samples.begin(), samples.end(),
        [](const Sample& a, const Sample& b) { return a.f < b.f; });
    obs.max_f = max_it->f;
    obs.max_time_utc = max_it->t;

    obs.latest_f = samples.back().f;
    obs.latest_time_utc = samples.back().t;

    // Lock gate: max must be sufficiently aged and current temp must be
    // sufficiently below max.
    auto age_min = std::chrono::duration_cast<std::chrono::minutes>(
        obs.latest_time_utc - obs.max_time_utc).count();
    double cooldown = obs.max_f - obs.latest_f;

    if (age_min < min_minutes_since_max) {
        std::ostringstream oss;
        oss << "max " << obs.max_f << "F was " << age_min
            << "min ago (<" << min_minutes_since_max << "); not locked";
        obs.reason = oss.str();
        obs.locked = false;
    } else if (cooldown < min_cooldown_f) {
        std::ostringstream oss;
        oss << "current " << obs.latest_f << "F only " << cooldown
            << "F below max " << obs.max_f << "F (<" << min_cooldown_f
            << "); not locked";
        obs.reason = oss.str();
        obs.locked = false;
    } else {
        std::ostringstream oss;
        oss << "max " << obs.max_f << "F " << age_min
            << "min ago, currently " << obs.latest_f
            << "F (-" << cooldown << "F) — locked";
        obs.reason = oss.str();
        obs.locked = true;
    }

    return obs;
}

std::optional<StationMaxObservation> StationObservationsClient::fetch_and_compute(
    const WeatherStation& station,
    int hours_back,
    int min_minutes_since_max,
    double min_cooldown_f,
    int min_samples) {

    auto now = std::chrono::system_clock::now();
    auto start = now - std::chrono::hours(hours_back);

    // NWS filters observations by `start` and `end` query params. Some NWS
    // instances also support `limit`; we leave it off and rely on the window.
    std::ostringstream url;
    url << "https://api.weather.gov/stations/" << station.id
        << "/observations?start=" << to_iso_z(start)
        << "&end=" << to_iso_z(now);

    spdlog::debug("NWS obs fetch {}: {}", station.id, url.str());
    auto resp = https_get(url.str());
    if (!resp.ok()) {
        spdlog::warn("NWS obs {} failed: HTTP {}", station.id, resp.status_code);
        return std::nullopt;
    }

    try {
        auto j = nlohmann::json::parse(resp.body);
        auto out = parse_observations(station.id, j, min_minutes_since_max,
                                       min_cooldown_f, min_samples);
        if (out) {
            spdlog::debug("NWS obs {}: {} samples, {}",
                          station.id, out->sample_count, out->reason);
        }
        return out;
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("NWS obs parse for {}: {}", station.id, e.what());
        return std::nullopt;
    }
}

} // namespace trader::kalshi
