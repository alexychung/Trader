#include "strategy/nba/kalshi_nba_parser.hpp"

#include <gtest/gtest.h>

using trader::nba::parse_nba_game_ticker;
using trader::nba::format_nba_game_ticker;

TEST(NbaKalshiParser, ParsesCanonicalTicker) {
    auto t = parse_nba_game_ticker("KXNBAGAME-26may18sasokc");
    ASSERT_TRUE(t.valid);
    EXPECT_EQ(t.date_iso, "2026-05-18");
    EXPECT_EQ(t.away_code, "sas");
    EXPECT_EQ(t.home_code, "okc");
}

TEST(NbaKalshiParser, ParsesEachMonth) {
    struct Row { const char* ticker; const char* date; };
    Row rows[] = {
        {"KXNBAGAME-26jan02bosnyk", "2026-01-02"},
        {"KXNBAGAME-26feb14lalden", "2026-02-14"},
        {"KXNBAGAME-26mar31gswphx", "2026-03-31"},
        {"KXNBAGAME-26apr01milchi", "2026-04-01"},
        {"KXNBAGAME-26may18sasokc", "2026-05-18"},
        {"KXNBAGAME-26jun07lalbos", "2026-06-07"},
        {"KXNBAGAME-26oct30bosnyk", "2026-10-30"},
        {"KXNBAGAME-26dec25lalgsw", "2026-12-25"},
    };
    for (const auto& r : rows) {
        auto t = parse_nba_game_ticker(r.ticker);
        ASSERT_TRUE(t.valid) << r.ticker;
        EXPECT_EQ(t.date_iso, r.date) << r.ticker;
    }
}

TEST(NbaKalshiParser, AcceptsMixedCaseAndLowercasesOutput) {
    auto t = parse_nba_game_ticker("KXNBAGAME-26MAY18SASOKC");
    ASSERT_TRUE(t.valid);
    EXPECT_EQ(t.away_code, "sas");
    EXPECT_EQ(t.home_code, "okc");
}

TEST(NbaKalshiParser, RejectsWrongPrefix) {
    EXPECT_FALSE(parse_nba_game_ticker("KXHIGHNY-26apr25-T75").valid);
    EXPECT_FALSE(parse_nba_game_ticker("KXNBA-26").valid);
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAM-26may18sasokc").valid);
}

TEST(NbaKalshiParser, RejectsBadMonth) {
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-26xyz18sasokc").valid);
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-26mxx18sasokc").valid);
}

TEST(NbaKalshiParser, RejectsNonNumericDate) {
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-aamay18sasokc").valid);
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-26mayXXsasokc").valid);
}

TEST(NbaKalshiParser, RejectsNonAlphaTricode) {
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-26may18sa1okc").valid);
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-26may18sas1kc").valid);
}

TEST(NbaKalshiParser, RejectsTooShort) {
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-26may18sas").valid);
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-").valid);
    EXPECT_FALSE(parse_nba_game_ticker("").valid);
}

TEST(NbaKalshiParser, RejectsImpossibleDayOfMonth) {
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-26may32sasokc").valid);
    EXPECT_FALSE(parse_nba_game_ticker("KXNBAGAME-26may00sasokc").valid);
}

TEST(NbaKalshiParser, FormatRoundTrip) {
    std::string t = format_nba_game_ticker(26, 5, 18, "SAS", "OKC");
    EXPECT_EQ(t, "KXNBAGAME-26may18sasokc");

    auto parsed = parse_nba_game_ticker(t);
    ASSERT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.date_iso, "2026-05-18");
    EXPECT_EQ(parsed.away_code, "sas");
    EXPECT_EQ(parsed.home_code, "okc");
}

TEST(NbaKalshiParser, FormatRejectsBadMonth) {
    EXPECT_EQ(format_nba_game_ticker(26, 0, 18, "sas", "okc"), "");
    EXPECT_EQ(format_nba_game_ticker(26, 13, 18, "sas", "okc"), "");
}

TEST(NbaKalshiParser, IgnoresContractSideSuffixIfPresent) {
    // Kalshi sometimes returns the market ticker as the series prefix and
    // separately the binary outcome. If callers pass a longer string with a
    // suffix, we should still parse the first 13 chars after the prefix.
    auto t = parse_nba_game_ticker("KXNBAGAME-26may18sasokc-YES");
    EXPECT_TRUE(t.valid);
    EXPECT_EQ(t.date_iso, "2026-05-18");
}
