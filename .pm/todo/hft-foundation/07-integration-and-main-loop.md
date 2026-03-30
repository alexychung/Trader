# Integration and Main Loop

**Status**: Not Started

---

## Scope

Wire all components together into a working multi-strategy trading bot with a main event loop, CLI interface, and testnet paper trading capability.

**This spec covers**:
- Main event loop (orchestrates all components across multiple symbols and strategies)
- Multi-strategy instance management (N market making instances + funding rate strategy)
- Component lifecycle management (init, start, stop, shutdown)
- CLI interface (start, stop, status, kill switch hotkey, per-strategy status)
- Testnet paper trading mode (full end-to-end on Binance testnet)
- Console dashboard (live P&L per strategy and total, positions, spreads, order counts)

**Out of scope**:
- Individual component internals → specs 01-06

---

## What's Done

| Item | Status |
|------|--------|
| Main loop | Not started |
| Component wiring | Not started |
| CLI interface | Not started |
| Console dashboard | Not started |

---

## Technical Context

### Main Loop Architecture
```
Thread 1: Market Data (WebSocket recv → parse → per-symbol order book update → event bus)
         Handles: combined stream for all altcoin pairs + BTC lead signal + futures mark price
Thread 2: Strategy Engine (consume market data → route to correct strategy instance → emit signals)
         Runs: N market making instances (one per altcoin) + 1 funding rate strategy
Thread 3: Execution (consume signals → portfolio risk check → send orders → track state)
         Manages: orders across all symbols on spot + futures
Thread 4: User Data Stream (WebSocket recv → fill events → position update per symbol)
Thread 5: Risk Monitor (watchdog — monitors heartbeats, stale data, portfolio exposure)
Main Thread: CLI / Dashboard (display per-strategy stats, handle user input)
```

### Component Startup Order
1. Load config (all strategy instances + risk params)
2. Initialize logging
3. Start event buses
4. Connect market data WebSocket (combined stream for all symbols + BTC + futures)
5. Build initial order books (REST snapshot per symbol)
6. Instantiate strategy instances via StrategyFactory (N market making + 1 funding rate)
7. Connect user data streams (spot + futures)
8. Start execution engine
9. Start risk monitor watchdog
10. Enable trading

### Graceful Shutdown
1. Disable new order generation on ALL strategies
2. Cancel all open orders on ALL symbols (spot + futures)
3. Wait for cancellation confirmations
4. Disconnect all WebSockets
5. Flush logs
6. Exit

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 1 | Main event loop with multi-strategy instance management | All components start/stop correctly, N strategy instances instantiated from config, clean shutdown across all |
| 2 | Console dashboard (per-strategy and portfolio stats) | Shows per-symbol P&L + positions + spreads, portfolio totals, funding rate status, risk utilization |
| 3 | End-to-end testnet: altcoin market making on 2+ pairs | Bot runs on testnet making markets on 2+ altcoin pairs simultaneously, BTC lead signal active, 10+ minutes no crash |
| 4 | End-to-end testnet: funding rate strategy | Funding rate strategy monitors rates, enters/exits positions on futures testnet, spot+perp legs tracked |

---

## References

- Binance Testnet: https://testnet.binance.vision
