#pragma once

// WebSocket reconnect scaffolding — pure, testable pieces of the reliability
// story that are independent of the Boost.Beast I/O loop.
//
// Why split: Kalshi's WS guidance (see docs/RESEARCH_DATA_ALGORITHMS.md §2.4)
// calls for exponential backoff with jitter, max 30s cap, resubscribe-and-
// fresh-snapshot on gap, and no incremental resync. Every piece of that is
// testable as a pure function; only the byte-pushing part needs a live
// connection. Keep this header I/O-free so unit tests cover the policy.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

namespace trader::kalshi {

// Exponential backoff with jitter.
//   attempt = 0 produces the base delay (e.g. 1s)
//   doubles each attempt until capped
//   jitter_pct of ±N% is applied deterministically if rand_01 provided,
//   or via std::rand() otherwise. Deterministic rand (unit tests) lets us
//   pin both the growth curve and the jitter envelope.
struct BackoffConfig {
    std::chrono::milliseconds base{1000};    // first retry delay
    std::chrono::milliseconds max{30000};    // cap
    double jitter_pct = 0.25;                // ±25%
};

// Pure: compute the n-th delay without jitter. attempt is zero-based.
inline std::chrono::milliseconds backoff_delay_no_jitter(int attempt,
                                                          const BackoffConfig& cfg = {}) {
    if (attempt < 0) attempt = 0;
    // 1s, 2s, 4s, 8s, ... capped. Use int64 to avoid overflow for large attempt.
    int64_t multiplier = (attempt >= 31) ? (int64_t{1} << 31) : (int64_t{1} << attempt);
    int64_t ms = static_cast<int64_t>(cfg.base.count()) * multiplier;
    ms = std::min<int64_t>(ms, cfg.max.count());
    return std::chrono::milliseconds(ms);
}

// Apply a ±jitter_pct band to a base delay. rand_01 in [0,1].
// Result is always > 0 (jitter can drop it to (1-jitter_pct)*base at minimum).
inline std::chrono::milliseconds apply_jitter(std::chrono::milliseconds delay,
                                               double rand_01,
                                               double jitter_pct) {
    if (jitter_pct <= 0.0) return delay;
    double factor = 1.0 + jitter_pct * (2.0 * rand_01 - 1.0);  // [1-j, 1+j]
    if (factor < 0.01) factor = 0.01;  // hard floor so we never return 0 or negative
    return std::chrono::milliseconds(static_cast<int64_t>(delay.count() * factor));
}

inline std::chrono::milliseconds backoff_delay(int attempt,
                                                double rand_01,
                                                const BackoffConfig& cfg = {}) {
    return apply_jitter(backoff_delay_no_jitter(attempt, cfg), rand_01, cfg.jitter_pct);
}

} // namespace trader::kalshi
