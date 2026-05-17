#include "risk/cluster_limiter.hpp"
#include <sstream>

namespace trader {

std::string ClusterLimiter::cluster_key(const std::string& ticker) {
    // First two dash-separated segments. If only one segment (or empty), return whole ticker.
    auto first = ticker.find('-');
    if (first == std::string::npos) return ticker;
    auto second = ticker.find('-', first + 1);
    if (second == std::string::npos) return ticker;
    return ticker.substr(0, second);
}

bool ClusterLimiter::check(const std::string& ticker, double additional_cost) const {
    if (additional_cost <= 0.0) return true;
    std::string c = cluster_key(ticker);

    std::lock_guard<std::mutex> lock(mutex_);
    double current = 0.0;
    for (const auto& [tkr, info] : positions_) {
        if (info.cluster == c) current += info.cost;
    }
    return (current + additional_cost) <= config_.max_per_cluster;
}

std::string ClusterLimiter::reject_reason(const std::string& ticker, double additional_cost) const {
    if (check(ticker, additional_cost)) return "";
    std::ostringstream oss;
    std::string c = cluster_key(ticker);
    oss << "cluster '" << c << "' exposure $" << cluster_exposure(c)
        << " + $" << additional_cost << " > cap $" << config_.max_per_cluster;
    return oss.str();
}

void ClusterLimiter::on_fill(const std::string& ticker, double cost) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = positions_[ticker];
    info.cluster = cluster_key(ticker);
    info.cost += cost;
}

void ClusterLimiter::on_settlement(const std::string& ticker) {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_.erase(ticker);
}

double ClusterLimiter::cluster_exposure(const std::string& cluster) const {
    std::lock_guard<std::mutex> lock(mutex_);
    double total = 0.0;
    for (const auto& [tkr, info] : positions_) {
        if (info.cluster == cluster) total += info.cost;
    }
    return total;
}

std::unordered_map<std::string, double> ClusterLimiter::all_clusters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, double> out;
    for (const auto& [tkr, info] : positions_) {
        out[info.cluster] += info.cost;
    }
    return out;
}

} // namespace trader
