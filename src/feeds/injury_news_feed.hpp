#pragma once

// Injury-news kill switch — interface for plugging in a feed that flags
// when star-player injury news breaks during the trading window.
//
// WHY THIS EXISTS:
// The Polymarket "Meet Your Market Maker" operator interview explicitly
// described their single most important manual control: a dashboard button
// labeled "turn off MM for this sport, clear all orders." They use it when
// breaking news hits ESPN/Twitter before the orderbook reacts — typically
// 10-60 seconds of advantage that sharp counterparties exploit by hitting
// stale resting quotes.
//
// At our scale we can't replicate the operator's Twitter beat-reporter
// dashboard, but a periodic ESPN injury-report scrape gives us SOME
// awareness of late-breaking lineup changes. The kill switch:
//   - Polls ESPN's NBA injury report (HTML scrape; no API needed)
//   - Maintains a set of "active injury concerns" by team
//   - When `should_freeze_team(team)` returns true, NBA strategy must:
//        a) cancel any resting orders on markets involving that team
//        b) refuse to generate new signals for those markets for N minutes
//
// IMPLEMENTATION STATUS:
// - This file: interface definition.
// - injury_news_feed.cpp: NullInjuryFeed stub that always returns false.
// - TODO: a real EspnInjuryFeed that scrapes
//     https://www.espn.com/nba/injuries
//   parses the HTML table, and freezes teams when a new "Out" / "Day-to-day"
//   row appears for a starter. Use core/http.cpp for the fetch.
//
// Note: ESPN's HTML structure changes occasionally. The scraper must be
// defensive — log + return "no change" on parse failure rather than
// erroneously freezing every team.

#include <chrono>
#include <string>

namespace trader::feeds {

class IInjuryNewsFeed {
public:
    virtual ~IInjuryNewsFeed() = default;

    // Returns true if the strategy must freeze trading on markets involving
    // the given team (case-insensitive tricode, e.g. "BOS", "LAL"). The
    // implementation decides freeze duration based on news recency +
    // severity.
    virtual bool should_freeze_team(const std::string& tricode) = 0;

    // Best-effort refresh. Caller should invoke periodically (e.g. once per
    // strategy tick). Implementations may rate-limit internally.
    virtual void refresh() = 0;

    // Source name for logging.
    virtual std::string name() const = 0;
};

// Default stub. Always returns false → strategy never freezes.
class NullInjuryNewsFeed : public IInjuryNewsFeed {
public:
    bool should_freeze_team(const std::string&) override { return false; }
    void refresh() override {}
    std::string name() const override { return "null"; }
};

} // namespace trader::feeds
