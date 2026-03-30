# Core Infrastructure

**Status**: Not Started

---

## Scope

Build shared infrastructure components used across all modules: configuration management, logging, timing utilities, and the event/message bus that connects components together.

**This spec covers**:
- Configuration system (YAML/JSON config files for API keys, symbols, parameters)
- Structured logging with spdlog (file + console, with microsecond timestamps)
- High-resolution timing utilities (clock, latency measurement)
- Thread-safe lock-free event bus for inter-component communication
- Common type definitions (Price, Quantity, OrderId, Side, etc.)

**Out of scope**:
- Market data specifics → `01-market-data-feed.md`
- Strategy logic → `03-strategy-engine.md`

---

## What's Done

| Item | Status |
|------|--------|
| Config system | Not started |
| Logging | Not started |
| Event bus | Not started |
| Common types | Not started |

---

## Technical Context

### Common Types
```cpp
using Price = double;       // Consider fixed-point for production
using Quantity = double;
using Timestamp = std::chrono::steady_clock::time_point;
enum class Side { Buy, Sell };
enum class OrderStatus { New, PartiallyFilled, Filled, Canceled, Rejected };
```

### Event Bus Design
- Lock-free SPSC (single-producer single-consumer) queues between components
- Each component runs on its own thread
- Events: MarketDataUpdate, Signal, OrderRequest, OrderResponse, Fill

### Config Structure
```yaml
exchange:
  api_key: "..."
  api_secret: "..."
  futures_api_key: "..."       # Separate key for futures (funding rate strategy)
  futures_api_secret: "..."
  testnet: true

# Multiple strategies, each targeting a different symbol or approach
strategies:
  - name: "market_making"
    symbol: "AVAXUSDT"
    # Strategy-specific params (see 03-strategy-engine.md)

  - name: "market_making"
    symbol: "ARBUSDT"

  - name: "funding_rate"
    pairs: ["AVAXUSDT", "ARBUSDT", "NEARUSDT"]

# BTC lead signal (subscribed for cross-pair alpha, not traded)
cross_pair_signals:
  btc_stream: "BTCUSDT"

risk:
  max_position_usd: 500           # Per symbol
  max_portfolio_exposure_usd: 3000 # Total across all strategies
  max_daily_loss_usd: 100
  max_orders_per_second: 5
```

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 1 | Common type definitions and constants | Types compile, used consistently across project |
| 2 | Configuration loader (YAML parsing, validation) | Loads config file, unit tests for missing/invalid keys |
| 3 | Logging setup with spdlog (console + file, μs timestamps) | Logging works across threads, log file rotates correctly |
| 4 | Lock-free event bus (SPSC queue + event types) | Events flow between producer/consumer threads, benchmark < 100ns per event |

---

## References

- spdlog: https://github.com/gabime/spdlog
- yaml-cpp: https://github.com/jbeder/yaml-cpp
