#include "strategy/kalshi/data_feed_manager.hpp"
#include <spdlog/spdlog.h>

namespace trader::kalshi {

DataFeedManager::DataFeedManager() {
    auto now = std::chrono::system_clock::now();
    schedules_ = {
        {"weather", std::chrono::seconds(6 * 3600), Timestamp{}, now, true},   // 6 hours
        {"economics", std::chrono::seconds(24 * 3600), Timestamp{}, now, true}, // 24 hours
    };
}

void DataFeedManager::set_weather_interval(std::chrono::seconds s) {
    for (auto& sched : schedules_) {
        if (sched.name == "weather") sched.interval = s;
    }
}

void DataFeedManager::set_econ_interval(std::chrono::seconds s) {
    for (auto& sched : schedules_) {
        if (sched.name == "economics") sched.interval = s;
    }
}

int DataFeedManager::tick() {
    auto now = std::chrono::system_clock::now();
    int refreshed = 0;

    for (auto& sched : schedules_) {
        if (!sched.enabled) continue;
        if (now >= sched.next_fetch) {
            spdlog::info("DataFeedManager: refreshing {}", sched.name);
            if (sched.name == "weather") {
                refresh_weather();
            } else if (sched.name == "economics") {
                refresh_econ();
            }
            sched.last_fetch = now;
            sched.next_fetch = now + sched.interval;
            ++refreshed;
        }
    }
    return refreshed;
}

void DataFeedManager::refresh_weather() {
    if (!weather_) return;
    weather_->refresh();
    publish_weather_data();
}

void DataFeedManager::refresh_econ() {
    if (!econ_) return;
    econ_->refresh();
    publish_econ_data();
}

void DataFeedManager::publish_weather_data() {
    if (!weather_ || !callback_) return;

    for (const auto& station : kalshi_weather_stations()) {
        auto ensembles = weather_->get_ensemble(station.id);
        for (const auto& ef : ensembles) {
            FeedEvent event;
            event.type = FeedEvent::Type::WeatherEnsemble;
            event.ensemble = ef;
            callback_(event);
        }
    }
}

void DataFeedManager::publish_econ_data() {
    if (!econ_ || !callback_) return;

    auto signals = econ_->to_signals();
    for (const auto& sig : signals) {
        FeedEvent event;
        event.type = FeedEvent::Type::EconSignal;
        event.signal = sig;
        callback_(event);
    }
}

} // namespace trader::kalshi
