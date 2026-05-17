// Pin the Kalshi 2026 wire format (post-April-2 fixed-point migration).
// These tests guarantee:
//   1. price_to_kalshi_string emits the exact 4-decimal format.
//   2. count_to_kalshi_fp_string emits 2-decimal "10.00" format.
//   3. parse_kalshi_price accepts strings, accepts numbers as DOLLARS (never
//      legacy cents), tolerates missing fields.
//   4. parse_market rehydrates a market from a representative 2026 JSON
//      without 100x'ing the price, losing precision, or misinterpreting
//      the `count_fp` field.
// If Kalshi changes wire format again, update encoding.hpp and these tests
// in one place.

#include <gtest/gtest.h>
#include "exchange/kalshi/encoding.hpp"
#include "exchange/kalshi/rest_client.hpp"
#include <nlohmann/json.hpp>

using namespace trader::kalshi;
using json = nlohmann::json;

// ===== Encoding =====

TEST(KalshiEncoding, PriceFormatIsExactly4Decimals) {
    EXPECT_EQ(price_to_kalshi_string(0.65),   "0.6500");
    EXPECT_EQ(price_to_kalshi_string(0.01),   "0.0100");
    EXPECT_EQ(price_to_kalshi_string(0.99),   "0.9900");
    EXPECT_EQ(price_to_kalshi_string(0.6525), "0.6525");
    EXPECT_EQ(price_to_kalshi_string(1.0),    "1.0000");
    EXPECT_EQ(price_to_kalshi_string(0.0),    "0.0000");
}

TEST(KalshiEncoding, CountFpFormatIsExactly2Decimals) {
    EXPECT_EQ(count_to_kalshi_fp_string(1),   "1.00");
    EXPECT_EQ(count_to_kalshi_fp_string(10),  "10.00");
    EXPECT_EQ(count_to_kalshi_fp_string(100), "100.00");
    EXPECT_EQ(count_to_kalshi_fp_string(0),   "0.00");
}

TEST(KalshiEncoding, CountFpFromDoubleSnapsToCent) {
    EXPECT_EQ(count_to_kalshi_fp_string(1.5),     "1.50");
    EXPECT_EQ(count_to_kalshi_fp_string(0.25),    "0.25");
    EXPECT_EQ(count_to_kalshi_fp_string(0.255),   "0.26");   // rounds-half-away
    EXPECT_EQ(count_to_kalshi_fp_string(0.254),   "0.25");
}

// ===== Price parsing — string format =====

TEST(KalshiEncoding, ParsesStringPrice) {
    json j = {{"yes_bid", "0.6500"}};
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "yes_bid"), 0.65);
}

TEST(KalshiEncoding, ParsesStringPriceBoundary) {
    json j = {{"p", "0.0100"}};
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "p"), 0.01);
    j["p"] = "0.9900";
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "p"), 0.99);
}

// ===== Price parsing — numeric fallback is DOLLARS, not legacy cents =====

TEST(KalshiEncoding, ParsesNumericPriceAsDollars) {
    // Critical safety test: if Kalshi server ever regresses to a numeric shape,
    // it must still be interpreted as a dollar decimal (0.65 = 65 cents), NOT
    // legacy integer cents (65 = $65). This test pins that forever.
    json j = {{"yes_bid", 0.65}};
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "yes_bid"), 0.65);
}

TEST(KalshiEncoding, ParsesIntegerPriceAsDollars) {
    // A bare `1` must parse as $1 (settled-YES price), not as 1¢.
    json j = {{"yes_bid", 1}};
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "yes_bid"), 1.0);
}

TEST(KalshiEncoding, MissingPriceReturnsDefault) {
    json j = json::object();
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "yes_bid"), 0.0);
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "yes_bid", 0.5), 0.5);
}

TEST(KalshiEncoding, NullPriceReturnsDefault) {
    json j = {{"yes_bid", nullptr}};
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "yes_bid", 0.3), 0.3);
}

TEST(KalshiEncoding, MalformedStringPriceReturnsDefault) {
    json j = {{"yes_bid", "not-a-number"}};
    EXPECT_DOUBLE_EQ(parse_kalshi_price(j, "yes_bid", 0.5), 0.5);
}

// ===== count_fp parsing =====

TEST(KalshiEncoding, ParsesCountFpString) {
    json j = {{"count_fp", "10.00"}};
    EXPECT_EQ(parse_kalshi_count_fp(j, "count_fp"), 10);
}

TEST(KalshiEncoding, ParsesCountFpStringWithFractionalTruncates) {
    // Whole-number caller truncates toward zero — conservative default while
    // the strategy layer is still `int`-typed.
    json j = {{"count_fp", "10.99"}};
    EXPECT_EQ(parse_kalshi_count_fp(j, "count_fp"), 10);
}

TEST(KalshiEncoding, ParsesLegacyIntegerCount) {
    json j = {{"count", 7}};
    EXPECT_EQ(parse_kalshi_count_fp(j, "count"), 7);
}

TEST(KalshiEncoding, ParsesCountFpDoublePreservesFractional) {
    // The double variant preserves the fraction for callers that want it.
    json j = {{"count_fp", "10.50"}};
    EXPECT_DOUBLE_EQ(parse_kalshi_count_fp_double(j, "count_fp"), 10.5);
}

// ===== End-to-end: parse_market with new wire format =====

TEST(KalshiEncoding, ParseMarketWithStringPrices) {
    // Representative 2026 payload shape.
    json j = {
        {"ticker", "KXHIGHNY-26APR20-T75"},
        {"title", "NYC high 26-Apr-20"},
        {"category", "weather"},
        {"status", "active"},
        {"yes_bid", "0.4200"},
        {"yes_ask", "0.4800"},
        {"last_price", "0.4500"},
        {"volume", 150},
        {"open_interest", 1200},
        {"close_time", "2026-04-20T23:59:00Z"}
    };
    auto m = KalshiRestClient::parse_market(j);
    EXPECT_EQ(m.ticker, "KXHIGHNY-26APR20-T75");
    EXPECT_DOUBLE_EQ(m.yes_bid, 0.42);
    EXPECT_DOUBLE_EQ(m.yes_ask, 0.48);
    EXPECT_DOUBLE_EQ(m.last_price, 0.45);
    EXPECT_EQ(m.volume, 150);
}

TEST(KalshiEncoding, ParseMarketDoesNotMis100xNumericPrice) {
    // Regression guard for the worst-case failure: a numeric 0.42 must NOT
    // be treated as "42 cents" and silently scaled to 0.0042 or inflated to 42.
    json j = {
        {"ticker", "FOO"}, {"category", "x"}, {"status", "active"},
        {"yes_bid", 0.42}, {"yes_ask", 0.48}
    };
    auto m = KalshiRestClient::parse_market(j);
    EXPECT_DOUBLE_EQ(m.yes_bid, 0.42);
    EXPECT_DOUBLE_EQ(m.yes_ask, 0.48);
}

TEST(KalshiEncoding, ParseMarketMissingPricesDefaultToZero) {
    json j = {{"ticker", "FOO"}, {"category", "x"}, {"status", "active"}};
    auto m = KalshiRestClient::parse_market(j);
    EXPECT_DOUBLE_EQ(m.yes_bid, 0.0);
    EXPECT_DOUBLE_EQ(m.yes_ask, 0.0);
    EXPECT_DOUBLE_EQ(m.last_price, 0.0);
}

// ===== kalshi_maker_post_price =====
//
// Regression guard for the fix we made after the April 2026 demo session
// exposed that submitting post_only=true at the ask gets rejected by Kalshi
// with "would cross". These tests pin the pricing contract so the strategy
// can run in maker mode without the venue silently rejecting every order.

TEST(KalshiMakerPostPrice, YesBuyPostsOneTickAboveBestBid) {
    // Normal spread — post at bid + 1¢.
    EXPECT_DOUBLE_EQ(kalshi_maker_post_price("yes", 0.30, 0.40), 0.31);
}

TEST(KalshiMakerPostPrice, NoBuyPostsOneTickAboveBestNoBid) {
    // Best NO bid is (1 - yes_ask) = 0.60; +tick = 0.61.
    EXPECT_DOUBLE_EQ(kalshi_maker_post_price("no", 0.30, 0.40), 0.61);
}

TEST(KalshiMakerPostPrice, NeverCrossesTheSpread) {
    // YES buy must end up strictly below yes_ask.
    double p_yes = kalshi_maker_post_price("yes", 0.48, 0.52);
    EXPECT_LT(p_yes, 0.52);
    // NO buy must end up strictly below (1 - yes_bid) = NO ask.
    double p_no = kalshi_maker_post_price("no", 0.48, 0.52);
    EXPECT_LT(p_no, 1.0 - 0.48);
}

TEST(KalshiMakerPostPrice, OneTickSpreadJoinsQueueNotCross) {
    // When bid+tick >= ask, posting at bid+tick would cross. Must clamp to
    // the existing best bid (join the queue, never cross).
    EXPECT_DOUBLE_EQ(kalshi_maker_post_price("yes", 0.50, 0.51), 0.50);
}

TEST(KalshiMakerPostPrice, SnapsToCentGrid) {
    // Kalshi trades on a 1¢ grid — rounding errors in intermediate math
    // must not leak sub-cent prices that the server will reject on submit.
    // Force a double that's off-grid: 0.30 + 0.01 computed naively is
    // 0.30000000000000004.
    double p = kalshi_maker_post_price("yes", 0.30, 0.40);
    double cents = p * 100.0;
    EXPECT_NEAR(cents, std::round(cents), 1e-9);
}

TEST(KalshiMakerPostPrice, DegenerateBookFallsBackToTakerPrice) {
    // Zero/inverted book — the book read was corrupt. Caller treats the
    // return as a taker price; our job is just not to NaN.
    EXPECT_DOUBLE_EQ(kalshi_maker_post_price("yes", 0.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(kalshi_maker_post_price("yes", 0.60, 0.40), 0.40);
}

TEST(KalshiMakerPostPrice, CustomTick) {
    // Default tick is 1¢, but the function takes an override for future
    // fractional-tick markets.
    EXPECT_DOUBLE_EQ(kalshi_maker_post_price("yes", 0.30, 0.40, 0.02), 0.32);
}

