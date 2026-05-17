#pragma once

#include "core/types.hpp"
#include <string>
#include <unordered_map>
#include <mutex>

namespace trader {

// Per-cluster exposure cap. A "cluster" is a group of correlated markets that
// resolve on the same underlying event — e.g., all weather thresholds for KNYC
// on 26-Apr-2026. Without this cap, a single bad weather day could blow out
// 5+ supposedly-independent positions simultaneously.
//
// Cluster key is derived from the first two dash-separated ticker segments,
// which on Kalshi reliably identify the underlying event:
//
//   KXHIGHNY-26APR26-T75   →   "KXHIGHNY-26APR26"   (NYC high, 2026-04-26)
//   KXCPIYY-26MAY-T3.0     →   "KXCPIYY-26MAY"      (CPI YoY, May 2026)
//   KXFED-26JUN-T5.25      →   "KXFED-26JUN"        (FOMC, June 2026)
//
// All thresholds for the same event share a cluster key.
class ClusterLimiter {
public:
    struct Config {
        double max_per_cluster = 30.0;  // dollars per correlated event
    };

    explicit ClusterLimiter(Config config = {}) : config_(config) {}

    // Derive cluster key from ticker. Public so callers (strategy, tests) can inspect.
    static std::string cluster_key(const std::string& ticker);

    // Check whether `additional_cost` would breach the cluster cap.
    bool check(const std::string& ticker, double additional_cost) const;

    // Reason string when check fails (empty if OK).
    std::string reject_reason(const std::string& ticker, double additional_cost) const;

    // Record a fill (increases cluster exposure for this ticker).
    void on_fill(const std::string& ticker, double cost);

    // Record a settlement (frees the ticker's cost from its cluster).
    void on_settlement(const std::string& ticker);

    // Current exposure for a specific cluster.
    double cluster_exposure(const std::string& cluster) const;

    // Snapshot of all clusters and their exposures (diagnostics).
    std::unordered_map<std::string, double> all_clusters() const;

    const Config& config() const { return config_; }

private:
    Config config_;
    mutable std::mutex mutex_;
    struct PositionInfo {
        std::string cluster;
        double cost = 0.0;
    };
    std::unordered_map<std::string, PositionInfo> positions_;  // ticker -> info
};

} // namespace trader
