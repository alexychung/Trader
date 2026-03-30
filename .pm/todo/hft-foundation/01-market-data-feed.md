# Market Data Feed Handler

**Status**: Not Started

---

## Scope

Build the foundational market data ingestion layer that connects to Binance WebSocket API, receives real-time order book updates and trade streams, and maintains local order book representations for **multiple symbols simultaneously**.

This is the foundation everything else builds on — without fast, reliable market data, no strategy can function. The bot runs multiple strategies across multiple altcoin pairs plus a BTC lead signal feed.

**This spec covers**:
- WebSocket connection management (connect, reconnect, heartbeat)
- Multi-symbol stream subscription via Binance combined streams
- Binance market data stream subscription (depth, trades, ticker) for N symbols
- Binary/JSON message parsing and deserialization
- Per-symbol local order book construction and maintenance from depth updates
- BTC/USDT bookTicker stream for cross-pair alpha signals
- Market data event bus for downstream consumers
- Binance Futures market data for funding rate strategy (`fapi` endpoints)

**Out of scope**:
- Strategy/signal logic → `03-strategy-engine.md`
- Order placement → `04-order-execution.md`
- Historical data / backtesting → `06-backtesting.md`

---

## What's Done

| Item | Status |
|------|--------|
| WebSocket client | Not started |
| Order book builder | Not started |
| Event bus | Not started |

---

## Technical Context

### Binance WebSocket API
- **Endpoint**: `wss://stream.binance.com:9443/ws/<streamName>`
- **Combined streams**: `wss://stream.binance.com:9443/stream?streams=<stream1>/<stream2>`
- **Depth stream**: `<symbol>@depth@100ms` (100ms updates, best for HFT)
- **Trade stream**: `<symbol>@trade`
- **Ticker stream**: `<symbol>@bookTicker` (best bid/ask, fastest)
- **Limits**: 5 WebSocket connections per IP, each supporting up to 1024 combined streams

### Binance Futures API (for funding rate strategy)
- **Funding rate**: `GET /fapi/v1/premiumIndex` — current funding rate + predicted next
- **Futures book ticker**: `wss://fstream.binance.com/ws/<symbol>@bookTicker`
- **Mark price stream**: `<symbol>@markPrice@1s`

### Multi-Symbol Architecture
Subscribe to combined streams for all target symbols in a single WebSocket connection:
```
wss://stream.binance.com:9443/stream?streams=
  avaxusdt@depth@100ms/avaxusdt@trade/avaxusdt@bookTicker/
  arbusdt@depth@100ms/arbusdt@trade/arbusdt@bookTicker/
  btcusdt@bookTicker    ← lead signal only, not traded
```

Maintain one OrderBook instance per symbol. Route parsed messages to the correct book by symbol field.

### Order Book Management
- Initial snapshot via REST: `GET /api/v3/depth?symbol=<SYMBOL>&limit=1000` (for each symbol)
- Apply buffered depth updates where `U <= lastUpdateId+1 AND u >= lastUpdateId+1`
- Drop updates where `u < lastUpdateId`
- This follows Binance's official "How to manage a local order book correctly" guide

### Performance Requirements
- Message parse latency: < 1μs target
- Order book update latency: < 5μs target per symbol
- Zero-allocation hot path (pre-allocated buffers)
- Must handle N symbols without latency degradation

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 1 | Project scaffolding (CMake, vcpkg, directory structure) | Project builds with all dependencies, hello world runs |
| 2 | WebSocket client wrapper using Boost.Beast | Connects to Binance, receives raw messages, auto-reconnects |
| 3 | JSON message parser for Binance depth/trade/ticker streams | Parses all 3 message types into typed structs, unit tests pass |
| 4 | Order book data structure (sorted price levels, qty tracking) | Insert/update/delete operations with unit tests, < 1μs per op |
| 5 | Multi-symbol order book manager (snapshot + incremental updates) | Builds books for N symbols from REST snapshots, routes WebSocket updates to correct book, applies depth updates per Binance spec |
| 6 | Binance Futures data feed (funding rate + mark price streams) | Fetches current/predicted funding rates, subscribes to mark price stream, unit tests pass |

---

## References

- Binance WebSocket API: https://binance-docs.github.io/apidocs/spot/en/#websocket-market-streams
- Binance depth management: https://binance-docs.github.io/apidocs/spot/en/#diff-depth-stream
