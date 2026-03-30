# HFT Crypto Multi-Strategy Trading Bot — Full Specification

## 1. What This Project Is

A high-frequency multi-strategy trading bot written in C++20 that trades cryptocurrency on Binance. It runs multiple concurrent strategies across altcoin pairs, stacking uncorrelated edges for maximum profit potential. Targets sub-millisecond internal latency and runs on Windows 11.

This is a **retail-scale** HFT bot — not co-located at an exchange, but fast enough to compete with other retail algorithmic traders on crypto markets.

## 2. Strategy Overview

### Why Multi-Strategy

Running a single strategy on BTC/USDT is a red ocean — institutional market makers with co-located servers dominate. The profitable retail approach is to **stack uncorrelated edges**:

| Strategy | Edge Source | Competition | Expected Contribution |
|----------|-----------|-------------|----------------------|
| Altcoin Market Making | Wider spreads (10-40 bps) | Low-medium | Primary profit source |
| Funding Rate Arbitrage | Carry income (10-100%+ APR) | Low | Steady baseline income |
| Cross-Pair BTC Lead | BTC leads altcoins 100-500ms | Medium | Alpha overlay on MM |

### Strategy #1: Altcoin Market Making (Primary)

Uses the Avellaneda-Stoikov model to place bid/ask orders on mid-cap altcoin pairs. Profits from spread capture while managing inventory risk.

**Why altcoins over BTC:**
- BTC/USDT spreads: 1-3 bps (negative edge after 15 bps round-trip fees)
- Altcoin spreads: 10-40 bps (positive edge, less competition)
- You're competing with other retail bots, not Citadel

**Target pairs:** AVAX/USDT, ARB/USDT, NEAR/USDT, INJ/USDT, SUI/USDT
- 24h volume > $10M (enough fills) and < $500M (not dominated by institutions)
- Start with 3-5 pairs, each running an independent strategy instance

**How it works:**
```
reservation_price = weighted_mid - inventory * γ * σ² * T
                  + obi_weight * OBI * σ         ← order book imbalance
                  + btc_lead_weight * btc_signal  ← cross-pair alpha

optimal_spread = γ * σ² * T + (2/γ) * ln(1 + γ/k)
bid = reservation_price - optimal_spread/2
ask = reservation_price + optimal_spread/2
```

Places 3-5 levels per side with geometric size decay.

### Strategy #2: Funding Rate Arbitrage (Secondary)

Delta-neutral carry trade exploiting perpetual futures funding mechanism.

**How it works:**
1. Monitor predicted funding rates across target pairs
2. When funding > 0.02% per 8h: buy spot + short perp (delta neutral)
3. Collect funding payments every 8 hours
4. Exit when funding drops below threshold

**Math:** 0.03% funding * 3x daily * 365 days = ~33% APR on $10k notional = ~$9/day per pair

### Strategy #3: Cross-Pair BTC Lead (Alpha Overlay)

Not a standalone strategy — an alpha signal fed into the altcoin market maker.

BTC price movements lead altcoin movements by 100-500ms. When BTC drops, the altcoin MM immediately skews quotes to avoid buying into the coming dip.

## 3. Target Exchange: Binance

- **Why Binance**: Most liquid crypto exchange, well-documented API, free testnet
- **Testnet first**: All development uses `testnet.binance.vision` (spot) and `testnet.binancefuture.com` (futures)
- **Both Spot and Futures API**: Market making on spot, funding rate arb uses both
- **Initial pairs**: AVAXUSDT, ARBUSDT, NEARUSDT (market making) + BTC lead signal

## 4. Architecture

```
┌───────────────────────────────────────────────────────────────────────┐
│                          Main Thread                                   │
│                    (CLI / Console Dashboard)                            │
│  [MM: AVAX] [MM: ARB] [MM: NEAR] [Funding] [Portfolio] [Risk Status] │
└────────┬─────────────────────────────────────┬────────────────────────┘
         │ SPSC Queue                          │ SPSC Queue
┌────────▼────────────┐           ┌────────────▼──────────────────────┐
│  Thread 1:          │           │  Thread 4:                         │
│  Market Data        │           │  User Data Streams                 │
│                     │           │                                     │
│ Combined WS recv    │           │ Spot WS: executionReport (all sym) │
│ → JSON parse        │           │ Futures WS: fills + positions       │
│ → Route by symbol   │           │ → Route fills to correct strategy   │
│ → Per-symbol books  │           │ → Update position tracker           │
│ → BTC lead signal   │           └────────────┬──────────────────────┘
│ → Futures mark/fund │                        │ SPSC Queue
└────────┬────────────┘                        │
         │ SPSC Queue                          │
┌────────▼────────────┐                        │
│  Thread 2:          │                        │
│  Strategy Engine    │                        │
│                     │                        │
│ Route data → correct│                        │
│   strategy instance │                        │
│ [MM:AVAX] computes  │                        │
│ [MM:ARB]  computes  │                        │
│ [MM:NEAR] computes  │                        │
│ [Funding] monitors  │                        │
│ → Emit SignalEvents │                        │
└────────┬────────────┘                        │
         │ SPSC Queue                          │
┌────────▼─────────────────────────────────────▼──────────────────────┐
│  Thread 3: Execution Engine                                          │
│                                                                       │
│ Consume signals → Portfolio risk check → Route to Spot or Futures API│
│ MM signals: cancelReplace with LIMIT_MAKER + self-trade prevention   │
│ Funding signals: IOC orders on both spot + futures                    │
│ Kill switch: cancel ALL orders ALL symbols, flatten ALL positions     │
└──────────────────────────────────────────────────────────────────────┘

  Thread 5: Risk Monitor (watchdog — heartbeats, stale data, portfolio exposure)
```

### Inter-Component Communication

Lock-free SPSC ring buffers between threads — no mutexes on hot path.

| Queue | Producer | Consumer | Events |
|-------|----------|----------|--------|
| MarketDataBus | Market Data thread | Strategy thread | Per-symbol book updates, trades, BTC signal, funding rates |
| SignalBus | Strategy thread | Execution thread | QuoteUpdates (per symbol), FundingEntry/Exit |
| ExecutionBus | User Data thread | Execution thread | Fills (routed by symbol), order status updates |

## 5. Core Components

### 5.1 Market Data Feed (`src/exchange/`)

Multi-symbol market data ingestion via Binance combined streams.

- **Single WebSocket**: Combined stream for all altcoin depth + trades + BTC bookTicker
- **Futures WebSocket**: Mark price + funding rate streams
- **Per-symbol order books**: Maintained independently, routed by symbol field
- **BTC lead signal**: bookTicker only (no full book needed), computed return over rolling window
- **Performance**: < 5μs per book update per symbol

### 5.2 Core Infrastructure (`src/core/`)

- **Types**: Price, Quantity, Side, Symbol, event structs including FundingRateEvent
- **Config**: Multi-strategy YAML config (array of strategy instances + portfolio risk)
- **Logging**: Per-strategy loggers (e.g., `strategy.AVAXUSDT`, `funding_rate`)
- **Event bus**: SPSC queues with symbol field for routing

### 5.3 Strategy Engine (`src/strategy/`)

**IStrategy interface** — all strategies implement this:
```
init(config, logger) | on_market_data(event) | on_fill(event) | on_timer(ts) | shutdown()
```

**MarketMakingStrategy** (one instance per altcoin pair):
- Avellaneda-Stoikov reservation price + optimal spread
- EWMA volatility (short-term + long-term for regime detection)
- OBI signal from local order book
- BTC lead signal for directional skew
- Multi-level quoting with tick/lot size compliance
- Inventory penalty amplification near limits
- Volatility regime detection (pull quotes in extreme vol)

**FundingRateStrategy** (one instance, monitors multiple pairs):
- Monitors predicted funding via REST polling
- Enters delta-neutral positions (spot + perp) when funding is attractive
- Exits when funding drops or basis diverges

### 5.4 Order Execution (`src/exchange/`)

- **Spot REST**: LIMIT_MAKER orders with selfTradePreventionMode=EXPIRE_MAKER
- **Atomic cancel-replace**: Primary requoting mechanism (minimizes unquoted time)
- **Futures REST**: IOC orders for funding rate entries/exits
- **User Data Streams**: Spot + Futures, single stream each covers all symbols
- **Multi-symbol order tracking**: Per-symbol state machines

### 5.5 Risk Management (`src/risk/`)

**Two levels of risk:**

| Level | Scope | Limits |
|-------|-------|--------|
| Per-symbol | Each trading pair independently | Vol-scaled position limit, order rate, data freshness |
| Portfolio | Aggregate across all symbols + strategies | Total exposure, correlation-adjusted exposure, daily P&L, drawdown |

**Kill switch**: Cancels ALL orders on ALL symbols (spot + futures), flattens ALL positions, requires manual restart. Runs on separate watchdog thread.

### 5.6 Backtesting (`src/backtest/`)

- **Multi-symbol replay**: Merges historical data for all symbols chronologically
- **Queue-aware fills**: Conservative model (back-of-queue assumption)
- **Per-strategy metrics**: P&L attribution breakdown per strategy type
- **Walk-forward optimization**: Prevents overfitting, per-symbol parameter tuning

## 6. Technology Choices

| Component | Library | Why |
|-----------|---------|-----|
| Language | C++20 | Low-latency, zero-cost abstractions |
| Build | CMake + vcpkg | Industry standard, Windows-friendly |
| WebSocket/HTTP | Boost.Beast | Mature, async I/O via Boost.Asio |
| JSON | nlohmann/json | Header-only, fast, intuitive |
| Logging | spdlog | Lock-free, structured, sinks |
| Config | yaml-cpp | Human-readable, nested structure |
| TLS | OpenSSL | Required for wss:// and HTTPS |
| Testing | Google Test | Industry standard |
| Platform | Windows 11 | MSVC compiler |

## 7. Configuration

See `docs/RESEARCH.md` Appendix A for the full multi-strategy config with parameter recommendations.

Key structure:
```yaml
strategies:
  - name: "market_making"
    symbol: "AVAXUSDT"
    gamma: 0.01
    btc_lead_weight: 0.2
    # ... per-pair params

  - name: "market_making"
    symbol: "ARBUSDT"
    gamma: 0.015              # More conservative for less liquid pair
    # ...

  - name: "funding_rate"
    pairs: ["AVAXUSDT", "ARBUSDT", "NEARUSDT"]
    min_funding_rate: 0.0002

cross_pair_signals:
  btc_stream: "BTCUSDT"

risk:
  max_position_usd: 500            # Per symbol
  max_portfolio_exposure_usd: 3000  # Total
  max_daily_loss_usd: 100
  kill_switch_loss_usd: 200
```

## 8. Directory Structure

```
Trader/
├── CMakeLists.txt
├── vcpkg.json
├── CLAUDE.md
│
├── include/
│   ├── common/types.h
│   ├── core/{config.h, event_bus.h, logger.h}
│   ├── exchange/{binance_ws.h, binance_rest.h, binance_futures.h, order_book.h, order_manager.h}
│   ├── strategy/{istrategy.h, market_making.h, funding_rate.h, volatility.h, signals.h}
│   ├── risk/{position_tracker.h, risk_gate.h, kill_switch.h}
│   └── backtest/{data_loader.h, replay_engine.h, sim_matcher.h, metrics.h}
│
├── src/{core/, exchange/, strategy/, risk/, backtest/, main.cpp}
├── tests/
├── config/{config.example.yaml}
├── data/                    # Historical data per symbol (gitignored)
├── logs/                    # Runtime logs (gitignored)
├── docs/RESEARCH.md         # Full quantitative research
└── .pm/todo/hft-foundation/ # Sprint specs and tasks
```

## 9. Build Phases (39 tasks)

| Phase | Tasks | What It Does |
|-------|-------|-------------|
| 1. Scaffolding | 1 | CMake + vcpkg + directory structure |
| 2. Market Data | 2-6 | Multi-symbol WebSocket, order books, BTC lead, futures data |
| 3. Core Infra | 7-10 | Types, multi-strategy config, logging, event bus |
| 4. Strategy | 11-19 | IStrategy, A-S model, OBI, BTC lead, multi-level, funding rate |
| 5. Execution | 20-25 | Spot + futures REST, cancel-replace, user data streams |
| 6. Risk | 26-30 | Per-symbol + portfolio tracking, vol-scaled limits, kill switch |
| 7. Backtesting | 31-35 | Multi-symbol data, replay, queue-aware fills, walk-forward |
| 8. Integration | 36-39 | Main loop, dashboard, E2E testnet (MM + funding) |

## 10. Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Target pairs | Altcoins, not BTC | Wider spreads (10-40 bps), less competition, real retail edge |
| Multi-strategy | MM + Funding + BTC lead | Stack uncorrelated edges, shared infrastructure |
| Threading | 1 thread per component + watchdog | Simple, SPSC queues, no shared mutable state |
| Order type | LIMIT_MAKER + cancel-replace | Guaranteed maker fees, minimal unquoted time |
| Risk levels | Per-symbol + portfolio | Correlation risk across altcoins is real |
| Kill switch | Separate watchdog thread | Must work even if strategy thread hangs |
| Backtesting fills | Queue-aware (back of queue) | Optimistic fills produce fantasy returns |
| Config | Multi-instance array | Same strategy class, different params per symbol |

## 11. Risk Warnings

- **This is a learning project with profit potential.** Set expectations: retail quant trading is hard.
- **Start with testnet only.** Never trade real money with untested code.
- **Altcoin risks**: Lower liquidity means wider spreads (good) but also more volatile moves (bad) and potential delisting.
- **Funding rate can flip**: Positions earning carry can start costing money. Must exit quickly.
- **Correlation risk**: Being long 3 altcoins is essentially a leveraged BTC bet during market-wide moves.
- **Latency disadvantage**: 10-50ms home latency vs < 1ms co-located. Algorithmic edge must compensate.

## 12. Glossary

| Term | Definition |
|------|------------|
| **Bid/Ask** | Buy/sell price in the order book |
| **Spread** | Ask minus bid — the market maker's gross profit per round trip |
| **BPS** | Basis points. 1 bps = 0.01%. 10 bps = 0.1% |
| **Mid-price** | (best_bid + best_ask) / 2 |
| **Weighted mid** | Mid-price adjusted for order size imbalance at top of book |
| **Inventory** | Net position (positive = long, negative = short) |
| **Maker/Taker** | Maker adds liquidity (lower fees), taker removes it (higher fees) |
| **POST_ONLY** | Order type guaranteeing maker status — rejected if it would cross |
| **Fill** | When our order gets executed |
| **Kill switch** | Emergency stop — cancels everything, flattens all positions |
| **OBI** | Order Book Imbalance — predictive signal for short-term price direction |
| **BTC lead** | BTC price movements predict altcoin movements 100-500ms ahead |
| **Funding rate** | Periodic payment between longs and shorts on perpetual futures |
| **Delta neutral** | Position with zero net market exposure (long spot + short perp) |
| **SPSC queue** | Single-producer single-consumer lock-free ring buffer |
| **Skew** | Shifting bid/ask to favor reducing inventory |
| **Sharpe ratio** | Risk-adjusted return. > 3.0 target for HFT |
| **Adverse selection** | Getting filled by informed traders who know price is about to move against you |
| **Walk-forward** | Backtest methodology that prevents overfitting by rolling optimization windows |
| **Gamma (γ)** | Risk aversion parameter in Avellaneda-Stoikov model |
