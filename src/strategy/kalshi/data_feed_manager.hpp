#pragma once

#include "core/types.hpp"
#include "core/event_bus.hpp"
#include "strategy/kalshi/weather_feed.hpp"
#include "strategy/kalshi/econ_feed.hpp"
#include <chrono>
#include <functional>
#include <vector>
#include <string>
#include <atomic>

namespace trader::kalshi {

// Variant type for feed events published to the bus
struct FeedEvent {
    enum class Type { WeatherEnsemble, EconSignal };
    Type type;
    EnsembleForecast ensemble;   // Valid when type == WeatherEnsemble
    DataSignal signal;           // Valid when type == EconSignal
};

struct FeedSchedule {
    std::string name;
    std::chrono::seconds interval;
    Timestamp last_fetch;
    Timestamp next_fetch;
    bool enabled = true;
};

class DataFeedManager {
public:
    DataFeedManager();

    // Register feeds
    void set_weather_feed(WeatherFeed* feed) { weather_ = feed; }
    void set_econ_feed(EconFeed* feed) { econ_ = feed; }

    // Event publishing
    using EventCallback = std::function<void(const FeedEvent&)>;
    void set_callback(EventCallback cb) { callback_ = std::move(cb); }

    // Check if any feeds need refreshing and do so
    int tick();

    // Force refresh a specific feed
    void refresh_weather();
    void refresh_econ();

    // Schedule management
    std::vector<FeedSchedule> get_schedules() const { return schedules_; }
    void set_weather_interval(std::chrono::seconds s);
    void set_econ_interval(std::chrono::seconds s);

private:
    void publish_weather_data();
    void publish_econ_data();

    WeatherFeed* weather_ = nullptr;
    EconFeed* econ_ = nullptr;
    EventCallback callback_;
    std::vector<FeedSchedule> schedules_;
};

} // namespace trader::kalshi
