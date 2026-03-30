# 02 — Kalshi Exchange Connectivity

**Status**: Not Started

---

## Scope

Build the Kalshi REST and WebSocket clients with authentication, market discovery, order management, and position tracking.

**This spec covers**:
- REST client with authentication (RSA or HMAC — verify from current docs)
- Market catalog: discover, filter, and cache available markets
- Order placement, cancellation, and fill tracking
- WebSocket client for real-time orderbook and fill updates
- Settlement monitoring (detect when markets resolve)

**Out of scope**:
- Trading strategy decisions → `05-trading-strategy.md`
- Probability modeling → `04-probability-engine.md`
- Risk limits → `06-risk-and-calibration.md`

---

## What's Done

| Item | Status |
|------|--------|
| REST client + auth | Not started |
| Market catalog + WebSocket | Not started |
| Order management + settlement | Not started |

---

## Technical Details

### Authentication — RSA-PSS Signing

Kalshi uses RSA-PSS signed requests (confirmed from current docs at `docs.kalshi.com`):

```
1. Generate RSA key pair (2048-bit minimum)
2. Register public key with Kalshi via dashboard
3. For each request, sign: timestamp_ms + method + path (no body)
4. Signature: RSA-PSS with SHA256, salt length = DIGEST_LENGTH
5. Encode signature as Base64
6. Send three headers:
   - KALSHI-ACCESS-KEY: your API key ID
   - KALSHI-ACCESS-TIMESTAMP: Unix timestamp in MILLISECONDS (not seconds!)
   - KALSHI-ACCESS-SIGNATURE: Base64-encoded RSA-PSS signature
```

**Common gotcha**: Timestamp must be in **milliseconds**. Using seconds will silently fail auth.
**Implementation**: Use OpenSSL's EVP_PKEY API for RSA-PSS signing (already in our dependency list).

### Market Catalog

```cpp
struct KalshiMarket {
    std::string ticker;          // e.g., "CPI-25APR-T3.5"
    std::string title;           // Human-readable
    std::string category;        // "Economics", "Weather", "Fed"
    std::string status;          // "open", "closed", "settled"
    double yes_bid;              // Best YES bid
    double yes_ask;              // Best YES ask
    double last_price;
    int volume;
    int open_interest;
    Timestamp close_time;        // When trading closes
    Timestamp expiration_time;   // When outcome resolves
    std::string result;          // "yes", "no", "" (if not settled)
};
```

**Market filtering**: Query `GET /markets` with category filter, cache locally, refresh every 5 minutes. Track markets approaching resolution (close_time within 7 days).

### Order Management

```cpp
struct KalshiOrder {
    OrderId id;
    std::string market_ticker;
    Side side;                    // Buy or Sell
    std::string contract_side;    // "yes" or "no"
    double price;                 // $0.01-$0.99
    int quantity;                 // Number of contracts
    std::string type;             // "limit" or "market"
    OrderStatus status;
    Timestamp created_at;
    int filled_quantity;
    double avg_fill_price;
};
```

### Settlement Monitoring

Markets resolve at `expiration_time`. After resolution:
1. Winning contracts pay $1.00 per contract
2. Losing contracts pay $0.00
3. Capital is freed for redeployment

Must poll `GET /portfolio/settlements` or detect via WebSocket to:
- Update PnL
- Free capital for new positions
- Log outcome for calibration

### API URLs

| Environment | REST | WebSocket |
|-------------|------|-----------|
| Production | `https://api.elections.kalshi.com/trade-api/v2` | `wss://api.elections.kalshi.com/trade-api/ws/v2` |
| Demo | `https://demo-api.kalshi.co/trade-api/v2` | `wss://demo-api.kalshi.co/trade-api/ws/v2` |

**Demo requires separate API keys from production.** Use demo for all development and testing. Same API structure, fake money.

### API Gotchas
- Prices are **strings with 4 decimal places** (e.g., "0.6500"), not integers or floats
- Quantities use `count_fp` strings (e.g., "10.00")
- Orderbook returns **bids only** — ascending order, best bid last. No explicit asks.
- All API orders are **limit orders** — no market orders via API
- Historical data partitioned: live data ~3 months; older via `/historical/*`
- WebSocket channels (even public ones) **require authentication headers**

### Fee Calculation (must be in order logic)
```
maker_fee = ceil(0.0175 * contracts * price * (1 - price))
taker_fee = ceil(0.07 * contracts * price * (1 - price))
```
Always use `post_only: true` to guarantee maker fees (4x lower than taker).

---

## Suggested Tasks

| # | Task | Done When |
|---|------|-----------|
| 4 | Kalshi REST client + auth + market catalog | Authenticates with demo API, fetches market list, filters by category, caches markets. Unit tests mock HTTP responses. Integration test hits demo API and returns real market data. |
| 5 | Kalshi WebSocket client + real-time orderbook | Connects to demo WebSocket, receives orderbook updates and trade feed, maintains local best bid/ask per subscribed market. Unit test verifies message parsing. |
| 6 | Order placement + fill tracking + settlement monitoring | Places limit orders on demo API, tracks fills via WebSocket and REST polling, detects settlements, updates position state. Integration test places and cancels an order on demo. |
