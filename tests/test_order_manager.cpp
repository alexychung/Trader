#include <gtest/gtest.h>
#include "exchange/kalshi/order_manager.hpp"

using namespace trader;
using namespace trader::kalshi;

namespace {

TrackedOrder make_open_yes_order(const std::string& id, const std::string& ticker,
                                   double price, Timestamp created_at) {
    TrackedOrder o;
    o.id = id;
    o.ticker = ticker;
    o.side = Side::Buy;
    o.contract_side = "yes";
    o.price = price;
    o.quantity = 5;
    o.filled_quantity = 0;
    o.status = OrderStatus::Open;
    o.is_maker = true;
    o.created_at = created_at;
    return o;
}

TrackedOrder make_open_no_order(const std::string& id, const std::string& ticker,
                                  double price, Timestamp created_at) {
    auto o = make_open_yes_order(id, ticker, price, created_at);
    o.contract_side = "no";
    return o;
}

MarketUpdate make_update(const std::string& ticker, double yes_bid, double yes_ask) {
    MarketUpdate mu;
    mu.ticker = ticker;
    mu.yes_bid = yes_bid;
    mu.yes_ask = yes_ask;
    mu.last_price = (yes_bid + yes_ask) * 0.5;
    mu.volume = 100;
    return mu;
}

MarketPosition make_yes_position(const std::string& ticker, int qty, double avg_cost) {
    MarketPosition p;
    p.ticker = ticker;
    p.contract_side = "yes";
    p.quantity = qty;
    p.avg_cost = avg_cost;
    p.total_cost = avg_cost * qty;
    return p;
}

OrderManager::Config default_cfg() {
    OrderManager::Config c;
    c.max_unfilled_age = std::chrono::seconds(300);
    c.reprice_threshold = 0.02;
    c.force_exit_prob_flip = 0.10;
    return c;
}

}  // namespace

// ===== decide_order_action: stale cancellation =====

TEST(OrderManagerDecision, KeepsFreshOrder) {
    auto now = std::chrono::system_clock::now();
    auto order = make_open_yes_order("o1", "MKT", 0.50, now - std::chrono::seconds(30));
    auto mu = make_update("MKT", 0.50, 0.55);
    auto action = OrderManager::decide_order_action(order, &mu, now, default_cfg());
    EXPECT_EQ(action, OrderManager::OrderAction::None);
}

TEST(OrderManagerDecision, CancelsOrderOlderThanMaxUnfilledAge) {
    auto now = std::chrono::system_clock::now();
    auto order = make_open_yes_order("o1", "MKT", 0.50,
                                       now - std::chrono::seconds(400));  // > 5min
    auto mu = make_update("MKT", 0.50, 0.55);
    auto action = OrderManager::decide_order_action(order, &mu, now, default_cfg());
    EXPECT_EQ(action, OrderManager::OrderAction::CancelStale);
}

TEST(OrderManagerDecision, FilledOrdersNeverCancelled) {
    auto now = std::chrono::system_clock::now();
    auto order = make_open_yes_order("o1", "MKT", 0.50,
                                       now - std::chrono::seconds(400));
    order.status = OrderStatus::Filled;
    auto action = OrderManager::decide_order_action(order, nullptr, now, default_cfg());
    EXPECT_EQ(action, OrderManager::OrderAction::None);
}

TEST(OrderManagerDecision, CancelledOrdersNeverActedOn) {
    auto now = std::chrono::system_clock::now();
    auto order = make_open_yes_order("o1", "MKT", 0.50,
                                       now - std::chrono::seconds(400));
    order.status = OrderStatus::Cancelled;
    auto action = OrderManager::decide_order_action(order, nullptr, now, default_cfg());
    EXPECT_EQ(action, OrderManager::OrderAction::None);
}

// ===== decide_order_action: reprice =====

TEST(OrderManagerDecision, RepricesYesOrderWhenBidMovesPastThreshold) {
    auto now = std::chrono::system_clock::now();
    // Original price 0.50; market bid now 0.54 (4¢ move > 2¢ threshold).
    auto order = make_open_yes_order("o1", "MKT", 0.50, now - std::chrono::seconds(10));
    auto mu = make_update("MKT", 0.54, 0.58);
    EXPECT_EQ(OrderManager::decide_order_action(order, &mu, now, default_cfg()),
              OrderManager::OrderAction::CancelReprice);
}

TEST(OrderManagerDecision, DoesNotRepriceBelowThreshold) {
    auto now = std::chrono::system_clock::now();
    auto order = make_open_yes_order("o1", "MKT", 0.50, now - std::chrono::seconds(10));
    auto mu = make_update("MKT", 0.51, 0.55);  // 1¢ move
    EXPECT_EQ(OrderManager::decide_order_action(order, &mu, now, default_cfg()),
              OrderManager::OrderAction::None);
}

TEST(OrderManagerDecision, RepricesNoOrderAgainstYesAsk) {
    auto now = std::chrono::system_clock::now();
    // NO order priced at 0.40 means we implied yes_ask ~0.60. If yes_ask
    // drops to 0.55, NO-implied price is 0.45 — 5¢ delta, reprice.
    auto order = make_open_no_order("o1", "MKT", 0.40, now - std::chrono::seconds(10));
    auto mu = make_update("MKT", 0.50, 0.55);
    EXPECT_EQ(OrderManager::decide_order_action(order, &mu, now, default_cfg()),
              OrderManager::OrderAction::CancelReprice);
}

TEST(OrderManagerDecision, NoMarketUpdateMeansNoReprice) {
    // Reprice needs current market — stale age still triggers though.
    auto now = std::chrono::system_clock::now();
    auto fresh = make_open_yes_order("o1", "MKT", 0.50, now - std::chrono::seconds(30));
    EXPECT_EQ(OrderManager::decide_order_action(fresh, nullptr, now, default_cfg()),
              OrderManager::OrderAction::None);

    auto stale = make_open_yes_order("o2", "MKT", 0.50, now - std::chrono::seconds(400));
    EXPECT_EQ(OrderManager::decide_order_action(stale, nullptr, now, default_cfg()),
              OrderManager::OrderAction::CancelStale);
}

TEST(OrderManagerDecision, StaleCheckTakesPrecedenceOverReprice) {
    // An old order on a market that also moved: stale wins (simpler cleanup).
    auto now = std::chrono::system_clock::now();
    auto order = make_open_yes_order("o1", "MKT", 0.50, now - std::chrono::seconds(400));
    auto mu = make_update("MKT", 0.60, 0.65);
    EXPECT_EQ(OrderManager::decide_order_action(order, &mu, now, default_cfg()),
              OrderManager::OrderAction::CancelStale);
}

// ===== decide_force_exit =====

TEST(OrderManagerDecision, DoesNotForceExitWhenModelStillFavorable) {
    auto pos = make_yes_position("MKT", 5, 0.50);  // entered at 0.50
    auto mu = make_update("MKT", 0.48, 0.52);
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.60, default_cfg());
    EXPECT_FALSE(d.should_exit);
}

TEST(OrderManagerDecision, ForceExitsWhenModelFlipsHardAgainstYesPosition) {
    // Position: YES @ avg_cost 0.60 (so we believed P(YES) > 0.60 entering).
    // New model: P(YES) = 0.30 → side_prob=0.30, swing vs break-even = 0.30,
    // well past the 10pp threshold.
    auto pos = make_yes_position("MKT", 5, 0.60);
    auto mu = make_update("MKT", 0.40, 0.45);
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.30, default_cfg());
    ASSERT_TRUE(d.should_exit);
    EXPECT_DOUBLE_EQ(d.exit_price, 0.40);  // YES sells at current yes_bid
    EXPECT_EQ(d.quantity, 5);
}

TEST(OrderManagerDecision, ForceExitsNoPositionUsesOneMinusYesAsk) {
    // NO position: exits at (1 - yes_ask).
    MarketPosition pos;
    pos.ticker = "MKT";
    pos.contract_side = "no";
    pos.quantity = 3;
    pos.avg_cost = 0.60;
    pos.total_cost = 1.80;

    auto mu = make_update("MKT", 0.55, 0.62);
    // Model now strongly favors YES → NO side_prob = 1 - 0.80 = 0.20.
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.80, default_cfg());
    ASSERT_TRUE(d.should_exit);
    EXPECT_DOUBLE_EQ(d.exit_price, 1.0 - 0.62);
}

TEST(OrderManagerDecision, ForceExitBelowThresholdStaysIn) {
    // Position at 0.55 avg_cost, current side_prob = 0.48 → swing 0.07.
    // Below the 10pp force_exit_prob_flip threshold. Hold.
    auto pos = make_yes_position("MKT", 5, 0.55);
    auto mu = make_update("MKT", 0.48, 0.52);
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.48, default_cfg());
    EXPECT_FALSE(d.should_exit);
}

TEST(OrderManagerDecision, DoesNotForceExitSettledPosition) {
    auto pos = make_yes_position("MKT", 5, 0.60);
    pos.is_settled = true;
    auto mu = make_update("MKT", 0.40, 0.45);
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.30, default_cfg());
    EXPECT_FALSE(d.should_exit);
}

TEST(OrderManagerDecision, DoesNotForceExitAtPennyPrices) {
    // Exit price below 1¢ means the market is effectively dead — don't send
    // a sell order that will sit rejected.
    auto pos = make_yes_position("MKT", 5, 0.60);
    auto mu = make_update("MKT", 0.005, 0.01);  // penny book
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.10, default_cfg());
    EXPECT_FALSE(d.should_exit);
}

TEST(OrderManagerDecision, DoesNotForceExitWithoutMarketUpdate) {
    auto pos = make_yes_position("MKT", 5, 0.60);
    auto d = OrderManager::decide_force_exit(pos, nullptr, 0.10, default_cfg());
    EXPECT_FALSE(d.should_exit);
}

TEST(OrderManagerDecision, DoesNotForceExitWhenMarketAlreadyAgrees) {
    // Model flipped hard (P(YES)=0.25) but the book has ALREADY moved — the
    // yes_bid is 0.23, essentially at the new fair value. Selling into this
    // book would lock in a 0.37/contract loss (from 0.60 entry) with no EV
    // gain vs holding to settlement, since market and model now agree.
    // Prior implementation would have exited (avg_cost 0.60 − side_prob 0.25
    // = 0.35 ≥ 0.10). New implementation correctly holds.
    auto pos = make_yes_position("MKT", 5, 0.60);
    auto mu = make_update("MKT", 0.23, 0.28);
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.25, default_cfg());
    EXPECT_FALSE(d.should_exit);
}

TEST(OrderManagerDecision, ForceExitsWhenBookStillOvervaluesOurSide) {
    // Same underdog model (P(YES)=0.30) but the book hasn't caught up:
    // yes_bid 0.45 leaves a 0.15 positive EV gap vs holding. Exit.
    auto pos = make_yes_position("MKT", 5, 0.60);
    auto mu = make_update("MKT", 0.45, 0.50);
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.30, default_cfg());
    ASSERT_TRUE(d.should_exit);
    EXPECT_DOUBLE_EQ(d.exit_price, 0.45);
}

TEST(OrderManagerDecision, ForceExitWinProbThresholdIsConfigurable) {
    // Stricter threshold (0.40) should keep us in when side_prob is 0.42.
    OrderManager::Config cfg = default_cfg();
    cfg.force_exit_win_prob = 0.40;

    auto pos = make_yes_position("MKT", 5, 0.60);
    auto mu = make_update("MKT", 0.55, 0.60);
    auto d = OrderManager::decide_force_exit(pos, &mu, 0.42, cfg);
    EXPECT_FALSE(d.should_exit);

    // Same position with the default 0.45 threshold would still skip exit
    // here because 0.42 < 0.45 but gap = 0.55-0.42 = 0.13 ≥ 0.10 would fire.
    // Confirm the stricter cfg blocks it.
    auto d_default = OrderManager::decide_force_exit(pos, &mu, 0.42, default_cfg());
    EXPECT_TRUE(d_default.should_exit);
}
