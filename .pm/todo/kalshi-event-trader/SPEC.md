# Kalshi Event Trading Bot — Phase 1 Specification

**Status**: Not Started
**Sprint**: `kalshi-event-trader`
**Timeline**: 6-8 weeks to live trading
**Capital**: $100 starting
**Primary research**: `docs/KALSHI_RESEARCH.md`

---

## Overview

Automated trading system targeting Kalshi's CFTC-regulated prediction market exchange. Uses calibrated ensemble probabilistic forecasts (weather) and economic nowcasting models (CPI, NFP) compared to market-implied probabilities to find mispriced contracts.

This is Phase 1 of a dual-venue strategy. Phase 2 (dYdX v4 perp market making with Avellaneda-Stoikov) reuses the shared core infrastructure built here.

### Why Kalshi First

1. **$100 works**: Contracts cost $0.01-$0.99 each. $100 buys meaningful positions across 10-20 markets.
2. **Widest spreads**: 2-40% on prediction markets vs 5-25 bps on perp DEXes. Edge is 100-1000x larger per trade.
3. **No latency requirements**: Edge comes from better probability estimates, not speed. Minutes of edge after data releases.
4. **Regulatory safety**: CFTC-regulated DCM. USD-settled. No bridge/smart contract risk.
5. **Every trade produces calibration data**: Predicted probability vs actual outcome. This data improves models over time.

### How It Makes Money

1. **Weather (primary, 50-60% of capital)**: Calibrated GEFS/ECMWF ensemble forecasts vs Kalshi temperature markets. Daily resolution = fastest capital turnover. Competition is mostly intuition-based traders. 9/10 category rating.
2. **Economics (secondary, 20-30%)**: Bottom-up CPI estimates using Cleveland Fed Nowcast + BLS components. Monthly resolution. 7/10 rating.
3. **Market making on thin books**: When bid-ask spread exceeds model's edge threshold, quote both sides using maker orders only.
4. **Cross-market consistency**: Related markets (e.g., "CPI > 3%" and "CPI > 3.5%") must be logically consistent. They often aren't.

### Critical Insight: Makers vs Takers

Academic study of 72.1M Kalshi trades (Whelan, UCD) shows:
- Average pre-fee return across all contracts: **-20%**
- **Makers** average loss: ~10%
- **Takers** average loss: ~32%

**This system must use maker (limit) orders exclusively.** Taker orders are negative EV.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Main Thread                           │
│              Event Loop + Console Dashboard              │
└──────────────┬──────────────────────┬───────────────────┘
               │                      │
    ┌──────────▼──────────┐  ┌───────▼────────────┐
    │  Thread 1: Data     │  │ Thread 2: Strategy  │
    │  - Kalshi WS feed   │  │ - Probability engine│
    │  - Open-Meteo API   │  │ - Edge detection    │
    │  - BLS/FRED APIs    │  │ - Position sizing   │
    │  - Market catalog   │  │ - Kelly criterion   │
    └──────────┬──────────┘  └───────┬────────────┘
               │ SPSC                │ SPSC
    ┌──────────▼──────────┐  ┌───────▼────────────┐
    │  Thread 3: Execution│  │ Thread 4: Risk      │
    │  - Order placement  │  │ - Position tracking │
    │  - Fill tracking    │  │ - Kill switch       │
    │  - Settlement watch │  │ - Calibration log   │
    └─────────────────────┘  └────────────────────┘

Inter-thread: Lock-free SPSC ring buffers (same as Phase 2)
```

### Shared Core (reused in Phase 2: dYdX)

| Component | Location | Reuse |
|-----------|----------|-------|
| Common types | `src/core/types.hpp` | 100% |
| Config loader | `src/core/config.cpp` | 100% |
| Logging | `src/core/logging.cpp` | 100% |
| SPSC event bus | `src/core/event_bus.hpp` | 100% |
| IStrategy interface | `src/strategy/istrategy.hpp` | 100% |
| Risk manager | `src/risk/risk_manager.cpp` | 90% |
| Kill switch | `src/risk/kill_switch.cpp` | 95% |
| PnL tracker | `src/risk/pnl_tracker.cpp` | 90% |
| Main event loop | `src/main.cpp` | 80% |

### Kalshi-Specific

| Component | Location |
|-----------|----------|
| Kalshi REST client | `src/exchange/kalshi/rest_client.cpp` |
| Kalshi WebSocket | `src/exchange/kalshi/ws_client.cpp` |
| Kalshi auth (RSA-PSS) | `src/exchange/kalshi/auth.cpp` |
| Market catalog | `src/exchange/kalshi/market_catalog.cpp` |
| Probability engine | `src/strategy/kalshi/probability_engine.cpp` |
| Weather ensemble model | `src/strategy/kalshi/weather_model.cpp` |
| Open-Meteo feed | `src/strategy/kalshi/open_meteo_feed.cpp` |
| Economic data feed | `src/strategy/kalshi/econ_feed.cpp` |
| Event strategy | `src/strategy/kalshi/event_strategy.cpp` |
| Calibration logger | `src/strategy/kalshi/calibration.cpp` |

---

## Kalshi API Reference

### Endpoints
- **REST (Production)**: `https://api.elections.kalshi.com/trade-api/v2`
- **REST (Demo)**: `https://demo-api.kalshi.co/trade-api/v2`
- **WebSocket (Production)**: `wss://api.elections.kalshi.com/trade-api/ws/v2`
- **WebSocket (Demo)**: `wss://demo-api.kalshi.co/trade-api/ws/v2`
- **Docs**: `https://docs.kalshi.com`

### Authentication — RSA-PSS Signing

Kalshi uses RSA-PSS signed requests with three headers:
- `KALSHI-ACCESS-KEY`: Your API key ID
- `KALSHI-ACCESS-SIGNATURE`: Base64-encoded RSA-PSS signature of `timestamp + method + path`
- `KALSHI-ACCESS-TIMESTAMP`: Unix timestamp in **milliseconds** (not seconds — common gotcha)
- Signing: SHA256 with PSS padding, `DIGEST_LENGTH` salt

### Rate Limits by Tier
| Tier | Read/sec | Write/sec | Qualification |
|------|----------|-----------|---------------|
| Basic | 20 | 10 | Account signup |
| Advanced | 30 | 30 | Qualification form |
| Premier | 100 | 100 | 3.75% monthly volume + tech competency |
| Prime | 400 | 400 | 7.5% monthly volume + tech competency |

Write-limited operations: CreateOrder, CancelOrder, AmendOrder, DecreaseOrder, batch variants.
BatchCancelOrders: each cancel = 0.2 transactions.

### Key Endpoints
- `GET /markets` — List markets with filters (category, status)
- `GET /markets/{ticker}` — Market details (yes_ask, yes_bid, volume, close_time)
- `GET /markets/{ticker}/orderbook` — Orderbook (bids only, ascending, best bid last)
- `POST /portfolio/orders` — Place limit order (all API orders are limit)
- `DELETE /portfolio/orders/{order_id}` — Cancel order
- `GET /portfolio/positions` — Current positions
- `GET /portfolio/settlements` — Settlement history
- `GET /portfolio/balance` — Account balance

### Order Placement (API)
```json
{
  "ticker": "KXHIGHNY-26APR-T75",
  "side": "yes",
  "action": "buy",
  "count_fp": "5.00",
  "yes_price_dollars": "0.6500",
  "client_order_id": "<uuid>",
  "time_in_force": "gtc",
  "post_only": true
}
```

**Key API gotchas:**
- Prices are strings with 4 decimal places (e.g., "0.6500")
- Quantities use `count_fp` strings (e.g., "10.00")
- Orderbook returns bids only — no explicit asks; ascending order with best bid last
- Historical data partitioned: live data ~3 months; older via `/historical/*`
- All API orders are limit orders — no market orders via API
- Demo requires separate API keys from production

### Contract Mechanics
- Binary contracts: YES share pays $1.00 if event occurs, $0.00 otherwise
- NO share pays $1.00 if event does NOT occur, $0.00 otherwise
- YES price + NO price ≈ $1.00 (minus spread)
- Min order: 1 contract
- Settlement: USD, credited to account on resolution (1-12+ hours after market close)

### Fee Structure (Variable by Price)

**Taker fee**: `ceil(0.07 * contracts * price * (1 - price))`
**Maker fee**: `ceil(0.0175 * contracts * price * (1 - price))`

| Contract Price | Maker Fee/Contract | Taker Fee/Contract | Maker Fee % |
|---------------|-------------------|-------------------|-------------|
| $0.05 | ~0.08 cents | ~0.33 cents | 1.7% |
| $0.10 | ~0.16 cents | ~0.63 cents | 1.6% |
| $0.25 | ~0.33 cents | ~1.31 cents | 1.3% |
| $0.50 | ~0.44 cents | ~1.75 cents | 0.9% |
| $0.75 | ~0.33 cents | ~1.31 cents | 0.4% |
| $0.90 | ~0.16 cents | ~0.63 cents | 0.2% |

- **Maximum maker fee**: ~0.44 cents/contract (at 50-cent price)
- **No settlement fees** — winning contracts pay $1.00 with no deduction
- Fees are lowest at extreme prices (near $0.01 or $0.99)
- **Maker fees are 4x lower than taker fees** — always use post_only limit orders

### Void Rule Warning

Markets that cannot be determined do NOT always void. Kalshi may resolve at the **"last traded fair price"** instead of voiding. Documented case of $30K loss. Key risk in sports markets with uncertain participation. The risk manager must account for this when sizing positions in ambiguous-resolution markets.

---

## Category Priority & Capital Allocation

Based on analysis of 72.1M trades and category-level research (see `docs/KALSHI_RESEARCH.md` Section 11):

| Priority | Category | Rating | Capital % | Frequency | Why |
|----------|----------|--------|-----------|-----------|-----|
| **1** | Weather/Temperature | **9/10** | 50-60% | Daily | Best data, weakest competition, fastest turnover |
| **2** | Economics: CPI | **7/10** | 15-20% | Monthly | Strong per-trade edge, Cleveland Fed Nowcast |
| **3** | Hurricanes (seasonal) | **7/10** | 10-15% (Jun-Nov) | Episodic | Same NOAA skills as weather, fear premium |
| **4** | Economics: NFP | **6/10** | 5-10% | Monthly | ADP leading indicator, noisier than CPI |
| **5** | Fed/FOMC | **5/10** | Opportunistic | 8x/year | CME FedWatch arb, but most efficient Kalshi category |
| Skip | Sports, Crypto, Stocks | **3-4/10** | 0% | — | Mature competition, no edge from public data |

### Weather Markets — Primary Edge Source

**Why weather is #1:**
- Daily resolution across 6 cities = up to 6+ trades/day (fastest capital turnover)
- Competition is mostly intuition-based ("feels warm today")
- GEFS reforecast archive provides **20+ years** of backtesting data
- Resolution is a thermometer reading — zero ambiguity
- Documented mispricings of 5-15 cents vs ~0.3-0.5 cent maker fees
- Documented bot achieving $1.8K profit using GFS ensemble

**NWS Station Codes (Kalshi settlement sources):**
| City | Ticker Series | NWS Station |
|------|--------------|-------------|
| NYC | KXHIGHNY | KNYC |
| Chicago | KXHIGHCHI | KORD |
| Miami | KXHIGHMIA | KMIA |
| LA | KXHIGHLAX | KLAX |
| Denver | KXHIGHDEN | KDEN |
| Austin | KXHIGHAUS | KAUS |

**Primary data source: Open-Meteo API** (free, no key, clean JSON):
- GFS 31-member ensemble forecasts
- ECMWF 51-member ensemble forecasts
- Hourly resolution, 16-day horizon
- `https://ensemble-api.open-meteo.com/v1/ensemble`

**Supplementary sources:**
- NWS API (`api.weather.gov`) — official point forecasts
- NOAA National Blend of Models (NBM) — pre-calibrated probabilistic forecasts
- Iowa Environmental Mesonet — historical station observations
- GHCN (NOAA NCEI) — 100+ years of daily temperature records
- NOAA GEFS Reforecast Archive — 20+ years for backtesting

**Modeling approach:**
1. Pull GEFS/ECMWF ensemble from Open-Meteo for target station's grid point
2. Count ensemble members exceeding contract threshold → raw probability
3. Apply bias correction using rolling 30-60 day model-vs-actual residuals
4. Compare calibrated probability to Kalshi market price
5. Trade when divergence exceeds fee threshold (~3-5 cents)

---

## Risk Parameters

```yaml
risk:
  max_position_per_market: 10        # contracts (= $10 max exposure per market)
  max_total_exposure: 80             # dollars (keep $20 reserve)
  max_daily_loss: 15                 # dollars (15% of capital)
  max_markets_active: 20             # diversification across 20 markets
  min_edge_threshold: 0.05           # 5 cent probability divergence to trade
  min_spread_to_mm: 0.08             # 8 cents minimum spread to market-make
  kill_switch_loss: 30               # dollars (30% of capital)
  stale_model_hours: 24              # refresh models at least daily
  cash_reserve_pct: 0.20             # always keep 20% in cash
  maker_only: true                   # NEVER use taker orders
```

---

## Model Calibration Framework

Every trade produces a record:

```
{
  "market_ticker": "KXHIGHNY-26APR-T75",
  "category": "weather",
  "trade_time": "2026-04-01T14:30:00Z",
  "model_probability": 0.88,
  "market_probability": 0.70,
  "edge": 0.18,
  "side": "YES",
  "price": 0.70,
  "quantity": 5,
  "maker_fee": 0.0147,
  "resolution_time": "2026-04-02T12:00:00Z",
  "outcome": "YES",
  "pnl": 1.43
}
```

**Metrics tracked**:
- **Brier score**: Mean squared error of probability predictions. Target < 0.20 (Polymarket achieves 0.187 across 2,847 markets).
- **Calibration curve**: Predicted probability vs actual frequency in 10 buckets. Perfect = diagonal.
- **Category-level accuracy**: Track which event types perform best/worst.
- **Reliability-Resolution decomposition**: Separate calibration quality from discrimination power.

---

## Specs Breakdown

| # | Spec | Tasks | Scope |
|---|------|-------|-------|
| 01 | Project Scaffolding + Shared Core | 3 | CMake, types, config, logging, event bus |
| 02 | Kalshi Exchange Connectivity | 3 | REST client, RSA-PSS auth, WebSocket, orders |
| 03 | Public Data Feeds | 3 | Open-Meteo ensemble, BLS API, FRED API |
| 04 | Probability Engine | 3 | Weather ensemble model, econ models, consistency |
| 05 | Trading Strategy | 3 | Edge detection, Kelly sizing, market making mode |
| 06 | Risk Management + Calibration | 3 | Risk limits, kill switch, calibration + Brier |
| 07 | Integration + Live Trading | 3 | Main loop, dashboard, E2E paper + live |
| **Total** | | **21** | |

---

## Phase 2 Handoff (dYdX v4)

After Kalshi is live and profitable, Phase 2 adds dYdX perp market making. What carries over:

- **100%**: Core types, config, logging, event bus, main loop, IStrategy interface
- **90%**: Risk manager (add per-symbol perp limits), kill switch (add exchange-specific cancel)
- **New**: dYdX exchange connectivity, Avellaneda-Stoikov quoter, OBI signal, BTC-lead signal, order book structure

Estimated Phase 2 additional work: 4-6 weeks on top of shared core.

---

## References

- Kalshi API docs: `https://docs.kalshi.com`
- Kalshi fee schedule: `https://kalshi.com/docs/kalshi-fee-schedule.pdf`
- Whelan, "Makers and Takers" (UCD): `https://www.karlwhelan.com/Papers/Kalshi.pdf`
- Kalshi/NBER Fed study: `https://www.nber.org/system/files/working_papers/w34702/w34702.pdf`
- Open-Meteo ensemble API: `https://open-meteo.com/en/docs/ensemble-api`
- BLS API: `https://www.bls.gov/developers/`
- FRED API: `https://fred.stlouisfed.org/docs/api/fred/`
- Cleveland Fed Inflation Nowcast: `https://www.clevelandfed.org/indicators-and-data/inflation-nowcasting`
- Venue Analysis: `docs/VENUE_ANALYSIS.md`
- Original A-S Research: `docs/RESEARCH.md` (applies to Phase 2)
