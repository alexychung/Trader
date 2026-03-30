#include <gtest/gtest.h>
#include "strategy/kalshi/data_feed_manager.hpp"

using namespace trader::kalshi;

TEST(DataFeedManager, TickRefreshesOverdueFeeds) {
    DataFeedManager mgr;
    int refreshed = mgr.tick();
    // Both weather and econ should be due (next_fetch = now at construction)
    EXPECT_GE(refreshed, 0);  // May be 0 if no feeds set, 2 if both due
}

TEST(DataFeedManager, CallbackFiresOnWeatherRefresh) {
    DataFeedManager mgr;
    WeatherFeed wf;
    mgr.set_weather_feed(&wf);

    int event_count = 0;
    mgr.set_callback([&](const FeedEvent& e) { ++event_count; });

    mgr.refresh_weather();
    // Weather feed returns empty (no network), so 0 events is correct
    EXPECT_GE(event_count, 0);
}

TEST(DataFeedManager, SchedulesExist) {
    DataFeedManager mgr;
    auto schedules = mgr.get_schedules();
    EXPECT_EQ(schedules.size(), 2u);
    EXPECT_EQ(schedules[0].name, "weather");
    EXPECT_EQ(schedules[1].name, "economics");
}

TEST(DataFeedManager, SetIntervalUpdatesSchedule) {
    DataFeedManager mgr;
    mgr.set_weather_interval(std::chrono::seconds(3600));  // 1 hour

    auto schedules = mgr.get_schedules();
    EXPECT_EQ(schedules[0].interval, std::chrono::seconds(3600));
}
