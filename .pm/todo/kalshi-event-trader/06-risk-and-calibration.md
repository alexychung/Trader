# 06 — Risk Management + Calibration

**Status**: Not Started

---

## Scope

Build the risk management layer (shared with Phase 2) and the model calibration system (Kalshi-specific). Risk management protects capital. Calibration measures and improves model accuracy over time.

**This spec covers**:
- Position tracking across all markets
- Per-market and portfolio-level risk limits
- Kill switch (cancel all orders, halt trading)
- Model calibration logger
- Brier score and calibration curve computation

**Out of scope**:
- Strategy logic → `05-trading-strategy.md`
- Order execution → `02-kalshi-exchange.md`

---

## What's Done

| Item | Status |
|------|--------|
| Risk manager + kill switch | Not started |
| Calibration system | Not started |

---

## Technical Details

### Risk Manager

```cpp
class RiskManager {
public:
    // Pre-trade check: returns true if trade is allowed
    bool check_trade(const TradeSignal& signal) const;

    // Update state after fill
    void on_fill(const Fill& fill);

    // Update state after settlement
    void on_settlement(const Settlement& settlement);

    // Current exposure
    double total_exposure() const;         // Sum of all position costs
    double available_capital() const;      // Balance - exposure
    double daily_pnl() const;
    int active_market_count() const;

    // Kill switch
    bool should_kill() const;
    void trigger_kill(const std::string& reason);

private:
    std::unordered_map<std::string, Position> positions_;
    double starting_balance_;
    double current_balance_;
    double daily_pnl_;
    Timestamp day_start_;
    RiskConfig config_;
};
```

### Risk Checks (Pre-Trade Gate)

Every trade must pass ALL checks:

```
1. Position in this market < max_position_per_market (10 contracts)
2. Total exposure < max_total_exposure ($80)
3. Daily PnL > -max_daily_loss (-$15)
4. Active market count < max_markets_active (20)
5. Available capital > order cost + reserve ($20 minimum reserve)
6. Kill switch not triggered
```

If any check fails, the trade is rejected with a logged reason.

### Position Tracking

```cpp
struct Position {
    std::string market_ticker;
    std::string contract_side;    // "yes" or "no"
    int quantity;
    double avg_cost;              // Average entry price
    double current_value;         // Current market price × quantity
    double unrealized_pnl;        // (current_value - avg_cost × quantity)
    Timestamp first_entry;
    Timestamp last_update;
    bool is_settled;
    double settled_pnl;           // After resolution
};
```

**Capital recycling**: When a market settles, the capital (either $1.00/contract for winners or $0.00 for losers) is freed and available_capital increases. This is critical at $100 — capital is constantly rotating through settlements.

### Void Rule Risk

Markets that cannot be determined do NOT always void. Kalshi may resolve at the **"last traded fair price"** instead of voiding. Documented case of $30K loss in sports markets where player didn't participate. Risk mitigation:
- Avoid markets with ambiguous resolution criteria
- Check settlement source for every market before trading
- Size smaller on markets where void/ambiguity risk exists (sports, company events)
- Weather and BLS economic data have zero ambiguity (thermometer reading / official statistic)

### Kill Switch

**Triggers**:
1. Daily loss exceeds `kill_switch_loss` ($30)
2. Manual trigger (keyboard shortcut or config flag)
3. API connectivity lost for > 60 seconds
4. Unexpected error in order placement (3 consecutive failures)

**Actions**:
1. Cancel all open orders (iterate and cancel via REST)
2. Set `kill_active = true` — blocks all new trades
3. Log trigger reason with full state snapshot
4. **Do NOT auto-flatten positions** — Kalshi positions are binary contracts that will resolve on their own. Selling at a loss to flatten is usually worse than holding.
5. Require manual restart after cooldown

### Calibration System

```cpp
struct CalibrationRecord {
    std::string market_ticker;
    std::string category;
    Timestamp trade_time;
    Probability model_probability;
    ContractPrice market_price;
    double edge;
    Side side;
    int quantity;
    ContractPrice entry_price;
    // Filled after resolution:
    Timestamp resolution_time;
    bool outcome;                  // true = YES resolved, false = NO resolved
    double pnl;
};
```

**Stored in**: SQLite database (`data/calibration.db`) for persistent analysis.

### Brier Score

```
Brier = (1/N) Σ (predicted_probability - actual_outcome)²
```

Where `actual_outcome` is 1.0 (YES) or 0.0 (NO).

- Perfect score: 0.0
- Random guessing: 0.25
- Target: < 0.20 (better than random)
- Track per category to identify which models work best

### Calibration Curve

Bin predictions into buckets (0-10%, 10-20%, ..., 90-100%). For each bucket:
- Average predicted probability
- Actual frequency of YES outcomes

Plot: predicted vs actual. Perfect calibration = diagonal line.

```cpp
struct CalibrationBucket {
    double bucket_center;       // e.g., 0.35 for 30-40% bucket
    double avg_prediction;
    double actual_frequency;
    int sample_count;
};
```

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 16 | Risk manager + position tracking + pre-trade gate | RiskManager tracks positions, exposure, daily PnL. Pre-trade gate rejects orders that violate limits. Capital recycling on settlement. Unit tests verify all 6 risk checks with edge cases (at limit, over limit, under limit). |
| 17 | Kill switch | Separate watchdog that monitors risk state. Cancels all orders on trigger. Blocks new trades. Logs state snapshot. Unit test verifies trigger conditions and order cancellation sequence. |
| 18 | Calibration logger + Brier score + calibration curve | SQLite-backed calibration database. Logs every trade prediction. Computes Brier score per category after settlements. Generates calibration curve data. Unit test inserts mock records and verifies Brier calculation. Integration test creates DB, writes records, reads back. |
