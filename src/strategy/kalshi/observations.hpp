#pragma once

// Station observation tracking for the "settled-but-unpriced" weather edge.
//
// Background. Kalshi weather contracts settle on the NWS CF6 daily climate
// summary — station-local midnight-to-midnight max. But trading stays open
// until Kalshi's own cutoff, and CF6 isn't published until the next morning.
// That leaves a several-hour window where the day's max is physically
// determined (sun has set, temps are trending down) but Kalshi still shows
// two-sided markets. Buying a clearly-settled contract at its mispriced
// price is the cleanest edge on the venue.
//
// The detection rule is timezone-free:
//   - Pull the station's recent observations (last ~14 hours) from NWS.
//   - Compute day's max and the timestamp at which it occurred.
//   - If (max_time is at least `min_minutes_since_max` ago) AND
//        (latest temperature is at least `min_cooldown_f` below max) AND
//        (we have at least `min_samples` observations),
//     the max is locked: nothing physically reasonable can beat it.
//
// Compared to a sunset/local-timezone gate this approach is robust to DST
// transitions, polar/equatorial stations, and freak late-night warmups —
// the trajectory itself tells us when we're past the peak.
//
// What this module does NOT do:
//   - No confident prediction during the day's rising phase (defers to
//     ensemble forecasts via WeatherEnsembleModel).
//   - No handling of winter overnight max (the "max so far" is on Jan-2
//     overnight if Jan-1 was warmer). The contract date gating in
//     WeatherEnsembleModel::estimate handles this.

#include "core/types.hpp"
#include "strategy/kalshi/weather_feed.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace trader::kalshi {

struct StationMaxObservation {
    std::string station_id;
    double max_f = 0.0;                    // observed daily max (°F)
    Timestamp max_time_utc{};              // when the max was observed
    double latest_f = 0.0;                 // most recent reading
    Timestamp latest_time_utc{};
    int sample_count = 0;                  // how many obs we saw
    bool locked = false;                   // passes the post-max gate
    std::string reason;                    // human-readable state
};

class StationObservationsClient {
public:
    // Fetch recent temperature observations for a single station and return
    // the computed daily-max state. `hours_back` caps the window read from
    // the NWS API (default 14h — spans past-midnight-UTC for all CONUS).
    //
    // Gate parameters:
    //   min_minutes_since_max:  max must be at least this old
    //   min_cooldown_f:         latest temp must be this far below max
    //   min_samples:            observations required before declaring locked
    //
    // Returns std::nullopt on fetch/parse failure. On success, `locked`
    // reflects whether the max can be considered final for the day.
    static std::optional<StationMaxObservation> fetch_and_compute(
        const WeatherStation& station,
        int hours_back = 14,
        int min_minutes_since_max = 180,
        double min_cooldown_f = 4.0,
        int min_samples = 30);

    // Pure extractor — given a parsed NWS observations JSON, compute the
    // StationMaxObservation. Exposed for testing.
    static std::optional<StationMaxObservation> parse_observations(
        const std::string& station_id,
        const nlohmann::json& j,
        int min_minutes_since_max,
        double min_cooldown_f,
        int min_samples);
};

// Tracks per-station daily-max observations. Thread-safety: single-threaded
// use from the main loop is assumed; add a mutex if that ever changes.
class StationObservationsTracker {
public:
    // Replace the cached observation for this station.
    void set(const StationMaxObservation& obs) {
        obs_[obs.station_id] = obs;
    }
    // Get cached observation (nullptr if none).
    const StationMaxObservation* get(const std::string& station_id) const {
        auto it = obs_.find(station_id);
        return it == obs_.end() ? nullptr : &it->second;
    }
    std::size_t size() const { return obs_.size(); }

private:
    std::unordered_map<std::string, StationMaxObservation> obs_;
};

} // namespace trader::kalshi
