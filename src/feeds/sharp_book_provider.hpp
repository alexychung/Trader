#pragma once

// Sharp-book fair-value provider — interface for plugging in an external
// "ground truth" probability source like Pinnacle Sports closing lines.
//
// WHY THIS EXISTS (2026-05-20 strategy pivot):
// The Polymarket "Meet Your Market Maker" operator interview (cited in
// memory/project_kalshi_mm_research_findings.md) confirmed what every named
// profitable Kalshi/Polymarket operator does: they don't compute fair value
// from price action — they read it off Pinnacle. Pinnacle is the universally
// recognized sharp book; their closing lines have the tightest historical
// agreement with actual outcomes. Closing Line Value (CLV) is the industry
// proxy for whether you have edge.
//
// Strategy integration: when this provider returns a sharp fair, the NBA
// strategy compares (kalshi_mid vs sharp_implied_prob). It fires a signal
// only if Kalshi has drifted ≥ min_clv_edge AWAY from sharp in our favor.
// Without this provider, the strategy falls back to the arcsine self-model
// only (which has no external sanity check).
//
// IMPLEMENTATION STATUS:
// - This file: interface definition.
// - sharp_book_provider.cpp: NullProvider stub that returns std::nullopt for
//   every call (default — bot still runs without sharp-fair gating).
// - TODO: a real PinnacleProvider implementation. Options:
//     1. Paid: OddsAPI (https://the-odds-api.com/) — 500 free requests/mo,
//        $30+/mo for higher tiers. Includes Pinnacle lines.
//     2. Paid: OddsJam, BettingPros, others (~$50-200/mo).
//     3. Pinnacle's own API requires their partner program (rare for retail).
//     4. Scrape: a third-party Pinnacle mirror (fragile, ToS-questionable).
//   Recommended: start with OddsAPI free tier for proof-of-concept; upgrade
//   only after the strategy is shown to produce signals.
//
// Game-key convention: callers pass (away_tricode, home_tricode, date_iso)
// matching the NbaScoreSnapshot. Provider returns the implied probability of
// the HOME team winning (consistent with NbaStrategy's home_wp orientation).

#include <optional>
#include <string>

namespace trader::feeds {

struct SharpFair {
    double home_win_prob;   // Pinnacle implied probability of home win, [0, 1]
    double confidence;      // 0..1 — how "sharp" we consider this (e.g. how
                            // close to game time, line stability indicator)
    std::string source;     // human-readable: "pinnacle", "oddsapi", etc.
};

class ISharpBookProvider {
public:
    virtual ~ISharpBookProvider() = default;

    // Returns the sharp-book fair for the given game, or nullopt if unknown
    // (game not in feed, provider unavailable, cache stale, etc.).
    // `date_iso` is YYYY-MM-DD, away/home are NBA tricodes (lowercase OK).
    virtual std::optional<SharpFair> get_fair(const std::string& away_tricode,
                                               const std::string& home_tricode,
                                               const std::string& date_iso) = 0;

    // Source name for logging.
    virtual std::string name() const = 0;
};

// Default stub. Always returns nullopt. The bot runs normally; the CLV gate
// is simply a no-op when this provider is in place.
class NullSharpBookProvider : public ISharpBookProvider {
public:
    std::optional<SharpFair> get_fair(const std::string&,
                                       const std::string&,
                                       const std::string&) override {
        return std::nullopt;
    }
    std::string name() const override { return "null"; }
};

// Sketch for a future OddsAPI-backed implementation. NOT IMPLEMENTED.
// To wire up:
//   1. Sign up at https://the-odds-api.com/ (500 free requests/mo)
//   2. Add `feeds.oddsapi.api_key` to config.yaml
//   3. Implement OddsApiPinnacleProvider with the same HTTP client used by
//      the NBA score feed (src/core/http.cpp).
//   4. Cache results per (date, game) for ~5 minutes to stay under quota.
//   5. Return SharpFair with source="oddsapi-pinnacle" and confidence based
//      on minutes-to-tipoff (tighter = higher confidence).
//
// Quota math: 500 req/mo ≈ 16/day. With one fetch per game per refresh:
// 12 games × 2 fetches/day = 24/day → too many. Cache and batch — fetch
// the whole day's slate in 1-3 calls.

} // namespace trader::feeds
