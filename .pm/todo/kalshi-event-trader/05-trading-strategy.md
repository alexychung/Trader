# 05 — Trading Strategy

**Status**: Not Started

---

## Scope

Build the trading logic that converts probability estimates into trade decisions. Handles edge detection, position sizing, market making mode, and the IStrategy implementation for Kalshi.

**This spec covers**:
- Edge detection: compare model probability vs market price
- Position sizing: Kelly criterion adapted for binary outcomes
- Market making mode: quote both sides when spread is wide enough
- KalshiEventStrategy class implementing IStrategy interface

**Out of scope**:
- Probability calculation → `04-probability-engine.md`
- Order execution mechanics → `02-kalshi-exchange.md`
- Risk limits → `06-risk-and-calibration.md`

---

## What's Done

| Item | Status |
|------|--------|
| Edge detection + position sizing | Not started |
| Market making mode | Not started |
| KalshiEventStrategy | Not started |

---

## Technical Details

### Edge Detection

```cpp
struct TradeSignal {
    std::string market_ticker;
    Probability model_prob;      // Our estimate
    ContractPrice market_price;  // Current YES price on Kalshi
    double edge;                 // model_prob - market_price (for YES side)
    double confidence;           // Model confidence [0, 1]
    Side side;                   // Buy YES or Buy NO
    int suggested_quantity;
    std::string rationale;
};
```

**Edge calculation**:
```
edge_yes = model_prob - yes_ask    // Edge on buying YES
edge_no = (1 - model_prob) - no_ask  // Edge on buying NO

If edge_yes > min_edge_threshold AND confidence > min_confidence:
    → Buy YES at yes_ask

If edge_no > min_edge_threshold AND confidence > min_confidence:
    → Buy NO at no_ask
```

**Fee-aware edge calculation**:
```
maker_fee = ceil(0.0175 * 1 * price * (1 - price))  // per contract
net_EV = model_prob * $1.00 - market_price - maker_fee
```

**Minimum fee-adjusted edge by price**:
| Price | Maker Fee | Min Edge to Profit |
|-------|-----------|-------------------|
| $0.10 | ~0.16c | >1.6% probability edge |
| $0.25 | ~0.33c | >1.3% |
| $0.50 | ~0.44c | >0.9% |
| $0.75 | ~0.33c | >0.4% |

The 5-cent edge threshold (configurable) is conservative and well above break-even at all price points. Always use `post_only: true` for maker fees (4x lower than taker).

### Position Sizing — Fractional Kelly

Kelly criterion for binary outcomes:

```
f* = (p * b - q) / b

Where:
  p = model probability of winning
  q = 1 - p = model probability of losing
  b = odds (payout / cost - 1)
  f* = fraction of capital to bet
```

**Use fractional Kelly (25-50%)** to reduce variance:

```
position_size = floor(f* × kelly_fraction × available_capital / contract_price)
```

Clamp to `max_position_per_market` from risk config.

**Example**: Model says 40% YES, market price $0.20.
- b = ($1.00 - $0.20) / $0.20 = 4.0
- f* = (0.40 × 4.0 - 0.60) / 4.0 = 0.25
- At 50% Kelly: bet 12.5% of capital = $12.50
- At $0.20/contract: 62 contracts → clamped to max 10 contracts = $2.00

### Market Making Mode

When a market has a wide spread AND model confidence is high, place orders on both sides:

```
If spread > min_spread_to_mm (8 cents) AND confidence > 0.7:
    bid = max(model_prob - half_spread_target, best_bid + 0.01)
    ask = min(model_prob + half_spread_target, best_ask - 0.01)

    If bid < ask AND (ask - bid) > 2 × fee_per_side:
        → Place buy YES at bid
        → Place sell YES at ask (or buy NO equivalent)
```

**When to market-make vs directional**:
- **Directional**: When edge is large (>10%) — you have a strong view, take the position
- **Market make**: When edge is moderate (5-8%) but spread is wide — capture spread, less directional risk
- **Skip**: When edge is below threshold or confidence is low

### KalshiEventStrategy (IStrategy Implementation)

```cpp
class KalshiEventStrategy : public IStrategy {
public:
    void on_market_update(const MarketUpdate& update) override;
    void on_data_signal(const DataSignal& signal) override;
    void on_fill(const Fill& fill) override;
    void on_settlement(const Settlement& settlement) override;

    std::vector<TradeSignal> generate_signals() override;

private:
    ProbabilityEngine& prob_engine_;
    std::unordered_map<std::string, KalshiMarket> active_markets_;
    std::unordered_map<std::string, Position> positions_;
    RiskManager& risk_manager_;
};
```

**Main loop per strategy tick** (every 30-60 seconds):
1. Update market prices from latest data
2. Refresh probability estimates if new data signals arrived
3. Scan all active markets for edge
4. Generate trade signals for markets exceeding threshold
5. Apply position sizing (Kelly)
6. Check risk limits
7. Submit orders

### Market Selection

Not every Kalshi market is worth trading. Filter for:
- **Category**: weather (primary), economics (secondary), fed (opportunistic). Skip sports, crypto, stocks, company events.
- **Time to resolution**: 1-7 days for weather (daily resolution), 1-30 days for economics
- **Volume**: > 50 contracts traded (some minimum liquidity; weather markets can be thinner)
- **Spread**: > $0.03 (need room for our edge)
- **Price range**: $0.15-$0.85 (avoid extremes where favorite-longshot bias is strongest — per UCD study, sub-$0.10 contracts have -60% average return)
- **Maker only**: Filter out markets where you can't get a maker order filled (too thin or too fast-moving)

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 13 | Edge detection + fractional Kelly position sizing | Compares model probability vs market price. Calculates edge per market. Kelly sizing with configurable fraction. Unit tests verify edge calc for known inputs, Kelly sizing clamps correctly at limits. |
| 14 | Market making mode + market selection filter | Quotes both sides when spread is wide and confidence is high. Filter selects tradeable markets by category, time, volume, spread, price. Unit tests verify MM quotes don't cross, filter correctly includes/excludes markets. |
| 15 | KalshiEventStrategy integration | Full IStrategy implementation. Wires probability engine + edge detection + sizing + market filter into strategy tick loop. Publishes TradeSignals to event bus. Integration test runs one full tick cycle with mock market data and verifies signal generation. |
