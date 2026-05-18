#pragma once

#include <string>

namespace trader::nba {

// Parses Kalshi NBA per-game moneyline tickers.
//
// Format observed May 2026:  KXNBAGAME-{YY}{mon}{DD}{away3}{home3}
//   e.g. KXNBAGAME-26may18sasokc   →  San Antonio Spurs @ Oklahoma City Thunder
//        KXNBAGAME-26may19lalden   →  Lakers @ Nuggets
//
// The 3-letter codes match Kalshi's lowercase tricode set, which corresponds
// 1:1 to standard NBA tricodes — but Kalshi sometimes uses non-standard
// abbreviations (e.g. "sas" for Spurs, "nop" for Pelicans). The caller should
// resolve codes through a canonical table.
//
// IMPORTANT: the binary contract's YES side semantics — which team is the YES
// outcome — is NOT derivable from the ticker. The Kalshi market metadata's
// `yes_sub_title` field gives the team name explicitly. Strategy code MUST
// fetch and compare against that field; never assume YES = home or YES = away.

struct NbaGameTicker {
    bool valid = false;
    std::string raw;          // "KXNBAGAME-26may18sasokc"
    std::string date_iso;     // "2026-05-18"
    std::string away_code;    // "sas" (lowercase tricode, as listed by Kalshi)
    std::string home_code;    // "okc"
};

// Returns parsed components. .valid = false on any parse failure.
NbaGameTicker parse_nba_game_ticker(const std::string& ticker);

// Format an NBA Kalshi ticker from its parts. Inverse of parse_nba_game_ticker.
// month_num is 1..12. Returns lowercased ticker.
std::string format_nba_game_ticker(int year_2digit, int month_num, int day,
                                    const std::string& away_code,
                                    const std::string& home_code);

} // namespace trader::nba
