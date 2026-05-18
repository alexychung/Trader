#include "strategy/nba/kalshi_nba_parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace trader::nba {

namespace {

constexpr std::array<const char*, 12> kMonthAbbrev = {
    "jan", "feb", "mar", "apr", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec"};

int month_from_abbrev(const std::string& abbr) {
    for (int i = 0; i < 12; ++i) {
        if (abbr == kMonthAbbrev[i]) return i + 1;
    }
    return 0;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool is_two_digit_num(const std::string& s) {
    return s.size() == 2 && std::isdigit(static_cast<unsigned char>(s[0])) &&
                            std::isdigit(static_cast<unsigned char>(s[1]));
}

bool is_three_letter_alpha(const std::string& s) {
    return s.size() == 3 && std::isalpha(static_cast<unsigned char>(s[0])) &&
                            std::isalpha(static_cast<unsigned char>(s[1])) &&
                            std::isalpha(static_cast<unsigned char>(s[2]));
}

} // namespace

NbaGameTicker parse_nba_game_ticker(const std::string& ticker) {
    NbaGameTicker out;
    out.raw = ticker;

    const std::string prefix = "KXNBAGAME-";
    if (ticker.size() < prefix.size() + 11) return out;
    if (ticker.compare(0, prefix.size(), prefix) != 0) return out;

    // Suffix layout after "KXNBAGAME-": YY mon DD away3 home3
    //                                    2  3  2   3    3   = 13 chars
    const std::string suffix = to_lower(ticker.substr(prefix.size()));
    if (suffix.size() < 13) return out;

    const std::string yy   = suffix.substr(0, 2);
    const std::string mon  = suffix.substr(2, 3);
    const std::string dd   = suffix.substr(5, 2);
    const std::string away = suffix.substr(7, 3);
    const std::string home = suffix.substr(10, 3);

    if (!is_two_digit_num(yy) || !is_two_digit_num(dd)) return out;
    const int month_num = month_from_abbrev(mon);
    if (month_num == 0) return out;
    if (!is_three_letter_alpha(away) || !is_three_letter_alpha(home)) return out;

    const int year_full = 2000 + std::stoi(yy);
    const int day_num   = std::stoi(dd);
    if (day_num < 1 || day_num > 31) return out;

    char date_buf[11];  // YYYY-MM-DD\0
    std::snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d",
                  year_full, month_num, day_num);

    out.valid = true;
    out.date_iso = date_buf;
    out.away_code = away;
    out.home_code = home;
    return out;
}

std::string format_nba_game_ticker(int year_2digit, int month_num, int day,
                                    const std::string& away_code,
                                    const std::string& home_code) {
    if (month_num < 1 || month_num > 12) return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "KXNBAGAME-%02d%s%02d%s%s",
                  year_2digit, kMonthAbbrev[month_num - 1], day,
                  to_lower(away_code).c_str(),
                  to_lower(home_code).c_str());
    return buf;
}

} // namespace trader::nba
