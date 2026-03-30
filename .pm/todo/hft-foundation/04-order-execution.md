# Order Execution

**Status**: Not Started

---

## Scope

Build the order management system that translates strategy signals into Binance API calls, tracks order state, and handles fills/cancellations.

**This spec covers**:
- Binance Spot REST API client (order placement, cancellation, account info)
- **Binance Futures REST API client** (for funding rate strategy — perp order placement)
- HMAC-SHA256 request signing for authenticated endpoints (both spot and futures)
- POST_ONLY order type enforcement (guaranteed maker, prevents accidental taker fills)
- Atomic cancel-replace via Binance `cancelReplace` endpoint (minimizes unquoted time)
- Self-trade prevention (`selfTradePreventionMode: EXPIRE_MAKER` on every order)
- Order state machine (New → PartialFill → Filled / Canceled)
- **Multi-symbol order tracking** (manages orders across N altcoin pairs simultaneously)
- Rate limiting per symbol and globally (Binance enforces 1200 requests/minute, 10 orders/second)
- Binance User Data Stream (WebSocket for real-time order/fill updates — covers all symbols)

**Out of scope**:
- What orders to place → `03-strategy-engine.md`
- Position limits and loss limits → `05-risk-management.md`

---

## What's Done

| Item | Status |
|------|--------|
| REST client | Not started |
| Request signing | Not started |
| Order state machine | Not started |
| User data stream | Not started |

---

## Technical Context

### Binance Order API
- **Place order**: `POST /api/v3/order` (HMAC signed)
  - Always use `timeInForce: GTC` with `type: LIMIT_MAKER` (alias for POST_ONLY)
  - Alternatively: `type: LIMIT` with `timeInForce: GTC` — but LIMIT_MAKER guarantees maker-only
  - **POST_ONLY is mandatory**: If a limit order would immediately match (cross the spread), LIMIT_MAKER rejects it instead of executing as taker. This guarantees maker fee rates and prevents accidental taking.
- **Cancel-replace**: `POST /api/v3/order/cancelReplace` (HMAC signed)
  - Atomically cancels an existing order and places a new one in a single API call
  - **Use this as the primary requoting mechanism** — eliminates the window where you're unquoted between a cancel and a new placement
  - Parameters: `cancelOrderId` + all new order params
- **Cancel order**: `DELETE /api/v3/order` (HMAC signed)
- **Cancel all**: `DELETE /api/v3/openOrders` (HMAC signed) — used by kill switch
- **Check order**: `GET /api/v3/order` (HMAC signed)
- **Account info**: `GET /api/v3/account` (HMAC signed)

### Self-Trade Prevention
Every order MUST include `selfTradePreventionMode`:
- `EXPIRE_MAKER` (recommended): If your new order would match your existing resting order, the resting (maker) order is cancelled and the new order proceeds. This is safest for market making — your newer quote is more likely to reflect current fair value.
- `EXPIRE_BOTH`: Both orders are cancelled. Safer but wastes the opportunity.
- Without this, your own bid can match your own ask, wasting fees and confusing position tracking.

### Request Signing
```
query_string = "symbol=BTCUSDT&side=BUY&type=LIMIT_MAKER&..."
signature = HMAC-SHA256(query_string, api_secret)
```

### User Data Stream (Real-time fills)
1. `POST /api/v3/userDataStream` → get listenKey
2. Connect WebSocket to `wss://stream.binance.com:9443/ws/<listenKey>`
3. Receive `executionReport` events for order updates
4. Keepalive: `PUT /api/v3/userDataStream` every 30 minutes

### Testnet
- REST: `https://testnet.binance.vision`
- WebSocket: `wss://testnet.binance.vision/ws`
- Free test funds, same API surface — use this during development

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 1 | HTTP client with HMAC-SHA256 signing for Binance Spot + Futures REST API | Signed requests pass both spot and futures testnet validation, unit tests for signature generation |
| 2 | Order placement with POST_ONLY/LIMIT_MAKER and self-trade prevention | Places LIMIT_MAKER orders with selfTradePreventionMode on testnet, verifies rejection on would-cross |
| 3 | Atomic cancel-replace for quote updates | Uses cancelReplace endpoint to update quotes in a single call, handles partial fill edge cases |
| 4 | Futures order placement (for funding rate strategy) | Places and cancels orders on Binance Futures testnet, supports IOC for funding entries/exits |
| 5 | User Data Stream (WebSocket for order/fill events across all symbols) | Receives real-time fill notifications for all symbols, parses executionReport, routes to correct strategy |
| 6 | Multi-symbol order state machine and tracking | Tracks open orders per symbol, reconciles state from fill events, unit tests for multi-symbol state transitions |

---

## References

- Binance REST API: https://binance-docs.github.io/apidocs/spot/en/#new-order-trade
- Binance User Data Stream: https://binance-docs.github.io/apidocs/spot/en/#user-data-streams
- Binance Testnet: https://testnet.binance.vision
