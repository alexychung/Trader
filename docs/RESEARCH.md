# Quantitative Research: Crypto Multi-Strategy Trading Bot

## Table of Contents

1. [Market Microstructure Fundamentals](#1-market-microstructure-fundamentals)
2. [Core Strategy: Avellaneda-Stoikov Market Making](#2-core-strategy-avellaneda-stoikov-market-making)
3. [Supporting Strategies & Signals](#3-supporting-strategies--signals)
4. [Data Sources & Feeds](#4-data-sources--feeds)
5. [Exchange Connectivity & Execution](#5-exchange-connectivity--execution)
6. [Risk Management](#6-risk-management)
7. [Backtesting & Simulation](#7-backtesting--simulation)
8. [Infrastructure & Latency](#8-infrastructure--latency)
9. [Regulatory & Operational Considerations](#9-regulatory--operational-considerations)
10. [Key Academic Papers & References](#10-key-academic-papers--references)
11. [Multi-Strategy Architecture & Pair Selection](#11-multi-strategy-architecture--pair-selection)

---

## 1. Market Microstructure Fundamentals

### 1.1 Order Book Anatomy

A limit order book (LOB) is the central data structure. It consists of:

- **Bids**: buy orders sorted descending by price. The highest bid is the **best bid**.
- **Asks**: sell orders sorted ascending by price. The lowest ask is the **best ask**.
- **Spread**: `best_ask - best_bid`. This is the market maker's gross profit per round trip.
- **Mid price**: `(best_bid + best_ask) / 2`. Used as a reference "fair value" in many models.
- **Weighted mid price**: `(best_bid * ask_qty + best_ask * bid_qty) / (bid_qty + ask_qty)`. More informative than simple mid when book is imbalanced.
- **Microprice**: A more sophisticated weighted mid that accounts for multiple levels of the book and queue position.

### 1.2 Key Microstructure Concepts

**Adverse Selection**: When a market maker's quotes get filled by informed traders who know the price is about to move against the maker. This is the primary risk in market making. If you consistently provide liquidity when prices are about to jump, you lose money on every fill.

**Inventory Risk**: Accumulating a large directional position exposes the market maker to price movements. A market maker holding 10 BTC of inventory is effectively a directional trader until that inventory is unwound.

**Queue Priority**: On exchanges with price-time priority (like Binance), earlier orders at the same price get filled first. Getting to the front of the queue is a latency game. In crypto, queue position matters less than in traditional equities because spreads are wider relative to tick size.

**Maker vs Taker**: Makers add liquidity (limit orders resting in the book), takers remove it (market orders or crossing limit orders). Exchanges incentivize makers with lower or negative fees.

**Book Imbalance**: The ratio of bid-side volume to ask-side volume at the top of the book. Strongly predictive of short-term price direction. If bids are 3x asks at the top level, the price is more likely to tick up.

```
imbalance = bid_qty_level1 / (bid_qty_level1 + ask_qty_level1)
```

Values > 0.5 suggest upward pressure, < 0.5 suggest downward pressure. This is one of the single most predictive features for next-tick price movement in crypto.

### 1.3 Trade Flow Toxicity

**VPIN (Volume-Synchronized Probability of Informed Trading)**: Measures the fraction of volume that is likely "informed" (toxic). High VPIN means market makers are being adversely selected. Calculate by bucketing trades into volume bars and measuring buy/sell imbalance.

```
VPIN = Σ|V_buy(i) - V_sell(i)| / (n * V_bucket)
```

When VPIN spikes, widen spreads or pull quotes entirely. This was famously predictive of the 2010 Flash Crash.

**Trade classification**: Use the Lee-Ready algorithm or bulk volume classification to tag each trade as buyer-initiated or seller-initiated. In crypto, most exchanges provide the aggressor side in the trade stream, so you don't need to infer it.

---

## 2. Core Strategy: Avellaneda-Stoikov Market Making

### 2.1 The Model

The Avellaneda-Stoikov (2008) model solves for optimal bid and ask quotes given:
- Current inventory `q`
- Time remaining `T - t`
- Risk aversion parameter `γ`
- Volatility `σ`

**Reservation price** (indifference price adjusted for inventory):

```
r(s, q, t) = s - q * γ * σ² * (T - t)
```

Where:
- `s` = current mid price
- `q` = current inventory (positive = long, negative = short)
- `γ` = risk aversion coefficient (higher = more conservative)
- `σ` = asset volatility (annualized, then scaled to your time horizon)
- `T - t` = time remaining in the trading session

**Optimal spread** (distance from reservation price to each quote):

```
δ(t) = γ * σ² * (T - t) + (2/γ) * ln(1 + γ/k)
```

Where `k` is the order arrival intensity parameter from a Poisson model of fills.

**Final quotes**:
```
bid = r - δ/2
ask = r + δ/2
```

### 2.2 Parameter Estimation

**Volatility (σ)**: The most critical parameter. Options for estimation:

1. **Realized volatility**: Standard deviation of log returns over a rolling window.
   - Use 1-second or 5-second returns for HFT.
   - Typical window: 300-1000 observations.
   - Weight recent observations more heavily (exponentially weighted).

2. **Garman-Klass volatility**: Uses OHLC data, more efficient than close-to-close:
   ```
   σ²_GK = 0.5 * (H - L)² - (2*ln(2) - 1) * (C - O)²
   ```

3. **Yang-Zhang volatility**: Combines overnight, open-close, and Rogers-Satchell components. Best for crypto since markets are 24/7 (no overnight gap issue, but useful for combining multiple window scales).

4. **Parkinson volatility**: Uses high-low range:
   ```
   σ²_P = (H - L)² / (4 * ln(2))
   ```

5. **Realized variance with sub-sampling**: Use overlapping windows to reduce noise in high-frequency variance estimates.

**For this bot**: Use an exponentially weighted moving variance of 1-second mid-price log returns with a halflife of ~60 seconds. This adapts quickly to regime changes while remaining smooth enough to not whipsaw quotes.

```cpp
// Pseudocode for EWMA volatility
double alpha = 1.0 - exp(-1.0 / halflife_seconds);
ewma_var = (1 - alpha) * ewma_var + alpha * (log_return * log_return);
sigma = sqrt(ewma_var * seconds_per_year);  // annualize if needed
```

**Order arrival intensity (k)**: Estimated from historical fill rates at various distances from mid. Fit a Poisson or exponential model:

```
λ(δ) = A * exp(-k * δ)
```

Where `λ(δ)` is the fill rate when quoting at distance `δ` from mid. Estimate `A` and `k` from historical data by binning fills by their distance from mid at time of placement.

**Risk aversion (γ)**: This is tunable. Higher γ means:
- Tighter inventory control (reservation price shifts more per unit of inventory)
- Wider spreads (less aggressive quoting)
- Lower PnL but lower drawdowns

Start with `γ` such that a position of `max_inventory` shifts the reservation price by roughly half the spread. Tune via backtest.

### 2.3 Extensions to the Basic Model

**Asymmetric information**: Adjust the reservation price when you detect informed flow (e.g., large trades on one side, order book imbalance, cross-exchange signals):

```
r_adjusted = r + α * signal
```

Where `signal` is your alpha signal (discussed in Section 3) and `α` scales its impact.

**Multi-level quoting**: Instead of placing one bid and one ask, place 3-5 levels on each side with decreasing size. This provides:
- Better queue priority at the inside
- Larger capture when volatility spikes
- More fills in total (at the cost of worse average fill price)

Size distribution: Use geometric decay — e.g., 40% of size at level 1, 25% at level 2, 20% at level 3, 15% at level 4.

**Discrete tick adaptation**: The continuous model produces prices between ticks. Round quotes to the nearest valid tick, but always ensure bid < ask. When the optimal spread is less than 1 tick, skip quoting (the edge is negative after fees).

**Inventory penalty amplification near limits**: As inventory approaches your hard limit, exponentially increase the effective `γ`:

```
γ_effective = γ * exp(β * (|q| / q_max)²)
```

This creates increasingly aggressive inventory reduction as you approach limits without needing a hard cutoff until `q_max`.

### 2.4 Time Horizon (T - t)

In crypto (24/7 markets), there's no natural session end. Options:

1. **Rolling window**: Set `T - t` to a constant (e.g., 300 seconds). The model then behaves as if it always has 5 minutes "remaining" — inventory urgency stays constant.
2. **Funding period**: If trading perpetual futures, use time until next funding payment as `T`. Inventory has a natural cost at funding time.
3. **Adaptive**: Scale `T - t` inversely with volatility regime. In calm markets, use longer horizons (more patient). In volatile markets, shorten (more urgent inventory control).

**Recommendation**: Use a rolling window of 300-600 seconds for spot market making. This keeps the model well-behaved without arbitrary session boundaries.

---

## 3. Supporting Strategies & Signals

### 3.1 Short-Term Alpha Signals

These signals predict the next 1-100 ticks of price movement. They are used to skew the market maker's quotes (shift reservation price) to lean into expected moves.

**Order Book Imbalance (OBI)**: The strongest single feature for short-term prediction in crypto.

```
OBI = (bid_qty - ask_qty) / (bid_qty + ask_qty)   // range [-1, 1]
```

Use top 1-5 levels of the book. Deeper levels add information but with diminishing returns. In backtests on Binance BTC/USDT, top-of-book OBI has a Pearson correlation of ~0.15-0.25 with next-second mid price change. This is extremely significant at HFT timescales.

**Trade flow imbalance (TFI)**: Net signed volume over a rolling window.

```
TFI = Σ(signed_volume) over last N trades
```

Where `signed_volume` = `+volume` if buyer-initiated, `-volume` if seller-initiated. Normalize by average volume to get a z-score.

**Weighted trade flow**: Weight recent trades more heavily and/or weight by size:

```
TFI_weighted = Σ(sign_i * volume_i * decay(t_now - t_i))
```

Using exponential decay with halflife of 5-30 seconds.

**Price acceleration**: Second derivative of the microprice. If price is not just moving up but accelerating upward, the signal is stronger:

```
accel = Δprice(t) - Δprice(t - lag)
```

**Cross-asset signals**: Price movements in correlated assets can predict your target. For crypto:
- BTC leads most altcoins by 50-500ms
- ETH leads most DeFi tokens
- Binance Futures lead Binance Spot by ~100-300ms
- Larger exchanges (Binance) lead smaller exchanges

If you're making a market on ETH/USDT spot, a sudden move in BTC/USDT futures is a strong signal to adjust quotes.

**Volatility regime detection**: Use the ratio of short-term to long-term volatility:

```
vol_ratio = σ_short / σ_long
```

If `vol_ratio > 1.5`, you're entering a high-vol regime — widen spreads. If `vol_ratio < 0.5`, low-vol regime — tighten spreads for more fills.

### 3.2 Feature Combination

Combine signals using a simple linear model for speed:

```
alpha = w1 * OBI + w2 * TFI + w3 * cross_asset + w4 * vol_signal
reservation_price = mid - q * γ * σ² * (T-t) + alpha
```

Estimate weights `w1...w4` via rolling linear regression of features against future mid-price changes at your target horizon (e.g., 1-second ahead). Retrain daily or weekly. Keep the model simple — in HFT, model latency matters, and complex models overfit to noise.

### 3.3 Statistical Arbitrage / Pairs Component

If you're market making on multiple pairs, you can exploit mean-reversion between correlated assets:

1. Compute rolling correlation and cointegration between pairs (e.g., ETH/BTC spread)
2. When the spread deviates beyond 2σ, skew your market making to lean into the reversion
3. This adds a secondary profit source beyond spread capture

**Cointegration test**: Use Engle-Granger or Johansen. For crypto, rolling 24-hour windows work well. The relationship between BTC and ETH is generally cointegrated, but the beta drifts, so re-estimate frequently.

### 3.4 Funding Rate Arbitrage (Perpetual Futures)

Perpetual futures have a funding rate mechanism — longs pay shorts (or vice versa) every 8 hours on Binance. When funding is extremely positive (longs paying shorts), you can:

1. Go short perp + long spot (delta neutral)
2. Collect funding payments
3. Close when funding normalizes

This isn't pure market making, but it's a complementary strategy that provides steady returns and can be overlaid on the market making inventory.

---

## 4. Data Sources & Feeds

### 4.1 Real-Time Market Data

**Binance WebSocket Streams** (primary):
- `<symbol>@depth@100ms` — Order book diff updates every 100ms. Must maintain local book state.
- `<symbol>@depth20@100ms` — Top 20 levels snapshot every 100ms. Simpler but less granular.
- `<symbol>@trade` — Individual trades in real-time. Essential for trade flow analysis.
- `<symbol>@aggTrade` — Aggregated trades (same price, same side, within 100ms). Lower overhead.
- `<symbol>@kline_1s` — 1-second candles. Useful for quick OHLC volatility.
- `<symbol>@bookTicker` — Best bid/ask updates in real-time. Lowest latency for spread monitoring.
- `<symbol>@ticker` — 24h rolling statistics.

**Binance REST API** (supplemental):
- `GET /api/v3/depth` — Order book snapshot (up to 5000 levels). Use for initial book state on startup.
- `GET /api/v3/trades` — Recent trades.
- `GET /api/v3/klines` — Historical candles.
- `GET /api/v3/ticker/24hr` — 24h statistics.
- `GET /api/v3/exchangeInfo` — Symbol info, tick sizes, lot sizes, filters.

**Connection management**:
- Binance allows 5 WebSocket connections per IP, each with up to 1024 streams via combined streams
- Use `wss://stream.binance.com:9443/stream?streams=<stream1>/<stream2>/...` for combined streams
- Testnet: `wss://testnet.binance.vision/ws` and `https://testnet.binance.vision`
- Implement reconnection logic with exponential backoff
- Detect stale data: if no update received in 5 seconds, force reconnect

### 4.2 Historical Data for Backtesting

**Free sources**:

1. **Binance Public Data**: https://data.binance.vision/
   - Tick-level trade data (aggTrades), klines (1s to 1M), order book snapshots
   - Available as CSV/ZIP, updated daily
   - Coverage: All Binance pairs, going back to listing date
   - **This is your primary historical data source.** It's free, comprehensive, and directly from the exchange you'll trade on.

2. **Binance API Historical Klines**:
   - `GET /api/v3/klines` with `startTime`/`endTime` — up to 1000 candles per request
   - Rate limited, but sufficient for bootstrapping

3. **CryptoDataDownload**: https://www.cryptodatadownload.com/
   - Aggregated OHLCV data from multiple exchanges
   - Free tier available, good for cross-exchange analysis

4. **Kaiko** (paid, institutional grade): https://www.kaiko.com/
   - Full order book snapshots (L2/L3), tick trades, OHLCV
   - Coverage across 100+ exchanges
   - Expensive but gold-standard for professional backtesting

5. **Tardis.dev**: https://tardis.dev/
   - Recorded raw WebSocket messages from major exchanges
   - Allows replaying the exact data feed you'd see in production
   - Has a free tier with limited history; paid plans for full archive
   - **Highly recommended for realistic backtesting** — you can replay the exact order book state

6. **CoinGecko API**: https://www.coingecko.com/en/api
   - Free tier: OHLCV, market cap, volume, price data
   - Useful for fundamental screening (which pairs to trade)
   - Not granular enough for HFT backtesting

7. **CoinMarketCap API**: https://coinmarketcap.com/api/
   - Similar to CoinGecko, good for fundamentals
   - Free tier available

**What to collect for backtesting**:
- **Essential**: aggTrades (timestamp, price, quantity, is_buyer_maker) at the highest resolution available
- **Ideal**: Full L2 order book snapshots at 100ms intervals + trade stream. This allows realistic simulation of queue priority and fill probability.
- **Minimum viable**: 1-second OHLCV candles + trade count + volume

### 4.3 Alternative Data

**On-chain data** (supplemental signals):
- **Mempool monitoring**: Large pending transactions can signal imminent large market orders
- **Exchange inflows/outflows**: Track BTC/ETH flowing into exchanges (bearish signal) or out (bullish)
- **Whale wallet tracking**: Via Etherscan API, Whale Alert
- **DeFi TVL changes**: Via DefiLlama API (https://defillama.com/docs/api)
- **Stablecoin supply**: USDT/USDC minting events often precede buying pressure

**Sentiment data**:
- **Crypto Fear & Greed Index**: https://alternative.me/crypto/fear-and-greed-index/
- **Social volume**: LunarCrush API (https://lunarcrush.com/developers/api) for Twitter/Reddit mention velocity
- **Funding rates**: Track across exchanges for market sentiment. Extreme funding = crowded trade.

**These alternative data sources are lower priority for an HFT market maker** but valuable for regime detection (when to widen spreads, when to reduce exposure).

### 4.4 Reference Data

**Exchange info you must cache and refresh**:
- Tick size (price precision) per symbol — `filters[].PRICE_FILTER.tickSize`
- Lot size (quantity precision) — `filters[].LOT_SIZE.stepSize`
- Min notional — `filters[].NOTIONAL.minNotional`
- Rate limits — `X-MBX-USED-WEIGHT` headers
- Trading status per symbol
- Fee schedule (maker/taker rates for your VIP level)

**Current Binance fee schedule** (as of early 2025, verify current):
- Regular: 0.1% maker / 0.1% taker
- Using BNB for fees: 0.075% / 0.075%
- VIP 1 (>= 1M 30d volume): 0.09% / 0.1%
- VIP tiers reduce fees further; HFT bots aim for VIP 3+ (0.06% maker)
- Some BTC pairs have zero maker fees periodically

**Fee impact on strategy**: At 0.075% maker + 0.075% taker round trip = 0.15%, you need a spread capture of at least 15bps to break even. On BTC/USDT with typical spreads of 1-5bps, this means **you must be a maker on both sides** (0.075% + 0.075% = 0.15% round trip as maker on both legs). If you're taking on either side, the economics break down quickly. This is why maker rebate and fee tier are critical to profitability.

---

## 5. Exchange Connectivity & Execution

### 5.1 Order Types for Market Making

**Limit orders** (primary): Place orders at specific prices in the book.
- `GTC` (Good Till Cancelled): Default. Stays until filled or cancelled.
- `IOC` (Immediate or Cancel): Fill what you can immediately, cancel the rest. Useful for aggressive inventory reduction.
- `FOK` (Fill or Kill): Fill the entire order or cancel completely. Rarely used in MM.
- `POST_ONLY` (Maker Only): **Critical for market making.** The order is guaranteed to be a maker order (adding liquidity). If it would immediately match, it's cancelled instead. This guarantees maker fees and prevents accidental taking.

**Always use `POST_ONLY`** for market making quotes. If the market moves and your "bid" would cross the current ask, you don't want to accidentally take liquidity at taker rates.

### 5.2 Order Management

**Cancel-replace workflow**:
1. When the model generates new optimal quotes, cancel existing orders and place new ones.
2. Binance supports `DELETE /api/v3/order` for individual cancels and `DELETE /api/v3/openOrders` for bulk cancel.
3. Binance also supports `cancelReplace` endpoint — atomic cancel + new order in one API call. **Use this to minimize the time you're unquoted.**

**Rate limits**:
- Binance: 10 orders/second/symbol for order placement, 100,000 orders/day.
- Use WebSocket API (`wss://ws-api.binance.com:443/ws-api/v3`) for lower-latency order management vs REST.
- The WebSocket order API avoids HTTP overhead and is ~50-100ms faster per round trip.

**Order tracking**:
- Use `newClientOrderId` to tag your orders with a unique ID that encodes strategy, timestamp, and side.
- Subscribe to `userData` WebSocket stream for real-time fill notifications (executionReport events).
- Reconcile fills against expected state — if an order is filled that you thought you cancelled, handle the unexpected inventory.

### 5.3 Execution Quality Metrics

Track these to measure and improve execution:

- **Fill rate**: % of quotes that get filled. Too high = your spreads are too tight (adverse selection). Too low = too wide (leaving money on the table). Target: 10-30% of quote updates result in a fill.
- **Effective spread**: The actual spread realized per round trip (buy fill to sell fill).
- **Adverse selection cost**: Average price move against you in the 1-5 seconds after a fill. This is the key metric — if this exceeds your spread, you're losing money.
- **Time to fill**: How long quotes rest before being filled. Shorter = more likely informed flow.
- **Inventory turnover**: How many times per hour your inventory cycles from flat to max and back. Higher turnover = more PnL opportunities.

### 5.4 Smart Order Routing

If you expand beyond Binance to multiple exchanges:

- Compare effective prices (price + fees) across venues
- Route maker orders to the venue with the best maker rebate
- Route taker orders to the venue with the best price after fees
- Consider latency differences — a better price on a slow exchange may be stale

For now (Binance only), this isn't needed, but the architecture should accommodate it.

---

## 6. Risk Management

### 6.1 Position Limits

**Hard limits** (never exceed, enforced at the order placement layer):
- `max_position`: Maximum absolute inventory in base asset (e.g., 1.0 BTC)
- `max_notional`: Maximum dollar value of position (e.g., $50,000)
- `max_order_size`: Maximum single order size (prevent fat finger)
- `max_open_orders`: Maximum concurrent open orders (prevent runaway order placement)

**Soft limits** (trigger spread widening and inventory reduction):
- Start widening spreads at 50% of `max_position`
- Start aggressively skewing at 75%
- At 90%, only place orders that reduce inventory (one-sided quoting)

### 6.2 PnL-Based Controls

- **Max daily loss**: Stop trading for the day if PnL drops below threshold (e.g., -$500). In crypto with 24/7 markets, use a rolling 24h window.
- **Max drawdown from peak**: If PnL drops X% from its peak in the current session, reduce position sizes by 50% and widen spreads by 2x.
- **Trailing stop on session PnL**: Lock in profits. If you're up $1000 and it drops to $800, tighten risk.

### 6.3 Kill Switch

**The kill switch is the most important safety feature.** It must:

1. **Cancel all open orders** immediately (use bulk cancel endpoint)
2. **Flatten all positions** using aggressive IOC orders (accept slippage)
3. **Prevent any new order placement** until manually re-enabled
4. **Log the trigger reason** for post-mortem analysis

**Triggers for automatic kill switch activation**:
- Daily loss limit breached
- Position limit breached (should never happen if hard limits work, but defense in depth)
- Data feed stale for > 5 seconds (you're quoting on stale prices = guaranteed adverse selection)
- Exchange connectivity loss
- Latency spike above threshold (e.g., order round trip > 2 seconds)
- Abnormal fill rate (e.g., 10x normal fills in 1 second = likely a flash crash or fat finger)
- VPIN spike above critical threshold
- Process crash of any critical component (watchdog detection)

**Implementation**: The kill switch must be on a separate code path with minimal dependencies. It should work even if the main strategy loop is hung. Consider a separate watchdog process.

### 6.4 Volatility-Based Risk Scaling

Scale all position limits and order sizes with inverse volatility:

```
effective_max_position = base_max_position * (target_vol / current_vol)
```

In a volatility spike (e.g., after a major news event), this automatically reduces your exposure. In calm markets, it allows larger positions for the same dollar risk.

### 6.5 Correlation Risk

If making markets on multiple pairs, monitor portfolio-level risk:
- BTC and ETH are ~0.8 correlated — being long both is not diversified
- Track portfolio beta to BTC and net dollar exposure
- Apply portfolio-level position limits, not just per-symbol

### 6.6 Exchange Risk

- **Counterparty risk**: Don't keep more capital on-exchange than needed for margin + daily PnL swing
- **API risk**: Exchanges change APIs, sometimes without notice. Version-pin and monitor changelog
- **Withdrawal risk**: Have automated withdrawal of profits above a threshold to a cold wallet

---

## 7. Backtesting & Simulation

### 7.1 Backtesting Architecture

**Event-driven replay engine**:
1. Read historical market data (trades + order book snapshots) chronologically
2. Feed events into the same strategy code that runs in production
3. Simulate order matching with realistic assumptions
4. Record all trades, PnL, and metrics

**Critical: Avoid lookahead bias.** The strategy must only see data up to the current event timestamp. Common mistakes:
- Using close price to make decisions that happened during the bar
- Using future book state for fill simulation
- Not accounting for your own market impact

### 7.2 Fill Simulation

This is the hardest part of backtesting a market maker. Your limit orders don't fill just because the price touches them.

**Conservative fill model** (recommended):
- A limit buy at price P fills only when a trade occurs at price P AND the trade is seller-initiated (aggressor selling into the book)
- Even then, your order only fills if total volume at that price exceeds the volume ahead of you in the queue
- Assume you are at the **back** of the queue (conservative). You fill only after all pre-existing volume at that price is consumed.

**Queue position estimation**:
```
queue_ahead = book_qty_at_price_when_order_placed
fill_probability = max(0, (trade_volume_at_price - queue_ahead)) / order_size
```

**Market impact**: For larger order sizes, assume you'll move the price by:
```
impact = k * sqrt(order_size / ADV)
```
Where `ADV` is average daily volume and `k` is calibrated from historical data (typically 0.1-0.5 for crypto).

### 7.3 Key Backtest Metrics

| Metric | What it tells you | Target |
|--------|-------------------|--------|
| Total PnL | Gross profitability | Positive |
| Sharpe Ratio | Risk-adjusted return (annualized) | > 3.0 for HFT |
| Sortino Ratio | Downside risk-adjusted return | > 4.0 |
| Max Drawdown | Worst peak-to-trough | < 5% of capital |
| Win Rate | % of round trips profitable | > 55% |
| Profit Factor | Gross profit / gross loss | > 1.5 |
| Avg Trade PnL | Average profit per round trip | > 2x fees |
| Fill Rate | % of quotes that get filled | 10-30% |
| Inventory Turnover | Daily volume / avg position | > 10x |
| Adverse Selection | Avg price move against you post-fill | < 0.5x spread |
| Time in Market | % of time actively quoting | > 90% |

### 7.4 Walk-Forward Optimization

Don't optimize parameters on the full dataset. Use walk-forward:

1. **In-sample**: Optimize parameters on months 1-3
2. **Out-of-sample**: Test on month 4
3. **Roll forward**: Optimize on months 2-4, test on month 5
4. **Aggregate**: Combine all out-of-sample results for realistic performance

This prevents overfitting and gives a realistic estimate of live performance.

### 7.5 Transaction Cost Analysis

Always include in backtests:
- Maker fees (per your VIP tier)
- Taker fees (for any aggressive orders)
- Slippage on aggressive orders (especially in thin books)
- BNB cost if using BNB for fee discounts
- Funding payments if trading perpetuals

A strategy that looks great before costs often becomes marginal or negative after costs. **If your average spread capture is within 2x of round-trip fees, the strategy is too fragile.**

---

## 8. Infrastructure & Latency

### 8.1 Latency Budget

For crypto HFT, the latency stack (in a co-located setup):

| Component | Target | Notes |
|-----------|--------|-------|
| Market data parsing | < 1 μs | Use zero-copy parsing, pre-allocated buffers |
| Strategy computation | < 5 μs | Simple math, no allocations |
| Order serialization | < 1 μs | Pre-format JSON templates |
| Network to exchange | 1-10 ms | Depends on colocation proximity |
| Exchange matching | 1-5 ms | Out of your control |
| **Total round trip** | **5-20 ms** | From market data to order ack |

Crypto latency is orders of magnitude slower than equities (where total latency is <10μs). This means:
- Algorithmic edge matters more than pure speed
- Statistical signals (book imbalance, flow) are more valuable than raw latency
- You're competing with other crypto bots at 5-50ms, not nanoseconds

### 8.2 Critical Implementation Details for C++

**Memory management on hot path**:
- Pre-allocate all buffers at startup
- Use memory pools for order objects (boost::pool or custom)
- Zero heap allocations in the market data → strategy → order path
- Use `std::string_view` and in-place parsing for JSON (or switch to FlatBuffers/SBE for internal messages)

**JSON parsing**: Binance sends JSON. nlohmann/json is convenient but not the fastest.
- For production hot path, consider `simdjson` (fastest JSON parser, uses SIMD instructions)
- Parse only the fields you need, skip the rest
- Pre-compute string hashes for field lookup

**Lock-free communication**:
- Use SPSC (single-producer, single-consumer) ring buffers between threads
- Market data thread → Strategy thread → Order management thread
- Boost.Lockfree or custom implementation
- Cache-line align all shared data structures to avoid false sharing

**Thread architecture**:
```
Thread 1: WebSocket I/O (market data receive, order responses)
Thread 2: Market data processing (book maintenance, feature computation)
Thread 3: Strategy (Avellaneda-Stoikov model, signal combination)
Thread 4: Order management (order state machine, rate limiting)
Thread 5: Risk monitoring (PnL tracking, kill switch)
Thread 6: Logging (async, never blocks hot path)
```

Pin threads to specific CPU cores. Isolate cores from the OS scheduler if on Linux (`isolcpus`).

### 8.3 Colocation

**Binance matching engine locations** (as of 2025):
- Primary: AWS Tokyo (ap-northeast-1)
- Also reported: AWS Singapore

**Colocation options**:
- AWS EC2 in the same region: c5n.large or c5.xlarge instances ($50-150/month)
- This gets you ~1-3ms network latency to Binance
- For development/testnet: any location is fine

**DNS optimization**: Use Binance's API endpoints with lowest latency from your location. They have multiple endpoints — benchmark them.

### 8.4 Monitoring & Observability

**Real-time dashboard** (minimum):
- Current PnL (unrealized + realized)
- Current inventory and direction
- Current spread being quoted
- Current volatility estimate
- Fill rate (rolling 5-minute)
- Order-to-fill latency (rolling)
- Data feed latency (exchange timestamp vs local timestamp)

**Alerting**:
- PnL approaching daily loss limit
- Inventory approaching hard limit
- Data feed latency spike
- Fill rate anomaly (too high or too low)
- Error rate spike in order placement

Use `spdlog` for structured logging. Log every order placement, cancel, fill, and risk event. Store in a format that allows post-trade analysis (CSV or binary with timestamps).

---

## 9. Regulatory & Operational Considerations

### 9.1 Exchange Rules

**Binance-specific rules to follow**:
- **Self-trade prevention**: Don't let your own buy and sell orders match each other. Use Binance's `selfTradePreventionMode` parameter (EXPIRE_MAKER or EXPIRE_BOTH).
- **Order rate limits**: Stay well within limits. Getting rate-limited means you can't cancel orders, which is dangerous.
- **API key permissions**: Use separate API keys for trading vs. reading. Trading keys should have IP whitelist.
- **Testnet first**: Always validate on testnet before mainnet. Testnet has the same API but no real money.

### 9.2 Tax & Accounting

- Every fill is a taxable event in most jurisdictions (US, UK, EU)
- Log every trade with: timestamp, pair, side, price, quantity, fee, fee currency
- Export capability to CSV for tax software (Koinly, CoinTracker, etc.)
- Consider a separate accounting module that tracks cost basis (FIFO, LIFO, or specific ID)

### 9.3 Operational Security

- API keys: Store in encrypted config file or environment variables, never in code
- Withdrawal: Disable withdrawal permission on trading API keys
- IP whitelist: Restrict API keys to your server's IP
- 2FA: Enable on exchange account
- Monitoring: Alert on any unexpected account activity
- Key rotation: Rotate API keys periodically (monthly)

---

## 10. Key Academic Papers & References

### 10.1 Core Market Making Theory

1. **Avellaneda, M. & Stoikov, S. (2008)**. "High-frequency trading in a limit order book." *Quantitative Finance, 8(3), 217-224.*
   - The foundational paper for our strategy. Derives optimal bid/ask quotes for a market maker with inventory risk.

2. **Gueant, O., Lehalle, C.A., & Fernandez-Tapia, J. (2012)**. "Dealing with the inventory risk: a solution to the market making problem." *Mathematics and Financial Economics, 7(4), 477-507.*
   - Extends Avellaneda-Stoikov with more practical considerations and closed-form solutions.

3. **Cartea, A., Jaimungal, S., & Penalva, J. (2015)**. *Algorithmic and High-Frequency Trading.* Cambridge University Press.
   - The comprehensive textbook. Covers market making, optimal execution, statistical arbitrage. Chapters 10-11 are directly relevant.

4. **Fodra, P. & Labadie, M. (2012)**. "High-frequency market-making with inventory constraints and directional bets."
   - Incorporates short-term price prediction into the Avellaneda-Stoikov framework.

### 10.2 Order Book Dynamics & Microstructure

5. **Cont, R., Stoikov, S., & Talreja, R. (2010)**. "A stochastic model for order book dynamics." *Operations Research, 58(3), 549-563.*
   - Models the order book as a queuing system. Useful for understanding fill probability.

6. **Cont, R., Kukanov, A., & Stoikov, S. (2014)**. "The price impact of order book events." *Journal of Financial Econometrics, 12(1), 47-88.*
   - Quantifies how order book events (trades, cancellations, new orders) move prices.

7. **Huang, W., Lehalle, C.A., & Rosenbaum, M. (2015)**. "Simulating and analyzing order book data: The queue-reactive model." *Journal of the American Statistical Association, 110(509), 107-122.*
   - Advanced order book simulation model for realistic backtesting.

### 10.3 Crypto-Specific Research

8. **Makarov, I. & Schoar, A. (2020)**. "Trading and arbitrage in cryptocurrency markets." *Journal of Financial Economics, 135(2), 293-319.*
   - Documents arbitrage opportunities across crypto exchanges. Relevant for cross-exchange signals.

9. **Hautsch, N. & Podolskij, M. (2024)**. "Market making in crypto: A comprehensive overview."
   - Survey of crypto market making specifics vs. traditional markets.

### 10.4 Practical Implementation Resources

10. **Hummingbot documentation**: https://hummingbot.org/
    - Open-source crypto market making bot. Not HFT-grade, but excellent documentation of strategies and their implementation.

11. **QuantConnect/Lean**: https://www.quantconnect.com/
    - Open-source algorithmic trading engine with crypto support. Good reference architecture.

12. **Binance API Documentation**: https://binance-docs.github.io/apidocs/
    - The definitive reference for all API endpoints, WebSocket streams, and rate limits.

13. **Crypto-focused quant blogs**:
    - https://blog.hummingbot.org/ — Market making strategy analysis
    - https://www.paradigm.xyz/writing — DeFi and crypto market structure research
    - https://atiselsts.github.io/pdfs/uniswap-v3-liquidity-math.pdf — AMM math (relevant if expanding to DEX MM)

---

## 11. Multi-Strategy Architecture & Pair Selection

### 11.1 Why Multi-Strategy

Running a single strategy on BTC/USDT is a red ocean — you're competing against co-located institutional market makers with sub-millisecond latency and millions in infrastructure. The profitable retail approach is to **stack uncorrelated edges**:

1. **Altcoin market making** — wider spreads, less competition, your A-S model has real edge
2. **Funding rate arbitrage** — delta-neutral carry, predictable income, runs alongside
3. **Cross-pair alpha signals** — BTC leads altcoins by 100-500ms, use as directional skew

These three strategies share 90% of infrastructure but produce uncorrelated returns.

### 11.2 Pair Selection: Altcoins Over BTC

**BTC/USDT is the wrong target for retail market making:**
- Spreads are 1-3 bps — your round-trip cost (maker+maker) is ~15 bps with BNB discount
- You can only profit as maker on both sides, and you're at the back of the queue behind firms in AWS Tokyo
- Adverse selection is extreme — informed flow is densest on BTC

**Mid-cap altcoins are where retail market makers actually profit:**

| Pair | Typical Spread | Competition | Why It Works |
|------|---------------|-------------|--------------|
| AVAX/USDT | 5-20 bps | Medium | Liquid enough for fills, wide enough for edge |
| ARB/USDT | 10-30 bps | Low-medium | Newer token, less mature MM competition |
| NEAR/USDT | 10-25 bps | Low | Good volume, wide spreads |
| INJ/USDT | 15-40 bps | Low | Lower liquidity = wider spreads = more edge per trade |
| SUI/USDT | 10-30 bps | Low | Growing ecosystem, increasing volume |

**Pair selection criteria:**
- 24h volume > $10M (enough to get fills)
- 24h volume < $500M (not dominated by institutional MMs)
- Typical spread > 10 bps (positive edge after fees)
- Available on Binance spot with reasonable tick/lot sizes
- Not in active delisting review

**Start with 3-5 pairs.** Run each as an independent IStrategy instance sharing the same risk manager.

### 11.3 Funding Rate Arbitrage (Strategy #2)

Perpetual futures on Binance have a **funding rate** mechanism: every 8 hours, longs pay shorts (or vice versa). When funding is positive (longs pay shorts), you can:

1. **Buy spot** (e.g., buy 1 AVAX on spot)
2. **Short perpetual** (short 1 AVAX-PERP, same size)
3. **Net position is zero** — you're delta-neutral
4. **Collect funding payments** every 8 hours

**The math:**
```
Funding rate: 0.03% every 8 hours (common during bull markets)
Annualized: 0.03% * 3 * 365 = 32.85% APR
On $10,000 notional: ~$9/day
```

During extreme sentiment, funding can spike to 0.1-0.5% per 8h (annualized 100%+). This is free money as long as you can hold the position through funding snapshots.

**Risks:**
- Funding can flip negative (you pay instead of receive) — must exit quickly
- Basis risk: spot and perp prices can temporarily diverge
- Margin requirements on perp side
- Exchange risk on the perp position

**Implementation:**
- Monitor funding rates across top 20 pairs via `GET /fapi/v1/premiumIndex`
- Enter when predicted funding > threshold (e.g., > 0.02% per period)
- Exit when predicted funding drops below cost threshold
- This runs as a `FundingRateStrategy` implementing IStrategy
- Requires both spot and futures API connections

### 11.4 Cross-Pair Signals (Alpha Overlay)

BTC price movements lead altcoin movements by 100-500ms. This is your strongest alpha signal for altcoin market making:

```
BTC moves down 0.1% at timestamp T
→ AVAX is likely to move down within 100-500ms
→ Lower your AVAX ask price (more aggressive selling)
→ Raise your AVAX bid price (less aggressive buying)
→ Avoid buying AVAX just before it drops
```

**Implementation:**
- Subscribe to BTC/USDT bookTicker stream (lowest latency)
- Compute BTC return over rolling 1-5 second windows
- Feed as alpha signal into altcoin market making strategy:
  ```
  r_adjusted = r + btc_weight * btc_signal * σ_altcoin
  ```
- `btc_weight` is pair-specific (calibrate from historical correlation)
- Typical BTC→altcoin beta: 0.5-1.5 depending on the altcoin

### 11.5 Portfolio-Level Risk

With multiple strategies and pairs, risk management must operate at the **portfolio level**, not just per-symbol:

- **Correlated exposure**: AVAX and ARB are both ~0.7 correlated with BTC — being long both is not diversified
- **Net dollar exposure**: Sum of all position values across all strategies
- **Portfolio max drawdown**: Applied to total equity, not per-strategy
- **Margin utilization**: Futures positions consume margin — track total usage vs. available

```
portfolio_exposure = Σ |position_usd(i)| for all symbols i
correlation_adjusted_risk = portfolio_exposure * avg_correlation_factor
```

### 11.6 Calibrating `k` (Order Arrival Intensity)

The order arrival intensity `k` directly controls spread width and is one of the hardest parameters to estimate. Here's the concrete procedure:

**Step 1: Collect data**
- Run the bot in observation mode (no trading) for 24-48 hours
- For each symbol, record: timestamp, best bid, best ask, mid price, every trade (price, qty, aggressor side)

**Step 2: Bucket fills by distance**
- For each hypothetical limit order at distance `δ` from mid (in bps):
  - Count how many trades occurred at or beyond that level
  - This gives you empirical fill rate `λ(δ)` at each distance

**Step 3: Fit the exponential model**
```
λ(δ) = A * exp(-k * δ)
```
- Use least-squares regression on log(λ) vs δ to get k and A
- `k` is typically 0.5-5.0 for crypto, depending on the pair
- Tighter pairs (BTC) have higher k (fills drop off faster with distance)
- Wider pairs (altcoins) have lower k (fills are more uniformly distributed)

**Step 4: Recalibrate**
- Recalculate weekly or when market regime changes
- Store per-symbol k values in config

### 11.7 Common Failure Modes

**Inventory accumulation death spiral (trending market):**
- Market trends strongly in one direction for hours
- Buy orders fill but sell orders don't (or vice versa)
- Inventory grows, skewing creates worse fills, inventory grows more
- **Detection**: Inventory at >80% of max for >5 minutes
- **Response**: Reduce size by 75%, widen spreads 3x, or pull quotes entirely until trend exhausts

**Fee drag (low volume periods):**
- Strategy is quoting but not getting enough fills to cover fixed costs (API fees, compute)
- Average spread capture within 1.5x of round-trip fees
- **Detection**: Rolling 1-hour P&L negative for 3+ consecutive hours
- **Response**: Reduce to 1 pair (most profitable), widen spreads, or pause until volume returns

**Adverse selection during news events:**
- CPI, FOMC, or major crypto events cause informed flow spikes
- Fill rate jumps but P&L per fill goes deeply negative
- **Detection**: Fill rate > 3x normal AND avg post-fill adverse move > 1x spread
- **Response**: Pull quotes 5 minutes before known events (use an event calendar), widen spreads 5x on unexpected volume spikes

**When to NOT trade:**
- Scheduled exchange maintenance (Binance announces these)
- First/last 5 minutes around funding snapshots (8h intervals) — price manipulation is common
- When your WebSocket reconnects (stale book state — wait for fresh snapshot)
- After kill switch fires: minimum 15 minute cooldown before manual restart

---

## Appendix A: Quick-Start Parameter Recommendations

For initial deployment on Binance altcoin pairs (testnet first):

```yaml
# Multi-strategy configuration
strategies:
  - name: "market_making"
    symbol: "AVAXUSDT"
    gamma: 0.01
    sigma_halflife: 60
    time_horizon: 300
    num_levels: 3
    level_spacing_bps: 2.0
    size_per_level: [5.0, 3.5, 2.5]     # AVAX units per level
    min_spread_bps: 20                    # Floor: breakeven + safety
    obi_weight: 0.3
    obi_levels: 5
    requote_interval_ms: 200
    use_post_only: true
    btc_lead_weight: 0.2                  # Cross-pair alpha signal

  - name: "market_making"
    symbol: "ARBUSDT"
    gamma: 0.015                          # More conservative for less liquid pair
    sigma_halflife: 60
    time_horizon: 300
    num_levels: 2
    level_spacing_bps: 3.0
    size_per_level: [50.0, 30.0]          # ARB units per level
    min_spread_bps: 25
    obi_weight: 0.25
    obi_levels: 3
    requote_interval_ms: 300
    use_post_only: true
    btc_lead_weight: 0.25

  - name: "funding_rate"
    pairs: ["AVAXUSDT", "ARBUSDT", "NEARUSDT"]
    min_funding_rate: 0.0002              # 0.02% per period minimum to enter
    exit_funding_rate: 0.0001             # Exit when funding drops below this
    max_notional_per_pair: 2000           # $2000 max per pair
    rebalance_interval_s: 300

# Shared risk parameters
risk:
  max_position_usd: 500                  # Per symbol
  max_portfolio_exposure_usd: 3000       # Total across all strategies
  max_daily_loss_usd: 100
  max_drawdown_pct: 5.0
  kill_switch_loss_usd: 200
  stale_data_timeout_ms: 5000
  max_order_latency_ms: 2000

# BTC lead signal (always subscribed, not traded)
cross_pair_signals:
  btc_stream: "BTCUSDT"
  lead_window_ms: 500
  min_move_bps: 5                        # Ignore BTC moves < 5 bps

# Data
data:
  book_depth_levels: 20
  use_trade_stream: true
  volatility_window: 300
```

## Appendix B: PnL Attribution Framework

Decompose daily PnL into:

1. **Spread capture**: Sum of (ask_fill - bid_fill) for completed round trips
2. **Inventory PnL**: Mark-to-market change of held inventory
3. **Fees paid**: Total maker/taker fees
4. **Adverse selection cost**: PnL lost to informed flow (measure as avg unfavorable move post-fill * volume)
5. **Signal alpha**: PnL attributable to quote skewing from alpha signals

```
Total PnL = Spread Capture + Inventory PnL - Fees - Adverse Selection + Signal Alpha
```

This decomposition tells you exactly where your money is coming from and going. If adverse selection > spread capture, your spreads are too tight. If fees > spread capture, you need a higher VIP tier or wider spreads. If signal alpha is negative, your signals are anti-predictive (invert them).

## Appendix C: Regime Detection Cheat Sheet

| Regime | Volatility | Spread | Volume | Action |
|--------|-----------|--------|--------|--------|
| Calm trending | Low | Tight | Normal | Normal quoting, slight trend skew |
| Calm ranging | Low | Tight | Low | Tightest spreads, max fill rate |
| Volatile trending | High | Wide | High | Wide spreads, small size, strong skew |
| Volatile choppy | High | Wide | High | Widest spreads, smallest size |
| Flash crash | Extreme | N/A | Extreme | **Kill switch.** Pull all quotes. |
| Low liquidity | Any | Very wide | Very low | Wide spreads, very small size, or don't quote |

Detect regime using:
- Short-term vs long-term vol ratio
- ADV (average daily volume) vs current volume
- Spread percentile vs historical
- VPIN level
