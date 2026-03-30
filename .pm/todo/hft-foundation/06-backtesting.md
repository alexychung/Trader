# Backtesting Engine

**Status**: Not Started | **Partially awaiting**: Quant research bot (fill model details)

---

## Scope

Build a **strategy-agnostic** backtesting framework that replays historical market data through any `IStrategy` implementation to evaluate performance. The framework itself is independent of strategy choice.

**This spec covers**:
- Historical data collection and storage (Binance aggTrades + order book snapshots)
- Market data replay engine (simulates real-time feed from historical data)
- Simulated order matching with queue-aware fill model (default)
- Performance metrics (P&L, Sharpe ratio, max drawdown, adverse selection, etc.)
- Walk-forward optimization harness (prevents overfitting)
- PnL attribution decomposition (spread capture, inventory, fees, adverse selection, signal alpha)

**Out of scope**:
- Live strategy logic → `03-strategy-engine.md`
- Live execution → `04-order-execution.md`

---

## What's Done

| Item | Status |
|------|--------|
| Data collector | Not started |
| Replay engine | Not started |
| Simulated matching | Not started |
| Metrics calculator | Not started |

---

## Technical Context

### Historical Data Sources
- **Primary**: Binance Public Data (https://data.binance.vision/) — free aggTrades, klines, order book snapshots as CSV/ZIP, updated daily, all pairs back to listing date
- **Binance REST**: `GET /api/v3/klines` for OHLCV candles (bootstrap)
- **Binance REST**: `GET /api/v3/aggTrades` for tick-level trade data (bootstrap)
- **Ideal for realistic backtesting**: Tardis.dev (https://tardis.dev/) — recorded raw WebSocket messages, allows replaying exact order book state. Free tier with limited history.
- Store locally as CSV or binary for fast replay

**What to collect**:
- **Essential**: aggTrades (timestamp, price, quantity, is_buyer_maker) — minimum for basic backtesting
- **Ideal**: Full L2 order book snapshots at 100ms intervals + trade stream — required for realistic queue simulation and OBI signal backtesting
- **Minimum viable**: 1-second OHLCV candles + trade count + volume

### Simulated Fill Models (pluggable)

**The default must be queue-aware (conservative).** An optimistic fill model for market making produces fantasy returns — your limit order does not fill just because the price touches it.

- **Queue-aware (default, REQUIRED)**: A limit buy at price P fills only when a trade occurs at price P AND the trade is seller-initiated. Even then, fill only after all pre-existing volume at that price is consumed (assume back of queue):
  ```
  queue_ahead = book_qty_at_price_when_order_placed
  fill_probability = max(0, (trade_volume_at_price - queue_ahead)) / order_size
  ```
- **Optimistic (sanity check only)**: Fill whenever price crosses our level. Use ONLY as an upper bound sanity check — never for strategy evaluation.
- **With market impact**: For larger orders, apply impact model:
  ```
  impact = k * sqrt(order_size / ADV)
  ```

### Key Metrics

| Metric | Formula | Target |
|--------|---------|--------|
| Total P&L | Sum of all trade P&L minus fees | Positive |
| Sharpe Ratio | mean(returns) / std(returns) * sqrt(365) | > 3.0 |
| Sortino Ratio | mean(returns) / downside_std * sqrt(365) | > 4.0 |
| Max Drawdown | Largest peak-to-trough decline | < 5% of capital |
| Win Rate | % of profitable round trips | > 55% |
| Profit Factor | Gross profit / gross loss | > 1.5 |
| Avg Trade PnL | Average profit per round trip | > 2x fees |
| Fill Rate | Filled orders / Total orders placed | 10-30% |
| Inventory Turnover | Daily volume / Average inventory | > 10x |
| Adverse Selection | Avg price move against you in 1-5s after fill | < 0.5x spread |
| Time in Market | % of time actively quoting | > 90% |

### PnL Attribution

Decompose PnL into components to diagnose where money comes from and goes:

```
Total PnL = Spread Capture + Inventory PnL - Fees - Adverse Selection + Signal Alpha
```

- **Spread capture**: Sum of (ask_fill - bid_fill) for completed round trips
- **Inventory PnL**: Mark-to-market change of held inventory
- **Fees**: Total maker/taker fees paid
- **Adverse selection**: PnL lost to informed flow (avg unfavorable move post-fill * volume)
- **Signal alpha**: PnL attributable to OBI quote skewing

If adverse selection > spread capture → spreads too tight. If fees > spread capture → need higher VIP tier or wider spreads. If signal alpha is negative → signals are anti-predictive (invert them).

### Transaction Cost Model

Always include in backtests:
- Maker fees per VIP tier (default: 0.075% with BNB, 0.1% without)
- Taker fees for any aggressive orders (kill switch flattening, IOC)
- Slippage on aggressive orders
- **If average spread capture is within 2x of round-trip fees, the strategy is too fragile.**

### Walk-Forward Optimization

Do NOT optimize parameters on the full dataset — this guarantees overfitting. Use walk-forward:

1. **In-sample**: Optimize parameters on months 1-3
2. **Out-of-sample**: Test on month 4 (no parameter changes)
3. **Roll forward**: Optimize on months 2-4, test on month 5
4. **Aggregate**: Combine all out-of-sample results for realistic performance estimate

This gives an honest estimate of live performance. If out-of-sample Sharpe is less than half of in-sample Sharpe, you're overfitting.

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 1 | Historical data downloader (multi-symbol: aggTrades + klines + funding rates) | Downloads 30 days for AVAXUSDT, ARBUSDT, BTCUSDT (lead signal), and funding rate history |
| 2 | Multi-symbol market data replay engine | Replays data for N symbols simultaneously, merges chronologically, feeds correct symbol to correct strategy instance |
| 3 | Queue-aware simulated order matcher (default fill model) | Simulates fills with queue position tracking, assumes back-of-queue, unit tests for fill/no-fill edge cases |
| 4 | Performance metrics calculator with PnL attribution (per-strategy and portfolio) | Per-symbol metrics + portfolio-aggregated metrics + PnL decomposition per strategy type |
| 5 | Walk-forward optimization harness | Runs rolling in-sample/out-of-sample parameter optimization per symbol, reports aggregated out-of-sample performance |

---

## References

- Binance Public Data: https://data.binance.vision/
- Tardis.dev (order book replay): https://tardis.dev/
- Binance Kline API: https://binance-docs.github.io/apidocs/spot/en/#kline-candlestick-data
- Binance aggTrades API: https://binance-docs.github.io/apidocs/spot/en/#compressed-aggregate-trades-list
- Full research: `docs/RESEARCH.md` (Sections 7.1-7.5)
