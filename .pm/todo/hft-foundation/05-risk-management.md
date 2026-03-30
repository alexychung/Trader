# Risk Management

**Status**: Not Started

---

## Scope

Build the risk management layer that protects capital by enforcing position limits, loss limits, and providing an emergency kill switch. This is the most critical safety component — it must be able to override and cancel all activity instantly.

**This spec covers**:
- Per-symbol position size limits, scaled by volatility regime
- **Portfolio-level exposure limits** (aggregate across all symbols and strategies)
- **Correlation-aware risk** (AVAX + ARB + NEAR are all correlated with BTC)
- Daily P&L tracking and loss limits (stop trading if daily loss exceeds threshold)
- Max drawdown from peak tracking
- Order rate limiting (don't exceed Binance limits)
- Kill switch (cancel all orders, flatten ALL positions across ALL strategies) with comprehensive triggers
- Pre-trade risk checks (reject orders that would breach limits)
- Stale data detection (data feed alive but no updates flowing)
- Funding rate strategy risk (basis divergence, margin utilization)

**Out of scope**:
- Strategy decisions → `03-strategy-engine.md`
- Order mechanics → `04-order-execution.md`

---

## What's Done

| Item | Status |
|------|--------|
| Position limits | Not started |
| P&L tracker | Not started |
| Kill switch | Not started |
| Pre-trade checks | Not started |

---

## Technical Context

### Risk Parameters (from config)
```yaml
risk:
  # Per-symbol limits
  max_position_usd: 500        # Base max position value per symbol
  max_open_orders_per_symbol: 10

  # Portfolio-level limits
  max_portfolio_exposure_usd: 3000  # Total |position| across all symbols and strategies
  max_correlation_adjusted_usd: 2000 # Exposure adjusted for BTC correlation

  # P&L limits
  max_daily_loss_usd: 100       # Stop ALL trading after $100 loss in rolling 24h
  max_drawdown_pct: 5.0         # Max drawdown from session peak (%)
  kill_switch_loss_usd: 200     # Emergency: cancel everything, flatten ALL

  # Rate and latency
  max_orders_per_second: 5      # Rate limit per symbol (Binance allows 10)
  stale_data_timeout_ms: 5000   # No book update = stale data
  max_order_latency_ms: 2000    # Order round trip too slow = pull quotes
  target_annual_vol: 0.5        # For volatility-based position scaling

  # Funding rate strategy specific
  max_basis_divergence_bps: 50  # Exit funding position if basis > 50 bps
  max_funding_notional_total: 6000  # Total across all funding positions
```

### Volatility-Scaled Position Limits

Fixed position limits are dangerous — $500 exposure in calm markets is fine, but during a flash crash it's reckless. Scale limits with inverse volatility:

```
effective_max_position = base_max_position * (target_vol / current_vol)
```

When volatility doubles, max position halves automatically. When markets are calm, you're allowed more exposure for the same dollar risk.

### Portfolio-Level Risk

With multiple strategies across multiple pairs, per-symbol risk is insufficient:

```
portfolio_exposure = Σ |position_usd(i)| for all symbols i, all strategies
```

**Correlation adjustment**: Most altcoins are 0.5-0.8 correlated with BTC. Being long AVAX, ARB, and NEAR simultaneously is nearly the same as being 3x long a single altcoin:

```
correlation_adjusted_exposure = portfolio_exposure * avg_pairwise_correlation
```

If `correlation_adjusted_exposure > max_correlation_adjusted_usd`, reduce positions starting with the least profitable pair.

**Funding positions count**: A spot long + perp short is delta-neutral in theory, but basis can diverge. Count funding positions at 20% of notional toward portfolio exposure (reflecting basis risk).

### Pre-Trade Check Flow
```
Strategy generates quote
  → Risk check: position limit OK? (use volatility-scaled limit)
  → Risk check: daily loss limit OK?
  → Risk check: max drawdown from peak OK?
  → Risk check: rate limit OK?
  → Risk check: data freshness OK? (last book update < stale_data_timeout)
  → If all pass → send to execution
  → If any fail → reject, log reason
```

### Stale Data Detection

A WebSocket connection can be alive while data stops flowing (exchange freeze, network buffer stall). This is more dangerous than a disconnect because the bot thinks it has good data.

Track `last_book_update_timestamp` for each symbol. If `now - last_book_update > stale_data_timeout_ms`, treat data as stale:
1. Cancel all open orders for that symbol
2. Log warning
3. If stale for > 3x timeout, trigger kill switch

### Kill Switch Triggers
1. Daily loss exceeds `kill_switch_loss_usd`
2. Drawdown from session peak exceeds `max_drawdown_pct`
3. Manual trigger (keyboard shortcut or command)
4. WebSocket disconnect > 5 seconds
5. Data feed stale > 3x `stale_data_timeout_ms` (connection alive but no updates)
6. Order round-trip latency exceeds `max_order_latency_ms`
7. Order book crossed or empty (exchange issue)
8. Abnormal fill rate (10x normal fills in 1 second — likely flash crash or fat finger)

### Kill Switch Actions
1. Cancel ALL open orders immediately (use `DELETE /api/v3/openOrders`)
2. Place aggressive IOC orders to flatten position (accept slippage)
3. Prevent any new order placement
4. Log trigger reason, current state, and all pending orders
5. Do NOT auto-restart — requires manual intervention

**Implementation note**: The kill switch must be on a separate code path with minimal dependencies. It should work even if the main strategy loop is hung. Consider a separate watchdog thread that monitors heartbeats from other threads.

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 1 | Multi-symbol position tracker (tracks fills, P&L, drawdown per symbol and portfolio-wide) | Tracks position and P&L per symbol and aggregated across all strategies; unit tests for multi-symbol fill sequences |
| 2 | Volatility-scaled and portfolio-level position limits | Per-symbol vol-scaled limits + portfolio exposure limit + correlation-adjusted exposure; unit tests for each |
| 3 | Pre-trade risk gate (validates against per-symbol AND portfolio limits) | Blocks orders exceeding vol-scaled limits, portfolio exposure, daily loss, drawdown, rate, data freshness; unit tests |
| 4 | Stale data detector (monitors last update timestamp per symbol) | Detects stale data per symbol even when WebSocket is connected, cancels that symbol's orders, escalates to kill switch |
| 5 | Kill switch (cancel ALL orders across ALL symbols, flatten ALL positions, halt) | Cancels all orders on all symbols, flattens all positions including funding legs, < 1 second on testnet |

---

## References

- Binance rate limits: https://binance-docs.github.io/apidocs/spot/en/#limits
