#include "core/staleness_gate.hpp"
#include <sstream>

namespace trader {

void StalenessGate::on_market_update(const std::string& ticker) {
    std::lock_guard<std::mutex> lock(mutex_);
    market_last_[ticker] = std::chrono::system_clock::now();
}

void StalenessGate::on_feed_update(const std::string& feed) {
    std::lock_guard<std::mutex> lock(mutex_);
    feed_last_[feed] = std::chrono::system_clock::now();
}

void StalenessGate::seed_market(const std::string& ticker, Timestamp ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    market_last_[ticker] = ts;
}

void StalenessGate::seed_feed(const std::string& feed, Timestamp ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    feed_last_[feed] = ts;
}

std::chrono::seconds StalenessGate::feed_max_age(const std::string& feed) const {
    auto it = config_.feed_overrides.find(feed);
    if (it != config_.feed_overrides.end()) return it->second;
    return config_.default_feed_max_age;
}

bool StalenessGate::is_market_fresh(const std::string& ticker) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = market_last_.find(ticker);
    if (it == market_last_.end()) return false;
    auto age = std::chrono::system_clock::now() - it->second;
    return age <= config_.market_max_age;
}

bool StalenessGate::is_feed_fresh(const std::string& feed) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = feed_last_.find(feed);
    if (it == feed_last_.end()) return false;
    auto age = std::chrono::system_clock::now() - it->second;
    return age <= feed_max_age(feed);
}

std::string StalenessGate::stale_reason_market(const std::string& ticker) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = market_last_.find(ticker);
    if (it == market_last_.end()) return "no market update received yet";
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - it->second);
    if (age <= config_.market_max_age) return "";
    std::ostringstream oss;
    oss << "market " << ticker << " stale: last update " << age.count() << "s ago (limit "
        << config_.market_max_age.count() << "s)";
    return oss.str();
}

std::string StalenessGate::stale_reason_feed(const std::string& feed) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = feed_last_.find(feed);
    auto max = feed_max_age(feed);
    if (it == feed_last_.end()) return "feed " + feed + ": no data received yet";
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - it->second);
    if (age <= max) return "";
    std::ostringstream oss;
    oss << "feed " << feed << " stale: last update " << age.count() << "s ago (limit "
        << max.count() << "s)";
    return oss.str();
}

std::unordered_map<std::string, Timestamp> StalenessGate::last_market_updates() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return market_last_;
}

std::unordered_map<std::string, Timestamp> StalenessGate::last_feed_updates() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return feed_last_;
}

} // namespace trader
